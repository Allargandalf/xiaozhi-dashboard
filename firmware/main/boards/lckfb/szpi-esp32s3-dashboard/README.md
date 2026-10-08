# LCKFB SZPI ESP32-S3 Dashboard

This is an independent firmware variant of the LCKFB SZPI ESP32-S3 board. It keeps the
upstream display, touch, camera, audio, AEC, Wi-Fi provisioning, and xiaozhi.me protocol,
while adding a three-page light dashboard and two device MCP tools. B now controls
dashboard pages and voice conversations instead of press-to-talk gestures or AEC switching.

## Build identity

```powershell
python scripts/build.py lckfb/szpi-esp32s3-dashboard `
  --name lichuang-dev-dashboard --language zh-CN --zip
```

The reported board type and board name are both `lichuang-dev-dashboard`. This prevents
the dashboard image from sharing OTA identity with the upstream `lichuang-dev` image.

## Runtime companion configuration

The primary companion transport is the board's UART0 USB-to-UART connection at 115200
8N1. Install `pyserial`, keep a data-capable Type-C cable connected, and run the web
service plus serial bridge from the repository root, replacing `COM4` with the board's
actual port:

```powershell
python -m pip install pyserial
.\scripts\start_companion.ps1 -SerialPort COM4
```

This USB path does not require the board and computer to share a LAN. Wi-Fi remains in
use for Xiaozhi conversations. The release image also retains HTTP over Wi-Fi as a
legacy, explicitly configured compatibility mode; it is not automatic USB failover.
It contains no secret; the fallback URL is
`http://xiaozhi-dashboard.local:8765` and its fallback token is empty. Runtime fallback
values are stored in NVS under namespace `dashboard`, keys `url` and `token`.

For fallback configuration, connect to UART0 USB-to-UART at 115200 8N1. The input path
is a bounded raw reader: it does not echo commands and does not retain command history.

```text
dashboard_config set http://192.168.1.20:8765 your-token
dashboard_config set http://192.168.1.20:8765 -
dashboard_config show
dashboard_config clear
```

Responses begin with `DASHBOARD_CONFIG ` and contain one JSON object. `show` reports only
whether a token is set; it never prints the token.

## Runtime behavior

- Idle state shows the selected overview, schedule, or work-record page. Listening, speaking, connection,
  provisioning, errors, and upgrades use the original assistant UI.
- B single-click cycles overview → schedule → work records. B double-click starts or
  ends a voice conversation. A single click during a conversation ends it and shows the
  next page. The upstream single-click provisioning action during startup remains;
  no long-press provisioning action is added.
- `self.dashboard.set_view` accepts `assistant`, `dashboard`, `overview`, `schedule`,
  and `log`. Selecting a dashboard view ends the active conversation immediately.
  `dashboard` preserves the last page; an ordinary conversation returns to that page
  after 30 seconds without activity.
- `self.dashboard.add_entry` validates and queues a bounded write. Its immediate response
  says `queued` and `saved:false`; it never claims that the computer has persisted data.
- Schedule and work-log data use the USB serial bridge by default. The optional HTTP
  fallback worker runs at low priority, has a four-job queue, uses four-second request
  timeouts, retries an uncertain POST once with the same idempotency key, and limits GET
  responses to 16 KiB. The device refreshes about every five seconds while USB is
  connected and every 30 seconds when using the HTTP compatibility mode. An uncertain
  write is never retried through the other transport.

The previous USB/idle-return version was built, flashed, and checked on the physical
board. Validation of the current page/button changes is recorded in the root BUILD.md.
First-time provisioning, voice writes, long-running audio, peak memory, and OTA still
require physical verification.
