# LCKFB SZPI ESP32-S3 Dashboard

This is an independent firmware variant of the LCKFB SZPI ESP32-S3 board. It keeps the
upstream display, touch, camera, audio, AEC, Wi-Fi provisioning, xiaozhi.me protocol, and
press-to-talk behavior, while adding an idle dashboard and two device MCP tools.

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

- Idle state shows today's schedule and work log. Listening, speaking, connection,
  provisioning, errors, and upgrades use the original assistant UI.
- `self.dashboard.set_view` selects `assistant` or `dashboard`. Selecting `dashboard`
  ends the active conversation and returns immediately; otherwise an ordinary
  conversation returns to the dashboard after 30 seconds without activity.
- `self.dashboard.add_entry` validates and queues a bounded write. Its immediate response
  says `queued` and `saved:false`; it never claims that the computer has persisted data.
- Schedule and work-log data use the USB serial bridge by default. The optional HTTP
  fallback worker runs at low priority, has a four-job queue, uses four-second request
  timeouts, retries an uncertain POST once with the same idempotency key, and limits GET
  responses to 16 KiB. The device refreshes about every five seconds while USB is
  connected and every 30 seconds when using the HTTP compatibility mode. An uncertain
  write is never retried through the other transport.

This variant is compile-tested without hardware. Display orientation, UART wiring and
USB synchronization, Wi-Fi reachability, MCP discovery, audio coexistence, memory peaks,
OTA, and restart behavior still require the physical board.
