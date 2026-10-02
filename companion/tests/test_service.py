from __future__ import annotations

import json
import sys
import threading
import unittest
import uuid
from datetime import date, datetime, timezone
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen

COMPANION_ROOT = Path(__file__).resolve().parents[1]
if str(COMPANION_ROOT) not in sys.path:
    sys.path.insert(0, str(COMPANION_ROOT))

from dashboard_service import local_today, make_server


class RunningService:
    def __init__(self, database: Path, token: str | None = None, today: date = date(2026, 10, 2)):
        self.server = make_server("127.0.0.1", 0, database, token, today_provider=lambda: today)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    @property
    def url(self) -> str:
        return f"http://127.0.0.1:{self.server.server_address[1]}"

    def __enter__(self) -> "RunningService":
        self.thread.start()
        return self

    def __exit__(self, exc_type, exc, traceback) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)

    def request(
        self,
        method: str,
        path: str,
        payload=None,
        token: str | None = None,
        raw_body: bytes | None = None,
        content_type: str = "application/json",
    ) -> tuple[int, dict | None]:
        data = raw_body if raw_body is not None else (json.dumps(payload).encode() if payload is not None else None)
        headers = {}
        if data is not None:
            headers["Content-Type"] = content_type
        if token is not None:
            headers["Authorization"] = f"Bearer {token}"
        request = Request(self.url + path, data=data, headers=headers, method=method)
        try:
            with urlopen(request, timeout=3) as response:
                body = response.read()
                return response.status, json.loads(body) if body else None
        except HTTPError as error:
            body = error.read()
            return error.code, json.loads(body) if body else None


class CompanionHttpTests(unittest.TestCase):
    def setUp(self) -> None:
        # Use a unique file in an existing writable directory. Some restricted
        # Windows profiles apply unusable ACLs to newly-created temp folders.
        self.database = Path(__file__).resolve().parent / f".test-dashboard-{uuid.uuid4().hex}.db"

    def tearDown(self) -> None:
        for suffix in ("", "-shm", "-wal"):
            Path(str(self.database) + suffix).unlink(missing_ok=True)

    def test_crud_and_dashboard_shape(self) -> None:
        with RunningService(self.database) as service:
            status, created = service.request(
                "POST",
                "/api/entries",
                {"type": "schedule", "date": "2026-10-02", "time": "15:00", "title": "Mentor meeting"},
            )
            self.assertEqual(status, 201)
            self.assertFalse(created["idempotent"])
            entry_id = created["entry"]["id"]
            self.assertEqual(
                set(created["entry"]),
                {"id", "type", "date", "time", "title", "content"},
            )

            status, _ = service.request(
                "POST",
                "/api/entries",
                {"type": "log", "date": "2026-10-02", "title": "Dashboard", "content": "API complete"},
            )
            self.assertEqual(status, 201)
            status, dashboard = service.request("GET", "/api/dashboard?date=2026-10-02")
            self.assertEqual(status, 200)
            self.assertEqual(dashboard["date"], "2026-10-02")
            self.assertEqual([item["title"] for item in dashboard["schedules"]], ["Mentor meeting"])
            self.assertEqual([item["title"] for item in dashboard["work_logs"]], ["Dashboard"])
            self.assertEqual(dashboard["counts"], {"schedules": 1, "work_logs": 1})
            self.assertEqual(
                dashboard["truncated"],
                {"schedules": False, "work_logs": False, "content_ids": []},
            )

            status, updated = service.request("PATCH", f"/api/entries/{entry_id}", {"title": "Mentor sync", "time": None})
            self.assertEqual(status, 200)
            self.assertEqual(updated["entry"]["title"], "Mentor sync")
            self.assertIsNone(updated["entry"]["time"])
            status, body = service.request("DELETE", f"/api/entries/{entry_id}")
            self.assertEqual((status, body), (204, None))
            status, missing = service.request("DELETE", f"/api/entries/{entry_id}")
            self.assertEqual(status, 404)
            self.assertEqual(missing["error"]["code"], "not_found")

    def test_default_date_and_hong_kong_timezone(self) -> None:
        self.assertEqual(
            local_today(datetime(2026, 10, 1, 16, 30, tzinfo=timezone.utc)),
            date(2026, 10, 2),
        )
        with RunningService(self.database, today=date(2030, 1, 2)) as service:
            status, dashboard = service.request("GET", "/api/dashboard")
            self.assertEqual(status, 200)
            self.assertEqual(dashboard["date"], "2030-01-02")

    def test_authentication(self) -> None:
        with RunningService(self.database, token="secret-token") as service:
            status, body = service.request("GET", "/api/dashboard")
            self.assertEqual(status, 401)
            self.assertEqual(body["error"]["code"], "unauthorized")
            status, _ = service.request("GET", "/api/dashboard", token="wrong")
            self.assertEqual(status, 401)
            status, dashboard = service.request("GET", "/api/dashboard", token="secret-token")
            self.assertEqual(status, 200)
            self.assertEqual(dashboard["date"], "2026-10-02")
            with urlopen(service.url + "/", timeout=3) as response:
                self.assertEqual(response.status, 200)

    def test_idempotency_and_conflict(self) -> None:
        payload = {
            "type": "schedule",
            "date": "2026-10-02",
            "time": "09:00",
            "title": "Daily sync",
            "content": "",
            "request_id": "voice-turn-123",
        }
        with RunningService(self.database) as service:
            status, first = service.request("POST", "/api/entries", payload)
            self.assertEqual(status, 201)
            status, replay = service.request("POST", "/api/entries", payload)
            self.assertEqual(status, 200)
            self.assertTrue(replay["idempotent"])
            self.assertEqual(first["entry"]["id"], replay["entry"]["id"])
            changed = dict(payload, title="Changed")
            status, conflict = service.request("POST", "/api/entries", changed)
            self.assertEqual(status, 409)
            self.assertEqual(conflict["error"]["code"], "idempotency_conflict")

    def test_persistence_across_server_restart(self) -> None:
        with RunningService(self.database) as service:
            status, _ = service.request(
                "POST", "/api/entries", {"type": "log", "date": "2026-10-02", "title": "Persisted"}
            )
            self.assertEqual(status, 201)
        with RunningService(self.database) as restarted:
            status, dashboard = restarted.request("GET", "/api/dashboard?date=2026-10-02")
            self.assertEqual(status, 200)
            self.assertEqual(dashboard["work_logs"][0]["title"], "Persisted")

    def test_validation_and_body_limits(self) -> None:
        with RunningService(self.database) as service:
            invalid_cases = [
                ({"type": "event", "date": "2026-10-02", "title": "X"}, "invalid_field"),
                ({"type": {"unexpected": True}, "date": "2026-10-02", "title": "X"}, "invalid_field"),
                ({"type": "log", "date": "2026-02-30", "title": "X"}, "invalid_field"),
                ({"type": "schedule", "date": "2026-10-02", "time": "25:00", "title": "X"}, "invalid_field"),
                ({"type": "log", "date": "2026-10-02", "title": "   "}, "invalid_field"),
                ({"type": "log", "date": "2026-10-02", "title": "X", "extra": 1}, "unknown_field"),
            ]
            for payload, code in invalid_cases:
                status, body = service.request("POST", "/api/entries", payload)
                self.assertEqual(status, 400)
                self.assertEqual(body["error"]["code"], code)

            status, body = service.request("POST", "/api/entries", raw_body=b"not-json")
            self.assertEqual(status, 400)
            self.assertEqual(body["error"]["code"], "invalid_json")
            status, body = service.request(
                "POST", "/api/entries", raw_body=b"{}", content_type="text/plain"
            )
            self.assertEqual(status, 415)
            self.assertEqual(body["error"]["code"], "unsupported_media_type")
            status, body = service.request("POST", "/api/entries", raw_body=b"x" * (64 * 1024 + 1))
            self.assertEqual(status, 413)
            self.assertEqual(body["error"]["code"], "body_too_large")

            invalid_unicode = b'{"type":"log","date":"2026-10-02","title":"\\ud800"}'
            status, body = service.request("POST", "/api/entries", raw_body=invalid_unicode)
            self.assertEqual(status, 400)
            self.assertEqual(body["error"]["code"], "invalid_unicode")

            status, body = service.request("PATCH", "/api/entries/9223372036854775808", {"title": "X"})
            self.assertEqual(status, 400)
            self.assertEqual(body["error"]["code"], "invalid_entry_id")

            for query in ("limit=0", "limit=101", "content_limit=0", "content_limit=10001", "limit=x"):
                status, body = service.request("GET", f"/api/dashboard?{query}")
                self.assertEqual(status, 400)
                self.assertEqual(body["error"]["code"], "invalid_query")

    def test_dashboard_limits_entries_and_content(self) -> None:
        with RunningService(self.database) as service:
            entry_ids = []
            for index in range(3):
                status, body = service.request(
                    "POST",
                    "/api/entries",
                    {
                        "type": "log",
                        "date": "2026-10-02",
                        "title": f"Log {index}",
                        "content": "abcdef",
                    },
                )
                self.assertEqual(status, 201)
                entry_ids.append(body["entry"]["id"])
            status, dashboard = service.request(
                "GET", "/api/dashboard?date=2026-10-02&limit=2&content_limit=3"
            )
            self.assertEqual(status, 200)
            self.assertEqual(dashboard["counts"]["work_logs"], 3)
            self.assertEqual(len(dashboard["work_logs"]), 2)
            self.assertTrue(dashboard["truncated"]["work_logs"])
            self.assertEqual(dashboard["truncated"]["content_ids"], entry_ids[:2])
            self.assertEqual([item["content"] for item in dashboard["work_logs"]], ["abc", "abc"])


if __name__ == "__main__":
    unittest.main()
