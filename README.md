# xiaozhi-dashboard

这是面向立创·实战派 ESP32-S3 的小智 Dashboard v0.1。它保留官方 xiaozhi.me、语音、音频和配网流程，在设备端增加日程/工作日志界面与 MCP 工具，在电脑上用一个本地 SQLite 服务保存数据。

当前 `main` 开发版增加 **Type-C 直连同步、亮色三页 Dashboard、B 键翻页/语音切换、电脑管理页和 30 秒无交互自动返回**。这些改动只同步源码，未发布新的 GitHub Release；已有 `v0.1-pre-flash` 二进制仍是原来的 Wi-Fi 同步版本。当前验证记录见 [BUILD.md](BUILD.md)。

2026-10-06 已完成 ESP32-S3 实机识别、原固件备份、首次烧录及启动检查。2026-10-08 已完成新版烧录、Type-C 读取同步，用户确认工具切换立即退出和 30 秒无交互回退均正常；亮色三页更新后，B 单击/双击及语音选择页面也已实测通过。语音写入记录、长时间音频稳定性、运行时峰值内存、首次配网和 OTA 仍需验证。[布局预览](docs/dashboard-preview.html) 使用示例数据，按 320×240 屏幕尺寸制作。

```text
xiaozhi.me AI
    │ Wi-Fi 对话；发现并调用设备端 MCP
    ▼
ESP32（Assistant ↔ Dashboard）
    │ Type-C USB 串口同步
    ▼
电脑 companion（Web UI + SQLite）
```

## 目录

- `firmware/`：完整 ESP32 源码，基于官方 `78/xiaozhi-esp32`；不是 Git submodule。
- `companion/`：本地服务、USB 串口桥接、Web Dashboard 和测试；HTTP-only 模式只用 Python 标准库，USB 模式额外使用 `pyserial`。
- `scripts/`：串口配置与可复现发布打包工具。
- `config/`：本机运行与可选局域网兼容模式的配置说明。
- `release/`：发布说明随源码保存；构建时生成的可烧录文件不提交到 Git。

固件使用独立板卡目录 `lckfb/szpi-esp32s3-dashboard`，设备/OTA 身份为 `lichuang-dev-dashboard`，避免与官方 `lichuang-dev` 混用。

## 先在电脑启动 Dashboard

需要 Python 3.9 或更高版本。USB 模式先安装唯一的额外依赖：

```powershell
python -m pip install pyserial
```

Windows PowerShell：

用支持数据传输的 Type-C 线连接设备，确认设备串口（例如 `COM4`），再启动 companion：

```powershell
.\scripts\start_companion.ps1 -SerialPort COM4
```

脚本在同一进程中启动本地 Web 管理页和 USB 串口桥接，默认只监听 `127.0.0.1:8765`，不启用 token。直接打开 <http://127.0.0.1:8765/> 即可，不需要填写访问凭据。日常使用只打开 `8765`；不要使用旧测试页的 `8771` 端口。设备和电脑不需要处于同一局域网。Wi-Fi 仍用于小智配网、语音对话和 MCP 调用，但不承担默认的日程/日志同步。USB 连接时设备约每 5 秒刷新一次数据；手动配置为 HTTP 兼容模式时约每 30 秒刷新一次。

使用期间 Type-C 线需要保持连接，电脑不能睡眠，且启动脚本所在的 PowerShell 窗口需要保持运行，否则设备无法读取或新增记录。

日程和工作日志实际保存在电脑的 SQLite 文件 `companion/data/dashboard.db`。仓库中默认没有这个文件，companion 第一次启动时才会创建。启动脚本把这个文件的绝对路径显式传给服务，不会被 PowerShell 中遗留的 `DASHBOARD_DB` 改到别处；只有主动传入 `-DatabasePath` 才会换库。Web 管理页、USB 串口桥接和可选 HTTP 接口都由这一个 companion 进程提供，并读写同一个文件；不要另起测试服务或第二个数据库来判断板子是否同步。

服务接口包括：

- `GET /api/health`
- `GET /api/dashboard?date=YYYY-MM-DD&limit=10&content_limit=500`
- `POST /api/entries`
- `PATCH /api/entries/{id}`
- `DELETE /api/entries/{id}`

HTTP API 主要供浏览器管理页和手动配置的 Wi-Fi 兼容模式使用；它不是 USB 断开后的自动故障切换，也不会把结果不确定的写入跨传输重试。本机默认不鉴权；只有显式启用局域网 HTTP 兼容模式时才配置 token，供 ESP32 或 API 客户端携带 `Authorization: Bearer <token>`。本机浏览器管理仍使用 `http://127.0.0.1:8765/`。详细字段见 `companion/README.md`。

## 烧录与连接

当前开发版需要先按 [BUILD.md](BUILD.md) 构建，再按 [FLASH.md](FLASH.md) 在 `firmware/build` 内分区烧录。已有 `v0.1-pre-flash` 发布包不包含 USB 同步。首次启动沿用官方小智热点配网流程，Wi-Fi 供小智语音与 MCP 使用；日程/日志默认通过 Type-C 线同步，不需要设置电脑局域网 IP。

需要手动改用 Wi-Fi HTTP 兼容模式时，才需要把 Dashboard URL 配成电脑的实际局域网 IP。这一步不需要重新编译，也不会重写整块 NVS。默认的 `xiaozhi-dashboard.local` 只是安全构建回退值；v0.1 companion 不提供 mDNS 广播，不能依赖它自动解析。

一根支持数据传输的 Type-C 线即可完成供电、烧录、串口配置和日程/日志同步；只有充电功能的线不行。电脑端 companion 仍需持续运行。

这块双 Type-C 板卡使用 **USB 转 UART（CH340）接口**同步，本机对应 `COM4`；另一接口是原生 USB，当前没有实现该接口的数据同步。其他电脑的 COM 编号可能不同。

Dashboard 有总览、仅日程、仅工作记录三个页面。正常使用时 B 单击按这个顺序循环翻页；B 双击进入或退出语音对话。正在对话时单击翻页也会结束对话。保留启动连接失败时单击 B 进入配网的原有行为，没有新增长按配网。

浏览器约每 5 秒自动刷新一次当前所选日期，同时保留正在填写或编辑的表单。网页每类最多读取 100 条并显示完整内容；ESP32 屏幕始终按 `Asia/Hong_Kong` 的今天显示，每类最多 4 条，补充内容读取前 32 个字符，文字超出屏幕宽度时显示省略号。因此，网页选了其他日期、当天超过 4 条或文字较长时，两边画面可以不同，但数据仍来自同一个 `companion/data/dashboard.db`。

设备在唤醒和对话时显示 Assistant。`self.dashboard.set_view` 接受 `assistant`、`dashboard`、`overview`、`schedule`、`log`；后三个值直接选择对应页面，`dashboard` 返回最近选中的页面。选择任一 Dashboard 页面会立即结束当前对话；普通对话结束后，默认在 30 秒无活动时回到最近选中的页面。可对小智说“显示日程”或“切到工作记录”。

自行构建、运行测试和生成发布包见 [BUILD.md](BUILD.md)。

## 当前功能边界

- 数据保存在电脑 SQLite 中，没有接入 Apple Calendar。
- 工作记录是独立记录，可在电脑管理页添加，或通过语音调用 `self.dashboard.add_entry` 的 `type=log` 添加。日程和工作记录分别保存；日程不会随时间经过或完成而自动转成工作记录，当前也没有日程完成状态。
- companion 关闭、电脑休眠或 Type-C 断开时，ESP32 无法同步新数据；恢复连接后可重新读取。只有手动配置为 Wi-Fi HTTP 兼容模式时，网络中断才会影响同步。
- Dashboard 与原 Assistant UI 共存；唤醒和对话进入小智界面，显式切换可立即返回，普通对话默认在 30 秒无活动后返回。
- `self.dashboard.add_entry` 返回 `queued` 仅表示进入写入队列；电脑完成保存后设备刷新数据，才能确认已保存。
- 本版本不修改 xiaozhi.me 后端。

本项目采用根目录 `LICENSE` 所列的 MIT 许可；上游来源和固定 commit 见 `THIRD_PARTY/UPSTREAM.md`。

GitHub 自动构建尚未启用；`config/ci/preflash.yml` 保留了可选模板，当前发布依据本机 ESP-IDF 6.0.1 的真实构建与测试结果。
