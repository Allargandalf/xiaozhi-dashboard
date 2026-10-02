import hashlib
import json
import os
import tempfile
import unittest
from pathlib import Path

from scripts.package_release import PackagingError, package_release


class PackageReleaseTests(unittest.TestCase):
    def setUp(self):
        sandbox_root = os.environ.get("PACKAGE_TEST_ROOT")
        if sandbox_root:
            # A managed Windows sandbox can pre-create these fixture directories.
            self.temporary = None
            self.root = Path(sandbox_root).resolve() / self._testMethodName
        else:
            self.temporary = tempfile.TemporaryDirectory()
            self.root = Path(self.temporary.name)
        self.build = self.root / "firmware" / "build"
        self.output = self.root / "release"
        (self.build / "bootloader").mkdir(parents=True, exist_ok=True)
        (self.build / "partition_table").mkdir(exist_ok=True)
        (self.build / "config").mkdir(exist_ok=True)
        self.board_config = (
            self.root
            / "firmware"
            / "main"
            / "boards"
            / "lckfb"
            / "szpi-esp32s3-dashboard"
            / "config.json"
        )
        self.board_config.parent.mkdir(parents=True, exist_ok=True)
        (self.build / "bootloader" / "bootloader.bin").write_bytes(b"B" * 32)
        (self.build / "partition_table" / "partition-table.bin").write_bytes(
            b"P" * 32
        )
        (self.build / "app.bin").write_bytes(b"A" * 64)
        self.flasher = {
            "write_flash_args": [
                "--flash_mode",
                "dio",
                "--flash_freq",
                "80m",
                "--flash_size",
                "16MB",
            ],
            "flash_files": {
                "0x0": "bootloader/bootloader.bin",
                "0x8000": "partition_table/partition-table.bin",
                "0x10000": "app.bin",
            },
            "extra_esptool_args": {"chip": "esp32s3"},
        }
        self._write_flasher()
        self.sdkconfig = {
            "BOARD_TYPE_LICHUANG_DEV_S3": False,
            "BOARD_TYPE_LICHUANG_DEV_S3_DASHBOARD": True,
            "DASHBOARD_SERVICE_URL": "http://xiaozhi-dashboard.local:8765",
            "DASHBOARD_SERVICE_TOKEN": "",
            "IDF_TARGET": "esp32s3",
        }
        self._write_sdkconfig()
        (self.build / "CMakeCache.txt").write_text(
            "BOARD_NAME:UNINITIALIZED=lichuang-dev-dashboard\n",
            encoding="utf-8",
        )
        (self.build / "project_description.json").write_text(
            json.dumps({"target": "esp32s3"}), encoding="utf-8"
        )
        self.board_config.write_text(
            json.dumps(
                {
                    "type": "lichuang-dev-dashboard",
                    "target": "esp32s3",
                    "builds": [{"name": "lichuang-dev-dashboard"}],
                }
            ),
            encoding="utf-8",
        )

    def tearDown(self):
        if self.temporary is not None:
            self.temporary.cleanup()

    def _write_flasher(self):
        (self.build / "flasher_args.json").write_text(
            json.dumps(self.flasher), encoding="utf-8"
        )

    def _write_sdkconfig(self):
        (self.build / "config" / "sdkconfig.json").write_text(
            json.dumps(self.sdkconfig), encoding="utf-8"
        )

    def _fake_esptool(self, command):
        output = Path(command[command.index("-o") + 1])
        output.write_bytes(b"M" * (0x10000 + 64))

    def test_packages_images_manifest_and_checksums(self):
        manifest = package_release(
            build_dir=self.build,
            output_dir=self.output,
            project_root=self.root,
            board="lckfb/szpi-esp32s3-dashboard",
            variant="lichuang-dev-dashboard",
            release_version="v0.1-pre-flash",
            source_revision="abc123",
            esptool_runner=self._fake_esptool,
        )

        self.assertFalse(manifest["hardware_verified"])
        self.assertEqual(manifest["provenance"]["source_revision"], "abc123")
        self.assertEqual(manifest["board_identity"], "lichuang-dev-dashboard")
        self.assertTrue((self.output / "merged-firmware.bin").is_file())
        self.assertIn("0x10000 app.bin", (self.output / "flash_args").read_text())

        app_entry = next(item for item in manifest["files"] if item["name"] == "app.bin")
        self.assertEqual(app_entry["size"], 64)
        self.assertEqual(
            app_entry["sha256"], hashlib.sha256(b"A" * 64).hexdigest()
        )
        sums = (self.output / "SHA256SUMS").read_text(encoding="utf-8")
        self.assertIn("  manifest.json\n", sums)

    def test_rejects_image_outside_build_directory(self):
        outside = self.root / "outside.bin"
        outside.write_bytes(b"unsafe")
        self.flasher["flash_files"]["0x10000"] = "../../outside.bin"
        self._write_flasher()

        with self.assertRaisesRegex(PackagingError, "escapes the build directory"):
            package_release(
                build_dir=self.build,
                output_dir=self.output,
                project_root=self.root,
                board="lckfb/szpi-esp32s3-dashboard",
                variant="lichuang-dev-dashboard",
                release_version="test",
                esptool_runner=self._fake_esptool,
            )

    def test_rejects_missing_image(self):
        self.flasher["flash_files"]["0x10000"] = "missing.bin"
        self._write_flasher()

        with self.assertRaisesRegex(PackagingError, "missing or empty"):
            package_release(
                build_dir=self.build,
                output_dir=self.output,
                project_root=self.root,
                board="lckfb/szpi-esp32s3-dashboard",
                variant="lichuang-dev-dashboard",
                release_version="test",
                esptool_runner=self._fake_esptool,
            )

    def test_rejects_nonempty_output_directory(self):
        self.output.mkdir()
        (self.output / "stale.bin").write_bytes(b"stale")

        with self.assertRaisesRegex(PackagingError, "generated or unknown files"):
            package_release(
                build_dir=self.build,
                output_dir=self.output,
                project_root=self.root,
                board="lckfb/szpi-esp32s3-dashboard",
                variant="lichuang-dev-dashboard",
                release_version="test",
                esptool_runner=self._fake_esptool,
            )

    def test_rejects_wrong_board_selection(self):
        self.sdkconfig["BOARD_TYPE_LICHUANG_DEV_S3_DASHBOARD"] = False
        self.sdkconfig["BOARD_TYPE_LICHUANG_DEV_S3"] = True
        self._write_sdkconfig()

        with self.assertRaisesRegex(PackagingError, "does not select only"):
            package_release(
                build_dir=self.build,
                output_dir=self.output,
                project_root=self.root,
                board="lckfb/szpi-esp32s3-dashboard",
                variant="lichuang-dev-dashboard",
                release_version="test",
                esptool_runner=self._fake_esptool,
            )

    def test_rejects_compile_time_token_without_disclosing_it(self):
        secret = "must-not-appear-in-errors"
        self.sdkconfig["DASHBOARD_SERVICE_TOKEN"] = secret
        self._write_sdkconfig()

        with self.assertRaises(PackagingError) as raised:
            package_release(
                build_dir=self.build,
                output_dir=self.output,
                project_root=self.root,
                board="lckfb/szpi-esp32s3-dashboard",
                variant="lichuang-dev-dashboard",
                release_version="test",
                esptool_runner=self._fake_esptool,
            )
        self.assertIn("non-empty compile-time Dashboard token", str(raised.exception))
        self.assertNotIn(secret, str(raised.exception))


if __name__ == "__main__":
    unittest.main()
