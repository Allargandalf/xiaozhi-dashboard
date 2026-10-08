import json
from pathlib import Path
import sys
import tempfile
import unittest
from datetime import date

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from dashboard_service import DashboardApp, EntryStore, handle_device_request
from serial_bridge import LineDecoder, MAX_FRAME_BYTES, REQUEST_PREFIX, RESPONSE_PREFIX, process_request


class SerialBridgeTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.database = Path(self.directory.name) / "usb.db"
        self.app = DashboardApp(EntryStore(self.database), today_provider=lambda: date(2026, 10, 6))

    def tearDown(self):
        self.directory.cleanup()

    def request(self, method, body, rpc_id="0123456789abcdef"):
        wire = REQUEST_PREFIX + json.dumps({"v": 1, "id": rpc_id, "method": method, "body": body}).encode()
        reply = process_request(wire, lambda method, body: handle_device_request(self.app, method, body), now=lambda: 1791244800)
        self.assertTrue(reply.startswith(RESPONSE_PREFIX))
        result = json.loads(reply[len(RESPONSE_PREFIX):])
        self.assertEqual(result["id"], rpc_id)
        return result

    def test_usb_write_is_persisted_and_lost_ack_retry_is_idempotent(self):
        body = {"type": "schedule", "date": "2026-10-06", "time": "15:00", "title": "与 mentor 开会", "content": "本周进度", "request_id": "voice-write-1"}
        first = self.request("entries.add", body)
        self.assertEqual(first["status"], 201)
        second = self.request("entries.add", body, "fedcba9876543210")
        self.assertEqual(second["status"], 200)
        self.assertTrue(second["body"]["idempotent"])
        self.assertEqual(first["body"]["entry"]["id"], second["body"]["entry"]["id"])
        reopened = EntryStore(self.database)
        snapshot = reopened.dashboard("2026-10-06", 4, 32)
        self.assertEqual(snapshot["counts"]["schedules"], 1)
        self.assertEqual(snapshot["schedules"][0]["title"], body["title"])

    def test_invalid_and_conflicting_writes_do_not_replace_records(self):
        body = {"type": "log", "date": "2026-10-06", "title": "完成联调", "request_id": "record-1"}
        self.assertEqual(self.request("entries.add", body)["status"], 201)
        self.assertEqual(self.request("entries.add", {**body, "title": "其他内容"})["status"], 409)
        self.assertEqual(self.request("entries.add", {**body, "date": "2026-02-30"})["status"], 400)
        self.assertEqual(self.request("entries.add", {"type": "log", "date": "2026-10-06", "title": "missing id"})["status"], 400)
        self.assertEqual(self.request("dashboard.get", {})["body"]["work_logs"][0]["title"], "完成联调")

    def test_snapshot_uses_hong_kong_day_and_provides_board_wall_clock(self):
        reply = self.request("dashboard.get", {})
        self.assertEqual(reply["body"]["date"], "2026-10-06")
        self.assertEqual(reply["local_timestamp"], 1791244800 + 28800)

    def test_ignores_logs_malformed_frames_and_non_allowlisted_methods(self):
        called = []
        def handler(*args):
            called.append(args)
            return 200, {}
        for wire in [b"I (100) board log", REQUEST_PREFIX + b"{broken", REQUEST_PREFIX + b"\xff", REQUEST_PREFIX + b'{"v":1,"id":"0123456789abcdef","method":[],"body":{}}', REQUEST_PREFIX + b'{"v":1,"id":"0123456789abcdef","method":"delete_all","body":{}}', REQUEST_PREFIX + b"x" * MAX_FRAME_BYTES]:
            self.assertIsNone(process_request(wire, handler))
        self.assertEqual(called, [])

    def test_partial_frames_and_overflow_do_not_merge_into_valid_requests(self):
        decoder = LineDecoder()
        self.assertEqual(decoder.feed(b"DASHBOARD_"), [])
        self.assertEqual(decoder.feed(b"RPC {}\r\nlog\n"), [b"DASHBOARD_RPC {}", b"log"])
        self.assertEqual(decoder.feed(b"x" * (MAX_FRAME_BYTES + 1) + b"DASHBOARD_RPC {}\n"), [])
        self.assertEqual(decoder.feed(b"next\n"), [b"next"])
        self.assertEqual(len(decoder.pending), 0)

    def test_oversized_reply_becomes_bounded_error_and_internal_exception_is_private(self):
        wire = REQUEST_PREFIX + b'{"v":1,"id":"0123456789abcdef","method":"dashboard.get","body":{}}'
        reply = process_request(wire, lambda *_: (200, {"large": "字" * MAX_FRAME_BYTES}))
        self.assertLess(len(reply), MAX_FRAME_BYTES)
        self.assertEqual(json.loads(reply[len(RESPONSE_PREFIX):])["status"], 413)
        def broken(*_):
            raise RuntimeError("private database content")
        reply = process_request(wire, broken)
        self.assertNotIn(b"private", reply)
        self.assertEqual(json.loads(reply[len(RESPONSE_PREFIX):])["status"], 500)


if __name__ == "__main__":
    unittest.main()
