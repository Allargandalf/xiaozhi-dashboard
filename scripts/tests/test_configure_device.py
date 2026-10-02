import argparse
import unittest

from scripts.configure_device import (
    ConfigurationError,
    configure,
    parse_response,
    validate_url,
)


class FakeSerial:
    def __init__(self, responses):
        self.responses = [response.encode() + b"\n" for response in responses]
        self.commands = []

    def write(self, value):
        self.commands.append(value)

    def flush(self):
        pass

    def readline(self):
        return self.responses.pop(0) if self.responses else b""


class ConfigureDeviceTests(unittest.TestCase):
    def test_parses_only_prefixed_json(self):
        self.assertIsNone(parse_response(b"ordinary boot log\n"))
        self.assertEqual(
            parse_response(b'DASHBOARD_CONFIG {"ok":true,"token_set":false}\n'),
            {"ok": True, "token_set": False},
        )

    def test_configures_and_verifies_without_exposing_token(self):
        port = FakeSerial(
            [
                'DASHBOARD_CONFIG {"ok":true,"url":"http://old:8765","token_set":false}',
                'DASHBOARD_CONFIG {"ok":true,"url":"http://192.168.1.8:8765","token_set":true}',
                'DASHBOARD_CONFIG {"ok":true,"url":"http://192.168.1.8:8765","token_set":true}',
            ]
        )
        result = configure(
            port, "http://192.168.1.8:8765", "very-secret-value", timeout=0.1
        )
        self.assertTrue(result["token_set"])
        self.assertEqual(port.commands[-1], b"dashboard_config show\r\n")

    def test_retries_readiness_probe_when_first_command_is_lost(self):
        class DelayedReadySerial(FakeSerial):
            def __init__(self):
                super().__init__([])

            def write(self, value):
                super().write(value)
                if len(self.commands) == 2:
                    self.responses.append(
                        b'DASHBOARD_CONFIG {"ok":true,"url":"http://old:8765","token_set":false}\n'
                    )
                elif len(self.commands) in {3, 4}:
                    self.responses.append(
                        b'DASHBOARD_CONFIG {"ok":true,"url":"http://192.168.1.8:8765","token_set":true}\n'
                    )

        port = DelayedReadySerial()
        configure(
            port,
            "http://192.168.1.8:8765",
            "stable-token",
            timeout=0.2,
            probe_timeout=0.001,
        )
        self.assertEqual(port.commands[0], b"dashboard_config show\r\n")
        self.assertEqual(port.commands[1], b"dashboard_config show\r\n")

    def test_rejects_device_error(self):
        port = FakeSerial(
            ['DASHBOARD_CONFIG {"ok":false,"error":"invalid_url"}']
        )
        with self.assertRaisesRegex(ConfigurationError, "invalid_url"):
            configure(port, "http://127.0.0.1:8765", None, timeout=0.1)

    def test_rejects_reserved_and_utf8_oversized_tokens(self):
        for token in ("-", "界" * 65):
            with self.subTest(token_length=len(token)):
                with self.assertRaisesRegex(ConfigurationError, "192 UTF-8 bytes"):
                    configure(FakeSerial([]), "http://host:8765", token, timeout=0.1)

    def test_rejects_unusable_url(self):
        with self.assertRaises(argparse.ArgumentTypeError):
            validate_url("http://host/path with spaces")
        with self.assertRaisesRegex(argparse.ArgumentTypeError, "192 UTF-8 bytes"):
            validate_url("http://host/" + "界" * 65)


if __name__ == "__main__":
    unittest.main()
