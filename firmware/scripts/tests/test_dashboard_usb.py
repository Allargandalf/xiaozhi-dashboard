import os
from pathlib import Path
import shlex
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class DashboardUsbFramingTests(unittest.TestCase):
    def test_actual_firmware_line_decoder(self):
        build = ROOT / "build/host-tests"
        build.mkdir(parents=True, exist_ok=True)
        output = build / ("dashboard_usb_test.exe" if os.name == "nt" else "dashboard_usb_test")
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        env = os.environ.copy()
        env.setdefault("ZIG_GLOBAL_CACHE_DIR", str(build / "zig-global-cache"))
        env.setdefault("ZIG_LOCAL_CACHE_DIR", str(build / "zig-local-cache"))
        subprocess.run(compiler + ["-std=c++20", f"-I{ROOT / 'main/boards/lckfb/szpi-esp32s3-dashboard'}", str(ROOT / "scripts/tests/dashboard_usb_host_test.cc"), "-o", str(output)], check=True, cwd=build, env=env)
        subprocess.run([output], check=True, cwd=build)
