"""Shared helpers for multi-port game flow setup (auth -> battle)."""

from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple

from client import TcpPersistentClient, TcpShortClient

SUCCESS_CODE = 1
BATTLE_FRAME = 1
BATTLE_WAIT = 0
BATTLE_START_PUSH = 0


def target_host(config: Dict[str, Any], key: str, default_port: int) -> Tuple[str, int]:
    targets = config.get("targets", {})
    if key in targets:
        t = targets[key]
        return t["host"], int(t["port"])
    if key == "auth" and "auth_target" in config:
        t = config["auth_target"]
        return t["host"], int(t["port"])
    single = config.get("target", {})
    host = single.get("host", "127.0.0.1")
    return host, default_port


async def register_uid(auth: TcpShortClient) -> Optional[str]:
    resp, _ = await auth.request({"type": 1, "uid": ""})
    if int(resp.get("code", -1)) != SUCCESS_CODE:
        return None
    uid = resp.get("data", {}).get("uid", "")
    return uid or None


async def login_uid(auth: TcpShortClient, uid: str) -> bool:
    resp, _ = await auth.request({"type": 0, "uid": uid})
    return int(resp.get("code", -1)) == SUCCESS_CODE


def map_first_root(map_arr: List[Dict[str, Any]]) -> int:
    incoming = set()
    for node in map_arr:
        for nid in node.get("nextId", []) or []:
            incoming.add(int(nid))
    for node in map_arr:
        node_id = int(node["nodeId"])
        if node_id not in incoming:
            return node_id
    return -1


async def lobby_set_ready(lobby: TcpShortClient, uid: str) -> bool:
    resp, _ = await lobby.request({"type": 7, "uid": uid, "ready": True})
    return int(resp.get("type", -1)) == 8


async def create_room(lobby: TcpShortClient, uid: str, max_people: int) -> int:
    resp, _ = await lobby.request({"type": 0, "uid": uid, "maximumPeople": max_people})
    if int(resp.get("code", -1)) != SUCCESS_CODE:
        return -1
    return int(resp.get("data", {}).get("roomInfo", {}).get("roomId", -1))


async def join_room(lobby: TcpShortClient, uid: str, room_id: int) -> bool:
    resp, _ = await lobby.request({"type": 1, "roomId": room_id, "uid": uid})
    return int(resp.get("code", -1)) == SUCCESS_CODE


async def enter_map_phase(
    map_client: TcpShortClient, room_id: int, uids: List[str]
) -> bool:
    root_id = -1
    for uid in uids:
        resp, _ = await map_client.request({"type": 0, "roomId": room_id, "uid": uid})
        if int(resp.get("type", -1)) != 0:
            return False
        if root_id < 0:
            root_id = map_first_root(resp.get("data", {}).get("map", []))
    if root_id < 0:
        return False
    for uid in uids:
        resp, _ = await map_client.request({"type": 1, "uid": uid, "selectId": root_id})
        if int(resp.get("type", -1)) != 1:
            return False
    return True


async def setup_room_to_battle(
    config: Dict[str, Any],
    room_id: int,
    uids: List[str],
) -> bool:
    lobby_h, lobby_p = target_host(config, "lobby", 8766)
    map_h, map_p = target_host(config, "map", 8768)
    lobby = TcpShortClient(lobby_h, lobby_p)
    for uid in uids:
        if not await lobby_set_ready(lobby, uid):
            return False
    map_client = TcpShortClient(map_h, map_p)
    return await enter_map_phase(map_client, room_id, uids)


async def start_battle_for_room(
    config: Dict[str, Any], uids: List[str]
) -> List[TcpPersistentClient]:
    battle_h, battle_p = target_host(config, "battle", 8769)
    clients: List[TcpPersistentClient] = []
    for uid in uids:
        battle = TcpPersistentClient(battle_h, battle_p)
        await battle.connect()
        resp, _ = await battle.request({"type": 0, "uid": uid})
        rtype = int(resp.get("type", -1))
        if rtype == BATTLE_WAIT:
            clients.append(battle)
            continue
        if rtype == BATTLE_FRAME:
            clients.append(battle)
            continue
        if int(resp.get("code", -1)) != SUCCESS_CODE:
            await battle.close()
            continue
        clients.append(battle)
    # Last ready triggers start; earlier clients may need to read start frame
    for battle in clients:
        await wait_battle_start(battle, max_reads=5)
    return clients


async def wait_battle_start(battle: TcpPersistentClient, max_reads: int = 20) -> bool:
    for _ in range(max_reads):
        msg = await battle.read_json()
        if int(msg.get("type", -1)) == BATTLE_FRAME:
            return True
        if int(msg.get("type", -1)) == BATTLE_WAIT:
            continue
    return False


def battle_sync_payload(uid: str, tick: int = 0) -> Dict[str, Any]:
    return {
        "type": 1,
        "uid": uid,
        "playerPosition": {"x": 1.0 + tick * 0.01, "y": 0.0},
        "playerDirection": {"x": 1.0, "y": 0.0},
        "enemyPositions": [],
    }
