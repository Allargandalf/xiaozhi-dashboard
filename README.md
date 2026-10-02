# xiaozhi-dashboard

这是面向立创·实战派 ESP32-S3 的小智 Dashboard v0.1。它保留官方 xiaozhi.me、语音、音频和配网流程，在设备端增加日程/工作日志界面与 MCP 工具，在电脑上用一个本地 SQLite 服务保存数据。

当前发布阶段是 **v0.1 pre-flash — ESP-IDF 6.0.1 编译验证完成，hardware unverified**。2026-10-02 的离线验证通过固件主机测试 100 项、companion 测试 7 项和顶层发布/串口测试 12 项；canonical 固件完整构建通过。屏幕方向、刷新、真实 Wi-Fi、xiaozhi.me 实际 MCP 调用、音频干扰、运行时内存峰值、重启、配网和 OTA 都应在板子上复测。

```text
xiaozhi.me AI
    │ 发现并调用设备端 MCP
    ▼
ESP32（Assistant ↔ Dashboard）
    │ HTTP + Bearer token / Wi-Fi
    ▼
电脑 companion（Web UI + SQLite）
```

## 目录

- `firmware/`：完整 ESP32 源码，基于官方 `78/xiaozhi-esp32`；不是 Git submodule。
- `companion/`：只依赖 Python 标准库的本地服务、Web Dashboard 和测试。
- `scripts/`：串口配置与可复现发布打包工具。
- `config/`：运行配置和密钥处理说明。
- `release/`：发布说明随源码保存；构建时生成的可烧录文件不提交到 Git。

固件使用独立板卡目录 `lckfb/szpi-esp32s3-dashboard`，设备/OTA 身份为 `lichuang-dev-dashboard`，避免与官方 `lichuang-dev` 混用。

## 先在电脑启动 Dashboard

需要 Python 3.9 或更高版本。Windows PowerShell：

首次真机联调时先确认电脑局域网 IP，再让脚本生成持久的本地 token、配置设备并启动服务：

```powershell
.\scripts\start_companion.ps1 `
  -ConfigurePort COM7 `
  -DashboardUrl http://192.168.1.23:8765
```

脚本把 token 保存在被 Git 忽略的 `config/dashboard.token`，不会显示 token。后续重启服务只运行 `.\scripts\start_companion.ps1`，它会复用同一 token。更换 token 时必须同时重配设备。

浏览器打开 `http://127.0.0.1:8765/`。ESP32 与电脑必须在同一可互访的网络；Windows 防火墙提示时只允许受信任的专用网络。服务默认数据库位于 `companion/data/dashboard.db`。

服务接口包括：

- `GET /api/health`
- `GET /api/dashboard?date=YYYY-MM-DD&limit=10&content_limit=500`
- `POST /api/entries`
- `PATCH /api/entries/{id}`
- `DELETE /api/entries/{id}`

设置 token 后，所有 API 请求都需要 `Authorization: Bearer <token>`。详细字段见 `companion/README.md`。

## 烧录与连接

预编译发布包生成后，按 [FLASH.md](FLASH.md) 烧录 `release/merged-firmware.bin`。首次启动沿用官方小智热点配网流程。完成 Wi-Fi 后，使用上面的启动脚本把设备 Dashboard URL 改成电脑的实际局域网 IP，然后启动 companion。

这一步不需要重新编译，也不会重写整块 NVS。默认的 `xiaozhi-dashboard.local` 只是安全构建回退值；v0.1 companion 不提供 mDNS 广播，不能依赖它自动解析。

自行构建、运行测试和生成发布包见 [BUILD.md](BUILD.md)。

## v0.1 边界

- 数据保存在电脑 SQLite 中，没有接入 Apple Calendar。
- 电脑服务关闭或网络断开时，ESP32 无法同步新数据；恢复连接后可重新读取。
- Dashboard 与原 Assistant UI 共存；唤醒和对话仍回到小智界面。
- 本版本不修改 xiaozhi.me 后端。

本项目采用根目录 `LICENSE` 所列的 MIT 许可；上游来源和固定 commit 见 `THIRD_PARTY/UPSTREAM.md`。

GitHub 自动构建尚未启用；`config/ci/preflash.yml` 保留了可选模板，当前发布依据本机 ESP-IDF 6.0.1 的真实构建与测试结果。
