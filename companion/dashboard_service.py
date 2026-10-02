#!/usr/bin/env python3
"""Local SQLite-backed companion service for the Xiaozhi dashboard."""

from __future__ import annotations

import argparse
from contextlib import closing
import hmac
import json
import os
import re
import sqlite3
import sys
import threading
import traceback
from dataclasses import dataclass
from datetime import date, datetime, timedelta, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Callable
from urllib.parse import parse_qs, urlsplit

try:
    from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

    try:
        HONG_KONG_TZ = ZoneInfo("Asia/Hong_Kong")
    except ZoneInfoNotFoundError:
        # Windows Python installations do not always ship the IANA database.
        # Hong Kong has used UTC+08:00 year-round since 1979.
        HONG_KONG_TZ = timezone(timedelta(hours=8), "Asia/Hong_Kong")
except ImportError:  # pragma: no cover - Python 3.9+ is documented and supported.
    HONG_KONG_TZ = timezone(timedelta(hours=8), "Asia/Hong_Kong")


DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8765
MAX_BODY_BYTES = 64 * 1024
MAX_TITLE_CHARS = 200
MAX_CONTENT_CHARS = 10_000
MAX_REQUEST_ID_CHARS = 128
DEFAULT_ENTRY_LIMIT = 10
MAX_ENTRY_LIMIT = 100
DEFAULT_CONTENT_LIMIT = 500
MAX_CONTENT_LIMIT = MAX_CONTENT_CHARS
MAX_SQLITE_ID = (1 << 63) - 1
SOCKET_TIMEOUT_SECONDS = 10
DATE_RE = re.compile(r"\d{4}-\d{2}-\d{2}\Z")
TIME_RE = re.compile(r"(?:[01]\d|2[0-3]):[0-5]\d\Z")
ENTRY_FIELDS = ("id", "type", "date", "time", "title", "content")


class ApiError(Exception):
    """Expected client-facing API error."""

    def __init__(self, status: int, code: str, message: str, details: Any = None):
        super().__init__(message)
        self.status = status
        self.code = code
        self.message = message
        self.details = details


def local_today(now: datetime | None = None) -> date:
    """Return the current calendar date in Asia/Hong_Kong."""

    instant = now or datetime.now(timezone.utc)
    if instant.tzinfo is None:
        instant = instant.replace(tzinfo=timezone.utc)
    return instant.astimezone(HONG_KONG_TZ).date()


def validate_date(value: Any, field: str = "date") -> str:
    if not isinstance(value, str) or not DATE_RE.fullmatch(value):
        raise ApiError(400, "invalid_field", f"{field} must use YYYY-MM-DD format", {"field": field})
    try:
        parsed = date.fromisoformat(value)
    except ValueError as exc:
        raise ApiError(400, "invalid_field", f"{field} is not a valid calendar date", {"field": field}) from exc
    if parsed.isoformat() != value:
        raise ApiError(400, "invalid_field", f"{field} must use YYYY-MM-DD format", {"field": field})
    return value


def validate_time(value: Any, field: str = "time") -> str | None:
    if value is None or value == "":
        return None
    if not isinstance(value, str) or not TIME_RE.fullmatch(value):
        raise ApiError(400, "invalid_field", f"{field} must use 24-hour HH:MM format", {"field": field})
    return value


def validate_text(value: Any, field: str, maximum: int, *, required: bool) -> str:
    if value is None and not required:
        return ""
    if not isinstance(value, str):
        raise ApiError(400, "invalid_field", f"{field} must be a string", {"field": field})
    normalized = value.strip() if field in {"title", "request_id"} else value
    if required and not normalized:
        raise ApiError(400, "invalid_field", f"{field} must not be empty", {"field": field})
    if len(normalized) > maximum:
        raise ApiError(
            400,
            "invalid_field",
            f"{field} must be at most {maximum} characters",
            {"field": field, "maximum": maximum},
        )
    return normalized


def validate_type(value: Any) -> str:
    if not isinstance(value, str) or value not in {"schedule", "log"}:
        raise ApiError(400, "invalid_field", "type must be schedule or log", {"field": "type"})
    return value


def validate_unicode(value: Any) -> None:
    """Reject JSON strings that cannot be represented as valid UTF-8."""

    if isinstance(value, str):
        try:
            value.encode("utf-8")
        except UnicodeEncodeError as exc:
            raise ApiError(400, "invalid_unicode", "JSON strings must contain valid Unicode") from exc
        return
    if isinstance(value, list):
        for item in value:
            validate_unicode(item)
        return
    if isinstance(value, dict):
        for key, item in value.items():
            validate_unicode(key)
            validate_unicode(item)


def validate_query_integer(
    values: list[str] | None,
    field: str,
    default: int,
    maximum: int,
) -> int:
    if values is None:
        return default
    if len(values) != 1 or not values[0].isdigit():
        raise ApiError(400, "invalid_query", f"{field} must be supplied once as an integer")
    result = int(values[0])
    if not 1 <= result <= maximum:
        raise ApiError(
            400,
            "invalid_query",
            f"{field} must be between 1 and {maximum}",
            {"field": field, "maximum": maximum},
        )
    return result


def normalize_create(payload: Any) -> dict[str, Any]:
    if not isinstance(payload, dict):
        raise ApiError(400, "invalid_json", "JSON body must be an object")
    allowed = {"type", "date", "time", "title", "content", "request_id"}
    unknown = sorted(set(payload) - allowed)
    if unknown:
        raise ApiError(400, "unknown_field", "JSON body contains unknown fields", {"fields": unknown})
    missing = [field for field in ("type", "date", "title") if field not in payload]
    if missing:
        raise ApiError(400, "missing_field", "Required fields are missing", {"fields": missing})

    request_id = payload.get("request_id")
    if request_id is not None:
        request_id = validate_text(request_id, "request_id", MAX_REQUEST_ID_CHARS, required=True)
    return {
        "type": validate_type(payload["type"]),
        "date": validate_date(payload["date"]),
        "time": validate_time(payload.get("time")),
        "title": validate_text(payload["title"], "title", MAX_TITLE_CHARS, required=True),
        "content": validate_text(payload.get("content"), "content", MAX_CONTENT_CHARS, required=False),
        "request_id": request_id,
    }


def normalize_patch(payload: Any) -> dict[str, Any]:
    if not isinstance(payload, dict):
        raise ApiError(400, "invalid_json", "JSON body must be an object")
    allowed = {"type", "date", "time", "title", "content"}
    unknown = sorted(set(payload) - allowed)
    if unknown:
        raise ApiError(400, "unknown_field", "JSON body contains unknown fields", {"fields": unknown})
    if not payload:
        raise ApiError(400, "empty_patch", "At least one editable field is required")
    normalized: dict[str, Any] = {}
    if "type" in payload:
        normalized["type"] = validate_type(payload["type"])
    if "date" in payload:
        normalized["date"] = validate_date(payload["date"])
    if "time" in payload:
        normalized["time"] = validate_time(payload["time"])
    if "title" in payload:
        normalized["title"] = validate_text(payload["title"], "title", MAX_TITLE_CHARS, required=True)
    if "content" in payload:
        normalized["content"] = validate_text(payload["content"], "content", MAX_CONTENT_CHARS, required=False)
    return normalized


def public_entry(row: sqlite3.Row) -> dict[str, Any]:
    return {field: row[field] for field in ENTRY_FIELDS}


class EntryStore:
    """Small SQLite repository that opens one connection per operation."""

    def __init__(self, path: Path | str):
        self.path = Path(path).expanduser().resolve()
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._initialize_lock = threading.Lock()
        self.initialize()

    def connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self.path, timeout=5.0)
        connection.row_factory = sqlite3.Row
        connection.execute("PRAGMA foreign_keys = ON")
        connection.execute("PRAGMA busy_timeout = 5000")
        return connection

    def initialize(self) -> None:
        with self._initialize_lock, closing(self.connect()) as connection, connection:
            connection.execute("PRAGMA journal_mode = WAL")
            connection.execute("PRAGMA synchronous = NORMAL")
            connection.executescript(
                """
                CREATE TABLE IF NOT EXISTS entries (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    type TEXT NOT NULL CHECK (type IN ('schedule', 'log')),
                    date TEXT NOT NULL,
                    time TEXT,
                    title TEXT NOT NULL,
                    content TEXT NOT NULL DEFAULT '',
                    request_id TEXT UNIQUE,
                    created_at TEXT NOT NULL,
                    updated_at TEXT NOT NULL
                );
                CREATE INDEX IF NOT EXISTS entries_date_type_time_idx
                    ON entries(date, type, time, id);
                """
            )

    @staticmethod
    def _timestamp() -> str:
        return datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")

    def dashboard(self, dashboard_date: str, limit: int, content_limit: int) -> dict[str, Any]:
        with closing(self.connect()) as connection, connection:
            count_rows = connection.execute(
                "SELECT type, COUNT(*) AS count FROM entries WHERE date = ? GROUP BY type",
                (dashboard_date,),
            ).fetchall()
            counts_by_type = {row["type"]: int(row["count"]) for row in count_rows}

            def fetch(entry_type: str) -> tuple[list[dict[str, Any]], list[int]]:
                rows = connection.execute(
                    """
                    SELECT id, type, date, time, title,
                           substr(content, 1, ?) AS content,
                           length(content) > ? AS content_was_truncated
                    FROM entries
                    WHERE date = ? AND type = ?
                    ORDER BY CASE WHEN time IS NULL THEN 1 ELSE 0 END, time, id
                    LIMIT ?
                    """,
                    (content_limit, content_limit, dashboard_date, entry_type, limit),
                ).fetchall()
                entries = [public_entry(row) for row in rows]
                content_ids = [int(row["id"]) for row in rows if row["content_was_truncated"]]
                return entries, content_ids

            schedules, schedule_content_ids = fetch("schedule")
            work_logs, log_content_ids = fetch("log")

        schedule_count = counts_by_type.get("schedule", 0)
        log_count = counts_by_type.get("log", 0)
        return {
            "date": dashboard_date,
            "schedules": schedules,
            "work_logs": work_logs,
            "counts": {"schedules": schedule_count, "work_logs": log_count},
            "truncated": {
                "schedules": schedule_count > len(schedules),
                "work_logs": log_count > len(work_logs),
                "content_ids": schedule_content_ids + log_content_ids,
            },
        }

    def get(self, entry_id: int, connection: sqlite3.Connection | None = None) -> sqlite3.Row | None:
        if connection is not None:
            return connection.execute(
                "SELECT id, type, date, time, title, content, request_id FROM entries WHERE id = ?",
                (entry_id,),
            ).fetchone()
        with closing(self.connect()) as owned_connection, owned_connection:
            return self.get(entry_id, owned_connection)

    @staticmethod
    def _same_create(row: sqlite3.Row, values: dict[str, Any]) -> bool:
        return all(row[field] == values[field] for field in ("type", "date", "time", "title", "content"))

    def create(self, values: dict[str, Any]) -> tuple[dict[str, Any], bool]:
        timestamp = self._timestamp()
        with closing(self.connect()) as connection, connection:
            if values["request_id"] is not None:
                existing = connection.execute(
                    "SELECT id, type, date, time, title, content, request_id FROM entries WHERE request_id = ?",
                    (values["request_id"],),
                ).fetchone()
                if existing is not None:
                    if not self._same_create(existing, values):
                        raise ApiError(409, "idempotency_conflict", "request_id was already used with different entry data")
                    return public_entry(existing), True
            try:
                cursor = connection.execute(
                    """
                    INSERT INTO entries(type, date, time, title, content, request_id, created_at, updated_at)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                    """,
                    (
                        values["type"],
                        values["date"],
                        values["time"],
                        values["title"],
                        values["content"],
                        values["request_id"],
                        timestamp,
                        timestamp,
                    ),
                )
            except sqlite3.IntegrityError:
                # A concurrent retry may win the UNIQUE(request_id) race.
                if values["request_id"] is None:
                    raise
                existing = connection.execute(
                    "SELECT id, type, date, time, title, content, request_id FROM entries WHERE request_id = ?",
                    (values["request_id"],),
                ).fetchone()
                if existing is None or not self._same_create(existing, values):
                    raise ApiError(409, "idempotency_conflict", "request_id was already used with different entry data")
                return public_entry(existing), True
            row = self.get(int(cursor.lastrowid), connection)
            assert row is not None
            return public_entry(row), False

    def update(self, entry_id: int, values: dict[str, Any]) -> dict[str, Any]:
        assignments = ", ".join(f"{field} = ?" for field in values)
        parameters = list(values.values()) + [self._timestamp(), entry_id]
        with closing(self.connect()) as connection, connection:
            cursor = connection.execute(
                f"UPDATE entries SET {assignments}, updated_at = ? WHERE id = ?",  # field names come from allowlist
                parameters,
            )
            if cursor.rowcount == 0:
                raise ApiError(404, "not_found", "Entry was not found")
            row = self.get(entry_id, connection)
            assert row is not None
            return public_entry(row)

    def delete(self, entry_id: int) -> None:
        with closing(self.connect()) as connection, connection:
            cursor = connection.execute("DELETE FROM entries WHERE id = ?", (entry_id,))
            if cursor.rowcount == 0:
                raise ApiError(404, "not_found", "Entry was not found")


@dataclass(frozen=True)
class DashboardApp:
    store: EntryStore
    token: str | None = None
    today_provider: Callable[[], date] = local_today
    web_root: Path = Path(__file__).resolve().parent / "web"


class DashboardServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, server_address: tuple[str, int], app: DashboardApp):
        self.app = app
        super().__init__(server_address, DashboardRequestHandler)


class DashboardRequestHandler(BaseHTTPRequestHandler):
    server: DashboardServer
    protocol_version = "HTTP/1.1"

    def setup(self) -> None:
        super().setup()
        self.connection.settimeout(SOCKET_TIMEOUT_SECONDS)

    def log_message(self, format_string: str, *args: Any) -> None:
        sys.stderr.write(
            "%s - - [%s] %s\n"
            % (self.client_address[0], self.log_date_time_string(), format_string % args)
        )

    def _send_bytes(self, status: int, body: bytes, content_type: str, extra_headers: dict[str, str] | None = None) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Cache-Control", "no-store")
        if extra_headers:
            for name, value in extra_headers.items():
                self.send_header(name, value)
        if self.close_connection:
            self.send_header("Connection", "close")
        self.end_headers()
        if self.command != "HEAD" and body:
            self.wfile.write(body)

    def _send_json(self, status: int, payload: Any) -> None:
        body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self._send_bytes(status, body, "application/json; charset=utf-8")

    def _send_error(self, error: ApiError) -> None:
        payload: dict[str, Any] = {"error": {"code": error.code, "message": error.message}}
        if error.details is not None:
            payload["error"]["details"] = error.details
        headers = {"WWW-Authenticate": 'Bearer realm="xiaozhi-dashboard"'} if error.status == 401 else None
        body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self._send_bytes(error.status, body, "application/json; charset=utf-8", headers)

    def _authorize_api(self) -> None:
        expected = self.server.app.token
        if expected is None:
            return
        authorization = self.headers.get("Authorization", "")
        prefix = "Bearer "
        supplied = authorization[len(prefix) :] if authorization.startswith(prefix) else ""
        try:
            matches = bool(supplied) and hmac.compare_digest(supplied.encode("utf-8"), expected.encode("utf-8"))
        except UnicodeEncodeError:
            matches = False
        if not matches:
            # A rejected POST/PATCH body has not been consumed, so this HTTP/1.1
            # connection must not be reused for another request.
            self.close_connection = True
            raise ApiError(401, "unauthorized", "A valid bearer token is required")

    def _read_json(self) -> Any:
        content_type = self.headers.get("Content-Type", "").split(";", 1)[0].strip().lower()
        if content_type != "application/json":
            raise ApiError(415, "unsupported_media_type", "Content-Type must be application/json")
        raw_length = self.headers.get("Content-Length")
        if raw_length is None:
            raise ApiError(411, "length_required", "Content-Length is required")
        try:
            length = int(raw_length)
        except ValueError as exc:
            raise ApiError(400, "invalid_content_length", "Content-Length must be an integer") from exc
        if length < 0:
            raise ApiError(400, "invalid_content_length", "Content-Length must not be negative")
        if length > MAX_BODY_BYTES:
            # Do not leave an unread oversized body on a reusable HTTP/1.1 connection.
            self.close_connection = True
            raise ApiError(413, "body_too_large", f"JSON body must not exceed {MAX_BODY_BYTES} bytes")
        try:
            raw_body = self.rfile.read(length)
        except TimeoutError as exc:
            self.close_connection = True
            raise ApiError(408, "request_timeout", "Timed out while reading the JSON body") from exc
        if len(raw_body) != length:
            self.close_connection = True
            raise ApiError(400, "incomplete_body", "Request body ended before Content-Length bytes were received")
        try:
            payload = json.loads(raw_body.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise ApiError(400, "invalid_json", "Body must contain valid UTF-8 JSON") from exc
        validate_unicode(payload)
        return payload

    @staticmethod
    def _entry_id(path: str) -> int | None:
        match = re.fullmatch(r"/api/entries/(\d+)", path)
        if match is None:
            return None
        digits = match.group(1)
        if len(digits) > 19:
            raise ApiError(400, "invalid_entry_id", "Entry id is outside the supported range")
        value = int(digits)
        if not 1 <= value <= MAX_SQLITE_ID:
            raise ApiError(400, "invalid_entry_id", "Entry id is outside the supported range")
        return value

    def _handle(self) -> None:
        parsed = urlsplit(self.path)
        path = parsed.path
        if path.startswith("/api/"):
            self._authorize_api()

        if self.command in {"GET", "HEAD"} and path == "/":
            index_path = self.server.app.web_root / "index.html"
            try:
                body = index_path.read_bytes()
            except OSError as exc:
                raise ApiError(500, "ui_unavailable", "Dashboard UI file is unavailable") from exc
            self._send_bytes(200, body, "text/html; charset=utf-8")
            return

        if self.command in {"GET", "HEAD"} and path == "/api/health":
            self._send_json(200, {"status": "ok"})
            return

        if self.command in {"GET", "HEAD"} and path == "/api/dashboard":
            query = parse_qs(parsed.query, keep_blank_values=True)
            unknown = sorted(set(query) - {"date", "limit", "content_limit"})
            if unknown:
                raise ApiError(400, "unknown_query", "Query contains unknown parameters", {"parameters": unknown})
            date_values = query.get("date")
            if date_values is None:
                dashboard_date = self.server.app.today_provider().isoformat()
            elif len(date_values) != 1:
                raise ApiError(400, "invalid_query", "date must be supplied once")
            else:
                dashboard_date = validate_date(date_values[0])
            limit = validate_query_integer(query.get("limit"), "limit", DEFAULT_ENTRY_LIMIT, MAX_ENTRY_LIMIT)
            content_limit = validate_query_integer(
                query.get("content_limit"),
                "content_limit",
                DEFAULT_CONTENT_LIMIT,
                MAX_CONTENT_LIMIT,
            )
            self._send_json(200, self.server.app.store.dashboard(dashboard_date, limit, content_limit))
            return

        if self.command == "POST" and path == "/api/entries":
            values = normalize_create(self._read_json())
            entry, idempotent = self.server.app.store.create(values)
            self._send_json(200 if idempotent else 201, {"entry": entry, "idempotent": idempotent})
            return

        entry_id = self._entry_id(path)
        if self.command == "PATCH" and entry_id is not None:
            values = normalize_patch(self._read_json())
            entry = self.server.app.store.update(entry_id, values)
            self._send_json(200, {"entry": entry})
            return

        if self.command == "DELETE" and entry_id is not None:
            self.server.app.store.delete(entry_id)
            self._send_bytes(204, b"", "application/json; charset=utf-8")
            return

        raise ApiError(404, "not_found", "Route was not found")

    def _dispatch(self) -> None:
        try:
            self._handle()
        except ApiError as error:
            self._send_error(error)
        except (BrokenPipeError, ConnectionResetError):
            return
        except Exception:
            traceback.print_exc(file=sys.stderr)
            self._send_error(ApiError(500, "internal_error", "The server could not complete the request"))

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler method name
        self._dispatch()

    def do_HEAD(self) -> None:  # noqa: N802
        self._dispatch()

    def do_POST(self) -> None:  # noqa: N802
        self._dispatch()

    def do_PATCH(self) -> None:  # noqa: N802
        self._dispatch()

    def do_DELETE(self) -> None:  # noqa: N802
        self._dispatch()


def make_server(
    host: str,
    port: int,
    database: Path | str,
    token: str | None = None,
    *,
    today_provider: Callable[[], date] = local_today,
) -> DashboardServer:
    normalized_token = token if token else None
    return DashboardServer((host, port), DashboardApp(EntryStore(database), normalized_token, today_provider))


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    companion_root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description="Xiaozhi local schedule and work-log service")
    parser.add_argument("--host", default=os.environ.get("DASHBOARD_HOST", DEFAULT_HOST), help="listen address")
    parser.add_argument(
        "--port",
        type=int,
        default=int(os.environ.get("DASHBOARD_PORT", str(DEFAULT_PORT))),
        help="listen port (0-65535)",
    )
    parser.add_argument(
        "--db",
        type=Path,
        default=Path(os.environ.get("DASHBOARD_DB", companion_root / "data" / "dashboard.db")),
        help="SQLite database path",
    )
    parser.add_argument(
        "--token",
        default=os.environ.get("DASHBOARD_TOKEN"),
        help="Bearer token; DASHBOARD_TOKEN is safer than command history",
    )
    args = parser.parse_args(argv)
    if not 0 <= args.port <= 65535:
        parser.error("--port must be between 0 and 65535")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    server = make_server(args.host, args.port, args.db, args.token)
    host, port = server.server_address[:2]
    print(f"Xiaozhi Dashboard companion listening on http://{host}:{port}")
    print(f"Database: {Path(args.db).expanduser().resolve()}")
    if not args.token:
        print("Authentication: disabled")
        if args.host not in {"127.0.0.1", "::1", "localhost"}:
            print("WARNING: service is reachable beyond this computer without a token", file=sys.stderr)
    else:
        print("Authentication: bearer token enabled")
    try:
        server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        print("\nStopping companion service")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
