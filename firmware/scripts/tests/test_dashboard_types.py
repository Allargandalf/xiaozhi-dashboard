import json
import os
from pathlib import Path
import shlex
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "main/boards/lckfb/szpi-esp32s3-dashboard"


class DashboardBoardConfigTests(unittest.TestCase):
    def test_variant_has_independent_ota_identity_and_safe_defaults(self):
        config = json.loads((BOARD / "config.json").read_text(encoding="utf-8"))
        self.assertEqual(config["type"], "lichuang-dev-dashboard")
        self.assertEqual(config["builds"][0]["name"], "lichuang-dev-dashboard")

        cmake = (ROOT / "main/CMakeLists.txt").read_text(encoding="utf-8")
        kconfig = (ROOT / "main/Kconfig.projbuild").read_text(encoding="utf-8")
        self.assertIn('set(BOARD_DIR "lckfb/szpi-esp32s3-dashboard")', cmake)
        self.assertIn("config BOARD_TYPE_LICHUANG_DEV_S3_DASHBOARD", kconfig)
        self.assertIn('default "http://xiaozhi-dashboard.local:8765"', kconfig)
        self.assertIn('config DASHBOARD_SERVICE_TOKEN', kconfig)
        self.assertRegex(kconfig, r"config DASHBOARD_SERVICE_TOKEN[\s\S]*?default \"\"")

    def test_portable_validation_logic(self):
        build_dir = ROOT / "build" / "host-tests"
        build_dir.mkdir(parents=True, exist_ok=True)
        source = ROOT / "scripts/tests/dashboard_types_host_test.cc"
        executable = build_dir / (
            "dashboard_types_test.exe" if os.name == "nt" else "dashboard_types_test"
        )
        command = shlex.split(os.environ.get("CXX", "c++")) + [
            "-x",
            "c++",
            "-std=c++20",
            f"-I{BOARD}",
            f"-I{ROOT / 'managed_components/espressif__cjson/cJSON'}",
            str(source),
            str(ROOT / "managed_components/espressif__cjson/cJSON/cJSON.c"),
            "-o",
            str(executable),
        ]
        environment = os.environ.copy()
        environment.setdefault("ZIG_GLOBAL_CACHE_DIR", str(build_dir / "zig-global-cache"))
        environment.setdefault("ZIG_LOCAL_CACHE_DIR", str(build_dir / "zig-local-cache"))
        subprocess.run(command, check=True, cwd=build_dir, env=environment)
        subprocess.run([executable], check=True, cwd=build_dir)


if __name__ == "__main__":
    unittest.main()
