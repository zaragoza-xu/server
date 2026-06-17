import asyncio
import random
import time
from dataclasses import dataclass, field
from typing import Any, Dict, List

from client import TcpPersistentClient, TcpShortClient
from flow_helpers import (
    battle_sync_payload,
    create_room,
    enter_map_phase,
    join_room,
    lobby_set_ready,
    login_uid,
    register_uid,
    setup_room_to_battle,
    start_battle_for_room,
    target_host,
)
from metrics import RunMetrics


SUCCESS_CODE = 1


def _client_timeout(config: Dict[str, Any]) -> float:
    return float(config.get("timeout_seconds", 10.0))


def room_id_from_response(resp: Dict[str, Any]) -> int:
    data = resp.get("data", {})
    if "roomInfo" in data:
        return int(data.get("roomInfo", {}).get("roomId", -1))
    return int(data.get("roomId", -1))


@dataclass
class ScenarioContext:
    uid_pool: List[str] = field(default_factory=list)
    room_id: int = -1
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)


def _extract_response_code(resp: Dict[str, Any]) -> int:
    if "code" in resp:
        return int(resp.get("code", -1))
    # LongEnvelope successes (no `code` field).
    rtype = int(resp.get("type", -1))
    if rtype in (0, 1, 2, 8):  # map/battle/home async lines
        return SUCCESS_CODE
    return -1


async def _request_and_record(client: TcpShortClient, payload: Dict[str, Any], metrics: RunMetrics) -> Dict[str, Any]:
    started = time.perf_counter()
    try:
        resp, meta = await client.request(payload)
    except Exception:
        elapsed_ms = (time.perf_counter() - started) * 1000
        metrics.add(
            response_code=-1,
            connect_ok=False,
            send_ok=False,
            ttfb_ok=False,
            recv_ok=False,
            close_ok=False,
            connect_ms=0.0,
            send_ms=0.0,
            ttfb_ms=0.0,
            recv_ms=0.0,
            e2e_ms=elapsed_ms,
        )
        return {"code": -1, "message": "exception", "data": {}}

    metrics.add(
        response_code=_extract_response_code(resp),
        connect_ok=bool(meta.get("connect_ok", False)),
        send_ok=bool(meta.get("send_ok", False)),
        ttfb_ok=bool(meta.get("ttfb_ok", False)),
        recv_ok=bool(meta.get("recv_ok", False)),
        close_ok=bool(meta.get("close_ok", False)),
        connect_ms=float(meta.get("connect_latency_ms", 0.0)),
        send_ms=float(meta.get("send_latency_ms", 0.0)),
        ttfb_ms=float(meta.get("ttfb_latency_ms", 0.0)),
        recv_ms=float(meta.get("recv_latency_ms", 0.0)),
        e2e_ms=float(meta.get("e2e_latency_ms", 0.0)),
    )
    return resp


async def auth_register_login(config: Dict[str, Any], metrics: RunMetrics) -> None:
    target = config["target"]
    duration = int(config["duration_seconds"])
    concurrency = int(config["concurrency"])
    end_at = time.monotonic() + duration
    ctx = ScenarioContext()
    client = TcpShortClient(target["host"], int(target["port"]))

    async def worker() -> None:
        while time.monotonic() < end_at:
            pick = random.random()
            if pick < 0.3 or not ctx.uid_pool:
                resp = await _request_and_record(client, {"type": 1, "uid": ""}, metrics)
                uid = resp.get("data", {}).get("uid", "")
                if int(resp.get("code", -1)) == SUCCESS_CODE and uid:
                    async with ctx.lock:
                        ctx.uid_pool.append(uid)
            else:
                async with ctx.lock:
                    uid = random.choice(ctx.uid_pool) if ctx.uid_pool else ""
                payload = {"type": 0, "uid": uid}
                await _request_and_record(client, payload, metrics)

    await asyncio.gather(*[worker() for _ in range(concurrency)])


async def lobby_join_hot_room(config: Dict[str, Any], metrics: RunMetrics) -> None:
    duration = int(config["duration_seconds"])
    concurrency = int(config["concurrency"])
    keepalive_interval_s = float(
        config.get("keepalive_interval_seconds", config.get("heartbeat_interval_seconds", 5.0))
    )
    end_at = time.monotonic() + duration
    auth_target = config.get("auth_target", {"host": "127.0.0.1", "port": 8765})
    timeout = _client_timeout(config)
    auth = TcpShortClient(auth_target["host"], int(auth_target["port"]), timeout_seconds=timeout)
    lobby = TcpShortClient(config["target"]["host"], int(config["target"]["port"]), timeout_seconds=timeout)
    ctx = ScenarioContext()

    # Build minimal uid pool and a hotspot room (must login before lobby ops).
    for _ in range(max(20, concurrency // 2)):
        resp = await _request_and_record(auth, {"type": 1, "uid": ""}, metrics)
        uid = resp.get("data", {}).get("uid", "")
        if int(resp.get("code", -1)) == SUCCESS_CODE and uid:
            login = await _request_and_record(auth, {"type": 0, "uid": uid}, metrics)
            if int(login.get("code", -1)) == SUCCESS_CODE:
                ctx.uid_pool.append(uid)
    if not ctx.uid_pool:
        return
    creator_uid = ctx.uid_pool[0]
    create_resp = await _request_and_record(
        lobby, {"type": 0, "uid": creator_uid, "maximumPeople": 1000000}, metrics
    )
    ctx.room_id = room_id_from_response(create_resp)
    if ctx.room_id <= 0:
        return

    async def worker() -> None:
        while time.monotonic() < end_at:
            uid = random.choice(ctx.uid_pool)
            if random.random() < 0.8:
                resp = await _request_and_record(lobby, {"type": 1, "roomId": ctx.room_id, "uid": uid}, metrics)
                code = int(resp.get("code", -1))
                # room state error is expected in hotspot contention, but any other failure can hint consistency issues.
                if code not in (SUCCESS_CODE, 258):
                    metrics.count("state_violations")
            else:
                await _request_and_record(lobby, {"type": 2, "uid": uid}, metrics)

    async def keepalive_worker() -> None:
        # Keep sessions active via LIST_ROOMS polling to reduce timeout-driven NOT_FOUND storms.
        while time.monotonic() < end_at:
            sleep_s = max(0.2, keepalive_interval_s)
            await asyncio.sleep(sleep_s)
            await _request_and_record(lobby, {"type": 3}, metrics)

    await asyncio.gather(
        *[worker() for _ in range(concurrency)],
        keepalive_worker(),
    )


async def e2e_short_conn(config: Dict[str, Any], metrics: RunMetrics) -> None:
    duration = int(config["duration_seconds"])
    concurrency = int(config["concurrency"])
    end_at = time.monotonic() + duration
    auth_target = config["targets"]["auth"]
    lobby_target = config["targets"]["lobby"]
    auth = TcpShortClient(auth_target["host"], int(auth_target["port"]))
    lobby = TcpShortClient(lobby_target["host"], int(lobby_target["port"]))
    ctx = ScenarioContext()

    # Pre-create a shared room (register + login required before lobby ops).
    for _ in range(5):
        reg = await _request_and_record(auth, {"type": 1, "uid": ""}, metrics)
        uid = reg.get("data", {}).get("uid", "")
        if int(reg.get("code", -1)) != SUCCESS_CODE or not uid:
            continue
        login = await _request_and_record(auth, {"type": 0, "uid": uid}, metrics)
        if int(login.get("code", -1)) != SUCCESS_CODE:
            continue
        create = await _request_and_record(
            lobby, {"type": 0, "uid": uid, "maximumPeople": 1000000}, metrics
        )
        room_id = room_id_from_response(create)
        if room_id > 0:
            ctx.room_id = room_id
            break

    async def flow_worker() -> None:
        while time.monotonic() < end_at:
            reg = await _request_and_record(auth, {"type": 1, "uid": ""}, metrics)
            uid = reg.get("data", {}).get("uid", "")
            if int(reg.get("code", -1)) != SUCCESS_CODE or not uid:
                metrics.count("flow_fail_register")
                continue
            login = await _request_and_record(auth, {"type": 0, "uid": uid}, metrics)
            if int(login.get("code", -1)) != SUCCESS_CODE:
                metrics.count("flow_fail_login")
                continue

            entered_room = False
            if random.random() < 0.2:
                create = await _request_and_record(
                    lobby, {"type": 0, "uid": uid, "maximumPeople": 16}, metrics
                )
                room_id = room_id_from_response(create)
                if room_id > 0:
                    async with ctx.lock:
                        ctx.room_id = room_id
                    entered_room = True
                else:
                    metrics.count("flow_fail_create_or_join")
            else:
                async with ctx.lock:
                    room_id = ctx.room_id
                if room_id > 0:
                    join = await _request_and_record(
                        lobby, {"type": 1, "roomId": room_id, "uid": uid}, metrics
                    )
                    if int(join.get("code", -1)) == SUCCESS_CODE:
                        entered_room = True
                    else:
                        create = await _request_and_record(
                            lobby, {"type": 0, "uid": uid, "maximumPeople": 16}, metrics
                        )
                        new_room = room_id_from_response(create)
                        if new_room > 0:
                            async with ctx.lock:
                                ctx.room_id = new_room
                            entered_room = True
                        else:
                            metrics.count("flow_fail_create_or_join")
                else:
                    create = await _request_and_record(
                        lobby, {"type": 0, "uid": uid, "maximumPeople": 16}, metrics
                    )
                    new_room = room_id_from_response(create)
                    if new_room > 0:
                        async with ctx.lock:
                            ctx.room_id = new_room
                        entered_room = True
                    else:
                        metrics.count("flow_fail_create_or_join")

            if not entered_room:
                continue

            leave_resp, leave_meta = await lobby.request({"type": 2, "uid": uid})
            leave_ok = int(leave_resp.get("type", -1)) == 2 or int(
                leave_resp.get("code", -1)
            ) == SUCCESS_CODE
            metrics.add(
                response_code=1 if leave_ok else -1,
                connect_ok=leave_meta.get("connect_ok", False),
                send_ok=leave_meta.get("send_ok", False),
                ttfb_ok=leave_meta.get("ttfb_ok", False),
                recv_ok=leave_meta.get("recv_ok", False),
                close_ok=leave_meta.get("close_ok", False),
                connect_ms=float(leave_meta.get("connect_latency_ms", 0)),
                send_ms=float(leave_meta.get("send_latency_ms", 0)),
                ttfb_ms=float(leave_meta.get("ttfb_latency_ms", 0)),
                recv_ms=float(leave_meta.get("recv_latency_ms", 0)),
                e2e_ms=float(leave_meta.get("e2e_latency_ms", 0)),
            )
            if leave_ok:
                metrics.count("flow_success")
            else:
                metrics.count("flow_fail_leave")

    await asyncio.gather(*[flow_worker() for _ in range(concurrency)])


async def _battle_sync_once(
    battle: TcpPersistentClient, uid: str, tick: int, metrics: RunMetrics
) -> None:
    started = time.perf_counter()
    try:
        await battle.send_json(battle_sync_payload(uid, tick))
        msg = await asyncio.wait_for(battle.read_json(), timeout=2.0)
        elapsed_ms = (time.perf_counter() - started) * 1000
        ok = int(msg.get("type", -1)) == 1
        metrics.add(
            response_code=1 if ok else -1,
            connect_ok=True,
            send_ok=True,
            ttfb_ok=ok,
            recv_ok=ok,
            close_ok=True,
            connect_ms=0.0,
            send_ms=0.0,
            ttfb_ms=elapsed_ms,
            recv_ms=elapsed_ms,
            e2e_ms=elapsed_ms,
        )
        if ok:
            metrics.count("battle_frames")
    except Exception:
        elapsed_ms = (time.perf_counter() - started) * 1000
        metrics.add(
            response_code=-1,
            connect_ok=True,
            send_ok=False,
            ttfb_ok=False,
            recv_ok=False,
            close_ok=True,
            connect_ms=0.0,
            send_ms=0.0,
            ttfb_ms=0.0,
            recv_ms=0.0,
            e2e_ms=elapsed_ms,
        )
        metrics.count("battle_sync_fail")


async def battle_sync(config: Dict[str, Any], metrics: RunMetrics) -> None:
    duration = int(config["duration_seconds"])
    players_per_room = int(config.get("players_per_room", 2))
    rooms = int(config.get("rooms", 2))
    if int(config.get("concurrency", 0)) > 0:
        rooms = max(1, int(config["concurrency"]) // max(players_per_room, 1))
    sync_interval = float(config.get("sync_interval_ms", 50)) / 1000.0
    end_at = time.monotonic() + duration

    auth_h, auth_p = target_host(config, "auth", 8765)
    auth = TcpShortClient(auth_h, auth_p)

    battle_sessions: List[tuple[TcpPersistentClient, str]] = []
    for _ in range(rooms):
        uids: List[str] = []
        for _ in range(players_per_room):
            uid = await register_uid(auth)
            if not uid or not await login_uid(auth, uid):
                metrics.count("setup_fail_auth")
                continue
            uids.append(uid)
        if len(uids) < players_per_room:
            continue
        room_id = await create_room(
            TcpShortClient(*target_host(config, "lobby", 8766)), uids[0], players_per_room + 2
        )
        if room_id <= 0:
            metrics.count("setup_fail_room")
            continue
        lobby = TcpShortClient(*target_host(config, "lobby", 8766))
        for uid in uids[1:]:
            if not await join_room(lobby, uid, room_id):
                metrics.count("setup_fail_join")
                break
        else:
            if not await setup_room_to_battle(config, room_id, uids):
                metrics.count("setup_fail_map")
                continue
            clients = await start_battle_for_room(config, uids)
            for client, uid in zip(clients, uids):
                battle_sessions.append((client, uid))
            metrics.count("rooms_started")

    if not battle_sessions:
        return

    async def sync_worker(client: TcpPersistentClient, uid: str) -> None:
        tick = 0
        while time.monotonic() < end_at:
            await _battle_sync_once(client, uid, tick, metrics)
            tick += 1
            await asyncio.sleep(sync_interval)

    await asyncio.gather(*[sync_worker(c, u) for c, u in battle_sessions])
    for client, _ in battle_sessions:
        await client.close()


async def full_flow(config: Dict[str, Any], metrics: RunMetrics) -> None:
    duration = int(config["duration_seconds"])
    concurrency = int(config["concurrency"])
    sync_count = int(config.get("battle_sync_count", 3))
    sync_interval = float(config.get("sync_interval_ms", 50)) / 1000.0
    end_at = time.monotonic() + duration

    auth_h, auth_p = target_host(config, "auth", 8765)
    lobby_h, lobby_p = target_host(config, "lobby", 8766)
    map_h, map_p = target_host(config, "map", 8768)
    battle_h, battle_p = target_host(config, "battle", 8769)

    ctx = ScenarioContext()  # kept for future shared-room scenarios

    async def flow_worker() -> None:
        while time.monotonic() < end_at:
            auth = TcpShortClient(auth_h, auth_p, timeout_seconds=_client_timeout(config))
            uid = await register_uid(auth)
            if not uid or not await login_uid(auth, uid):
                metrics.count("flow_fail_register")
                continue

            lobby = TcpShortClient(lobby_h, lobby_p, timeout_seconds=_client_timeout(config))
            # Solo room avoids multi-player ready/map coordination under concurrency.
            room_id = await create_room(lobby, uid, 1)
            if room_id <= 0:
                metrics.count("flow_fail_create_or_join")
                continue

            if not await lobby_set_ready(lobby, uid):
                metrics.count("flow_fail_ready")
                continue

            map_client = TcpShortClient(map_h, map_p, timeout_seconds=_client_timeout(config))
            resp, _ = await map_client.request({"type": 0, "roomId": room_id, "uid": uid})
            if int(resp.get("type", -1)) != 0:
                metrics.count("flow_fail_map")
                continue
            from flow_helpers import map_first_root

            root_id = map_first_root(resp.get("data", {}).get("map", []))
            if root_id < 0:
                metrics.count("flow_fail_map")
                continue
            resp, meta = await map_client.request({"type": 1, "uid": uid, "selectId": root_id})
            metrics.add(
                response_code=1 if int(resp.get("type", -1)) == 1 else -1,
                connect_ok=meta.get("connect_ok", False),
                send_ok=meta.get("send_ok", False),
                ttfb_ok=meta.get("ttfb_ok", False),
                recv_ok=meta.get("recv_ok", False),
                close_ok=meta.get("close_ok", False),
                connect_ms=float(meta.get("connect_latency_ms", 0)),
                send_ms=float(meta.get("send_latency_ms", 0)),
                ttfb_ms=float(meta.get("ttfb_latency_ms", 0)),
                recv_ms=float(meta.get("recv_latency_ms", 0)),
                e2e_ms=float(meta.get("e2e_latency_ms", 0)),
            )
            if int(resp.get("type", -1)) != 1:
                metrics.count("flow_fail_map")
                continue

            battle = TcpPersistentClient(battle_h, battle_p, timeout_seconds=_client_timeout(config))
            try:
                await battle.connect()
                ready_resp, ready_meta = await battle.request({"type": 0, "uid": uid})
                ok_ready = int(ready_resp.get("type", -1)) in (0, 1) or int(
                    ready_resp.get("code", -1)
                ) == SUCCESS_CODE
                if not ok_ready:
                    metrics.count("flow_fail_battle_ready")
                    continue
                for i in range(sync_count):
                    await _battle_sync_once(battle, uid, i, metrics)
                    await asyncio.sleep(sync_interval)
                metrics.count("flow_success")
            except Exception:
                metrics.count("flow_fail_battle")
            finally:
                await battle.close()

    await asyncio.gather(*[flow_worker() for _ in range(concurrency)])


SCENARIO_REGISTRY = {
    "auth_register_login": auth_register_login,
    "lobby_join_hot_room": lobby_join_hot_room,
    "e2e_short_conn": e2e_short_conn,
    "battle_sync": battle_sync,
    "full_flow": full_flow,
}
