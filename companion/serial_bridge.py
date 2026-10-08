"""Bounded JSON RPC over the board's USB-to-UART port (no Wi-Fi needed)."""
from __future__ import annotations

import json
import re
import sys
import threading
import time
from collections.abc import Callable
from typing import Any

REQUEST_PREFIX = b"DASHBOARD_RPC "
RESPONSE_PREFIX = b"dashboard_rpc "
MAX_FRAME_BYTES = 16 * 1024
ID_PATTERN = re.compile(r"[0-9a-f]{16}\Z")


class LineDecoder:
    """Retain partial reads, discard oversized lines through their newline."""

    def __init__(self) -> None:
        self.pending = bytearray()
        self.discarding = False

    def feed(self, data: bytes) -> list[bytes]:
        lines: list[bytes] = []
        for byte in data:
            if byte == 13:
                continue
            if byte == 10:
                if not self.discarding and self.pending:
                    lines.append(bytes(self.pending))
                self.pending.clear()
                self.discarding = False
            elif not self.discarding:
                if len(self.pending) >= MAX_FRAME_BYTES:
                    self.pending.clear()
                    self.discarding = True
                else:
                    self.pending.append(byte)
        return lines


def process_request(
    line: bytes,
    handler: Callable[[str, dict[str, Any]], tuple[int, dict[str, Any]]],
    *,
    now: Callable[[], float] = time.time,
) -> bytes | None:
    if not line.startswith(REQUEST_PREFIX) or len(line) > MAX_FRAME_BYTES:
        return None
    try:
        request = json.loads(line[len(REQUEST_PREFIX):].decode("utf-8"))
    except (UnicodeDecodeError, ValueError, RecursionError):
        return None
    if (
        not isinstance(request, dict)
        or type(request.get("v")) is not int
        or request["v"] != 1
        or not isinstance(request.get("id"), str)
        or not ID_PATTERN.fullmatch(request["id"])
        or not isinstance(request.get("method"), str)
        or request.get("method") not in {"dashboard.get", "entries.add"}
        or not isinstance(request.get("body"), dict)
    ):
        return None
    try:
        status, body = handler(request["method"], request["body"])
    except Exception:
        # Never put request contents, credentials, or DB exception text on UART.
        status, body = 500, {"error": "service_error"}
    response = {
        "v": 1,
        "id": request["id"],
        "status": status,
        "body": body,
        # Match official firmware's shifted-epoch wall-clock convention.
        "local_timestamp": int(now()) + 8 * 60 * 60,
    }
    encoded = RESPONSE_PREFIX + json.dumps(
        response, ensure_ascii=True, separators=(",", ":"), allow_nan=False
    ).encode("ascii")
    if len(encoded) > MAX_FRAME_BYTES:
        response.update(status=413, body={"error": "response_too_large"})
        encoded = RESPONSE_PREFIX + json.dumps(response, separators=(",", ":")).encode("ascii")
    return encoded + b"\n"


class SerialBridge:
    def __init__(self, port: str, handler: Callable, *, baud: int = 115200) -> None:
        # Import only for USB mode; the HTTP-only service remains stdlib-only.
        try:
            import serial
        except ImportError as error:
            raise RuntimeError("USB mode needs pyserial: python -m pip install pyserial") from error
        self.serial = serial
        self.port = port
        self.baud = baud
        self.handler = handler
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self._run, name="dashboard-usb", daemon=True)

    def start(self) -> None:
        self.thread.start()

    def stop(self) -> None:
        self.stop_event.set()
        self.thread.join(timeout=3)

    def _run(self) -> None:
        failed = False
        while not self.stop_event.is_set():
            try:
                device = self.serial.Serial()
                device.port = self.port
                device.baudrate = self.baud
                device.timeout = 0.25
                device.write_timeout = 2
                # Set lines before opening: do not deliberately reset the board.
                device.dtr = False
                device.rts = False
                with device:
                    print(f"USB bridge connected: {self.port}", flush=True)
                    failed = False
                    decoder = LineDecoder()
                    next_hello = 0.0
                    first_reply = True
                    while not self.stop_event.is_set():
                        if time.monotonic() >= next_hello:
                            device.write(b"dashboard_usb hello\n")
                            next_hello = time.monotonic() + 5
                        chunk = device.read(min(device.in_waiting or 1, 1024))
                        for line in decoder.feed(chunk):
                            reply = process_request(line, self.handler)
                            if reply is not None:
                                device.write(reply)
                                device.flush()
                                if first_reply:
                                    status = json.loads(reply[len(RESPONSE_PREFIX):])["status"]
                                    print(f"USB device request answered: status {status}", flush=True)
                                    first_reply = False
            except (self.serial.SerialException, OSError):
                if not failed:
                    print(f"USB bridge waiting for {self.port}; close other serial monitors if in use", file=sys.stderr, flush=True)
                    failed = True
                self.stop_event.wait(2)
