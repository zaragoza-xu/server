import asyncio
import json
import time
from typing import Any, Dict, Optional, Tuple


class TcpShortClient:
    def __init__(self, host: str, port: int, timeout_seconds: float = 3.0) -> None:
        self.host = host
        self.port = port
        self.timeout_seconds = timeout_seconds

    async def request(self, payload: Dict[str, Any]) -> Tuple[Dict[str, Any], Dict[str, Any]]:
        connect_start = time.perf_counter()
        reader = None
        writer = None
        meta = {
            "connect_ok": False,
            "connect_latency_ms": 0.0,
            "send_ok": False,
            "send_latency_ms": 0.0,
            "ttfb_ok": False,
            "ttfb_latency_ms": 0.0,
            "recv_ok": False,
            "recv_latency_ms": 0.0,
            "e2e_latency_ms": 0.0,
            "close_ok": False,
        }
        request_start = time.perf_counter()
        try:
            reader, writer = await asyncio.wait_for(
                asyncio.open_connection(self.host, self.port),
                timeout=self.timeout_seconds,
            )
            meta["connect_ok"] = True
            meta["connect_latency_ms"] = (time.perf_counter() - connect_start) * 1000

            frame = json.dumps(payload, ensure_ascii=False) + "\n"
            send_start = time.perf_counter()
            writer.write(frame.encode("utf-8"))
            await asyncio.wait_for(writer.drain(), timeout=self.timeout_seconds)
            send_done = time.perf_counter()
            meta["send_ok"] = True
            meta["send_latency_ms"] = (send_done - send_start) * 1000

            recv_start = time.perf_counter()
            line = await asyncio.wait_for(reader.readline(), timeout=self.timeout_seconds)
            recv_done = time.perf_counter()
            meta["ttfb_ok"] = True
            meta["recv_ok"] = True
            meta["ttfb_latency_ms"] = (recv_done - send_done) * 1000
            meta["recv_latency_ms"] = (recv_done - recv_start) * 1000
            meta["e2e_latency_ms"] = (recv_done - request_start) * 1000
            if not line:
                return {"code": -1, "message": "empty response", "data": {}}, meta
            response = json.loads(line.decode("utf-8").strip())
            return response, meta
        finally:
            if writer is not None:
                try:
                    writer.close()
                    await writer.wait_closed()
                    meta["close_ok"] = True
                except Exception:
                    meta["close_ok"] = False


class TcpPersistentClient:
    """Long-lived TCP JSON line client for battle/map push streams."""

    def __init__(self, host: str, port: int, timeout_seconds: float = 5.0) -> None:
        self.host = host
        self.port = port
        self.timeout_seconds = timeout_seconds
        self._reader: Optional[asyncio.StreamReader] = None
        self._writer: Optional[asyncio.StreamWriter] = None

    async def connect(self) -> float:
        start = time.perf_counter()
        self._reader, self._writer = await asyncio.wait_for(
            asyncio.open_connection(self.host, self.port),
            timeout=self.timeout_seconds,
        )
        return (time.perf_counter() - start) * 1000

    async def close(self) -> bool:
        if self._writer is None:
            return True
        try:
            self._writer.close()
            await self._writer.wait_closed()
            return True
        except Exception:
            return False
        finally:
            self._reader = None
            self._writer = None

    async def send_json(self, payload: Dict[str, Any]) -> None:
        if self._writer is None:
            raise RuntimeError("not connected")
        frame = json.dumps(payload, ensure_ascii=False) + "\n"
        self._writer.write(frame.encode("utf-8"))
        await asyncio.wait_for(self._writer.drain(), timeout=self.timeout_seconds)

    async def read_json(self) -> Dict[str, Any]:
        if self._reader is None:
            raise RuntimeError("not connected")
        line = await asyncio.wait_for(self._reader.readline(), timeout=self.timeout_seconds)
        if not line:
            return {"code": -1, "message": "empty response"}
        return json.loads(line.decode("utf-8").strip())

    async def request(self, payload: Dict[str, Any]) -> Tuple[Dict[str, Any], Dict[str, Any]]:
        """Send one message and read one line response with timing meta."""
        request_start = time.perf_counter()
        connect_ms = 0.0
        if self._writer is None:
            connect_ms = await self.connect()
        meta = {
            "connect_ok": True,
            "connect_latency_ms": connect_ms,
            "send_ok": False,
            "send_latency_ms": 0.0,
            "ttfb_ok": False,
            "ttfb_latency_ms": 0.0,
            "recv_ok": False,
            "recv_latency_ms": 0.0,
            "e2e_latency_ms": 0.0,
            "close_ok": True,
        }
        send_start = time.perf_counter()
        await self.send_json(payload)
        send_done = time.perf_counter()
        meta["send_ok"] = True
        meta["send_latency_ms"] = (send_done - send_start) * 1000
        recv_start = time.perf_counter()
        resp = await self.read_json()
        recv_done = time.perf_counter()
        meta["ttfb_ok"] = True
        meta["recv_ok"] = True
        meta["ttfb_latency_ms"] = (recv_done - send_done) * 1000
        meta["recv_latency_ms"] = (recv_done - recv_start) * 1000
        meta["e2e_latency_ms"] = (recv_done - request_start) * 1000
        return resp, meta
