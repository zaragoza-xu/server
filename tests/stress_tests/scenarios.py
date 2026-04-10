import asyncio
import random
import time
from dataclasses import dataclass, field
from typing import Any, Dict, List

from client import TcpShortClient
from metrics import RunMetrics


SUCCESS_CODE = 1


@dataclass
class ScenarioContext:
    uid_pool: List[str] = field(default_factory=list)
    room_id: int = -1
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)


async def _request_and_record(client: TcpShortClient, payload: Dict[str, Any], metrics: RunMetrics) -> Dict[str, Any]:
    started = time.perf_counter()
    try:
        resp, meta = await client.request(payload)
    except Exception:
        elapsed_ms = (time.perf_counter() - started) * 1000
        metrics.add(
            response_code=-1,
            connect_ok=False,
            close_ok=False,
            connect_ms=0.0,
            send_ms=0.0,
            ttfb_ms=0.0,
            recv_ms=0.0,
            e2e_ms=elapsed_ms,
        )
        return {"code": -1, "message": "exception", "data": {}}

    metrics.add(
        response_code=int(resp.get("code", -1)),
        connect_ok=bool(meta.get("connect_ok", False)),
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
    heartbeat_interval_s = float(config.get("heartbeat_interval_seconds", 5.0))
    end_at = time.monotonic() + duration
    auth = TcpShortClient("127.0.0.1", 8765)
    lobby = TcpShortClient(config["target"]["host"], int(config["target"]["port"]))
    ctx = ScenarioContext()

    # Build minimal uid pool and a hotspot room.
    for _ in range(max(20, concurrency // 2)):
        resp = await _request_and_record(auth, {"type": 1, "uid": ""}, metrics)
        uid = resp.get("data", {}).get("uid", "")
        if int(resp.get("code", -1)) == SUCCESS_CODE and uid:
            ctx.uid_pool.append(uid)
    if not ctx.uid_pool:
        return
    creator_uid = ctx.uid_pool[0]
    create_resp = await _request_and_record(
        lobby, {"type": 0, "uid": creator_uid, "maximumPeople": 1000000}, metrics
    )
    ctx.room_id = int(create_resp.get("data", {}).get("roomId", -1))
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

    async def heartbeat_worker() -> None:
        # Keep user sessions alive to avoid heartbeat-timeout driven NOT_FOUND storms.
        while time.monotonic() < end_at:
            sleep_s = max(0.2, heartbeat_interval_s)
            await asyncio.sleep(sleep_s)
            for uid in ctx.uid_pool:
                await _request_and_record(lobby, {"type": 5, "uid": uid}, metrics)

    await asyncio.gather(
        *[worker() for _ in range(concurrency)],
        heartbeat_worker(),
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

            if random.random() < 0.2:
                create = await _request_and_record(
                    lobby, {"type": 0, "uid": uid, "maximumPeople": 16}, metrics
                )
                room_id = int(create.get("data", {}).get("roomId", -1))
                if room_id > 0:
                    async with ctx.lock:
                        ctx.room_id = room_id
                else:
                    metrics.count("flow_fail_create_or_join")
            else:
                async with ctx.lock:
                    room_id = ctx.room_id
                if room_id > 0:
                    join = await _request_and_record(
                        lobby, {"type": 1, "roomId": room_id, "uid": uid}, metrics
                    )
                    if int(join.get("code", -1)) != SUCCESS_CODE:
                        metrics.count("flow_fail_create_or_join")

            leave = await _request_and_record(lobby, {"type": 2, "uid": uid}, metrics)
            if int(leave.get("code", -1)) == SUCCESS_CODE:
                metrics.count("flow_success")
            else:
                metrics.count("flow_fail_leave")

    await asyncio.gather(*[flow_worker() for _ in range(concurrency)])


SCENARIO_REGISTRY = {
    "auth_register_login": auth_register_login,
    "lobby_join_hot_room": lobby_join_hot_room,
    "e2e_short_conn": e2e_short_conn,
}
