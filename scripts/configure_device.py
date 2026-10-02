#!/usr/bin/env python3
"""Configure the Dashboard service endpoint over the ESP32 serial console."""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from collections.abc import Sequence
from urllib.parse import urlsplit


RESPONSE_PREFIX = "DASHBOARD_CONFIG "


class ConfigurationError(RuntimeError):
    """Raised when configuration could not be safely applied or verified."""


def positive_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("value must be an integer") from error
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return parsed


def validate_url(value: str) -> str:
    if len(value.encode("utf-8")) > 192:
        raise argparse.ArgumentTypeError("URL must be at most 192 UTF-8 bytes")
    if any(character.isspace() for character in value):
        raise argparse.ArgumentTypeError("URL must not contain whitespace")
    parsed = urlsplit(value)
    if parsed.scheme not in {"http", "https"} or not parsed.hostname:
        raise argparse.ArgumentTypeError("URL must be an absolute http:// or https:// URL")
    if parsed.username or parsed.password or parsed.query or parsed.fragment:
        raise argparse.ArgumentTypeError(
            "URL must not contain credentials, a query, or a fragment"
        )
    return value.rstrip("/")


def parse_response(line: bytes) -> dict | None:
    text = line.decode("utf-8", errors="replace").strip()
    if not text.startswith(RESPONSE_PREFIX):
        return None
    try:
        payload = json.loads(text[len(RESPONSE_PREFIX) :])
    except json.JSONDecodeError as error:
        raise ConfigurationError("device returned malformed configuration JSON") from error
    if not isinstance(payload, dict) or not isinstance(payload.get("ok"), bool):
        raise ConfigurationError("device returned an invalid configuration response")
    return payload


def wait_for_response(serial_port, timeout: float) -> dict:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        payload = parse_response(serial_port.readline())
        if payload is not None:
            return payload
    raise ConfigurationError("timed out waiting for the device configuration response")


def _write_command(serial_port, command: str) -> None:
    encoded = (command + "\r\n").encode("utf-8")
    if len(encoded) >= 512:
        raise ConfigurationError("serial configuration command is too long")
    serial_port.write(encoded)
    serial_port.flush()


def _send(serial_port, command: str, timeout: float) -> dict:
    _write_command(serial_port, command)
    payload = wait_for_response(serial_port, timeout)
    if not payload["ok"]:
        error = payload.get("error", "unknown")
        raise ConfigurationError(f"device rejected the configuration ({error})")
    return payload


def _wait_until_ready(
    serial_port,
    deadline: float,
    *,
    probe_timeout: float,
) -> None:
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise ConfigurationError("timed out waiting for the device serial console")
        _write_command(serial_port, "dashboard_config show")
        try:
            payload = wait_for_response(serial_port, min(probe_timeout, remaining))
        except ConfigurationError as error:
            if "timed out" in str(error):
                continue
            raise
        if not payload["ok"]:
            error = payload.get("error", "unknown")
            raise ConfigurationError(f"device rejected the readiness probe ({error})")
        return


def configure(
    serial_port,
    url: str,
    token: str | None,
    timeout: float,
    *,
    probe_timeout: float = 1.5,
) -> dict:
    try:
        url = validate_url(url)
    except argparse.ArgumentTypeError as error:
        raise ConfigurationError(str(error)) from error
    if token is not None and (
        not token
        or token == "-"
        or len(token.encode("utf-8")) > 192
        or any(character.isspace() for character in token)
    ):
        raise ConfigurationError(
            "token must be non-empty, not '-', at most 192 UTF-8 bytes, "
            "and contain no whitespace"
        )
    if probe_timeout <= 0:
        raise ConfigurationError("serial readiness probe timeout must be positive")
    deadline = time.monotonic() + timeout
    _wait_until_ready(serial_port, deadline, probe_timeout=probe_timeout)

    token_argument = token if token is not None else "-"
    # The token is deliberately never printed or included in an exception.
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise ConfigurationError("timed out before the device configuration could be sent")
    _send(serial_port, f"dashboard_config set {url} {token_argument}", remaining)
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise ConfigurationError("timed out before the device configuration could be verified")
    confirmed = _send(serial_port, "dashboard_config show", remaining)
    if confirmed.get("url") != url:
        raise ConfigurationError("device verification returned a different URL")
    if confirmed.get("token_set") is not (token is not None):
        raise ConfigurationError("device verification returned an unexpected token state")
    return confirmed


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="serial port, for example COM7")
    parser.add_argument("--url", required=True, type=validate_url)
    token = parser.add_mutually_exclusive_group(required=True)
    token.add_argument(
        "--token-env",
        metavar="NAME",
        help="read the Dashboard token from this environment variable",
    )
    token.add_argument(
        "--clear-token",
        action="store_true",
        help="configure the service without an authentication token",
    )
    parser.add_argument("--baud", type=positive_int, default=115200)
    parser.add_argument("--timeout", type=float, default=30.0)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    if args.timeout <= 0:
        print("error: --timeout must be positive", file=sys.stderr)
        return 2
    token = None
    if args.token_env:
        token = os.environ.get(args.token_env)
        if not token:
            print(
                f"error: environment variable {args.token_env} is missing or empty",
                file=sys.stderr,
            )
            return 2

    try:
        import serial
    except ImportError:
        print(
            "error: pyserial is unavailable; activate the ESP-IDF environment first",
            file=sys.stderr,
        )
        return 2

    try:
        with serial.Serial(
            args.port,
            args.baud,
            timeout=0.25,
            write_timeout=args.timeout,
        ) as serial_port:
            # Opening USB Serial/JTAG can reset the board; allow its console to start.
            time.sleep(1.5)
            serial_port.reset_input_buffer()
            result = configure(serial_port, args.url, token, args.timeout)
    except (OSError, serial.SerialException, ConfigurationError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    token_state = "set" if result["token_set"] else "clear"
    print(f"Dashboard endpoint configured: {result['url']} (token {token_state})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
