# 烧录与首次真机验证

当前 `main` 的 USB 同步和新版界面尚未发布二进制。要使用这些功能，请先按 `BUILD.md` 构建，再在 `firmware/build` 中用 `write-flash '@flash_args'` 烧录；已有 `v0.1-pre-flash` 下载包不包含这些改动。

v0.1 发布包只面向立创·实战派 ESP32-S3 Dashboard 独立变体。烧录前检查 `release/manifest.json` 中：

```json
{
  "board_directory": "lckfb/szpi-esp32s3-dashboard",
  "board_identity": "lichuang-dev-dashboard",
  "chip": "esp32s3",
  "hardware_verified": false
}
```

## 1. 校验下载文件

在项目根目录运行：

```powershell
Get-Content release/SHA256SUMS
Get-FileHash release/merged-firmware.bin -Algorithm SHA256
```

哈希必须与 `SHA256SUMS` 和 `manifest.json` 一致。

## 2. 首次完整烧录

使用支持数据传输的 Type-C 线连接板子（只有充电功能的线无法烧录或配置），激活 ESP-IDF 6.0.1+ 环境并确认串口，例如 `COM7`：

```powershell
python -m esptool --chip esp32s3 --port COM7 --baud 460800 `
  write-flash 0x0 release/merged-firmware.bin
```

`merged-firmware.bin` 是首次安装/恢复用完整镜像。它覆盖的地址范围可能清除原有 Wi-Fi、激活信息、Dashboard 配置和 OTA 状态；如需保留已有设备数据，先备份并使用下一节的分区烧录。

不要把官方 `lichuang-dev` 的 OTA 包与本项目互刷。本项目固定报告 `lichuang-dev-dashboard`，后续 OTA 也必须保持相同身份、芯片和分区布局。v0.1 尚未完成真机 OTA 验证。

## 3. 保留 NVS 的分区烧录

发布包的 `flash_args` 只列出本次构建产生的镜像，不包含 NVS 数据镜像。进入发布目录后执行：

```powershell
Push-Location release
python -m esptool --chip esp32s3 --port COM7 --baud 460800 `
  write-flash '@flash_args'
Pop-Location
```

执行前先查看 `flash_args`，确认没有要保留的 NVS 地址。分区烧录会更新其中列出的 bootloader、分区表、应用、资源和 OTA data 等内容；它保留未列出的 NVS，但并不等同于已经验证的在线 OTA。

## 4. 配 Wi-Fi 与电脑服务

首次启动且没有 Wi-Fi 凭据时，设备沿用官方热点配网流程。按屏幕/语音提示完成配网；Wi-Fi 用于小智语音对话和 MCP 调用。日程/日志默认走 Type-C USB，不要求板子与电脑处于同一局域网。

保持支持数据传输的 Type-C 线连接，确认设备串口后，在项目根目录启动 companion。例如串口为 `COM4`：

```powershell
python -m pip install pyserial
.\scripts\start_companion.ps1 -SerialPort COM4
```

脚本在同一进程中启动本地 Web 管理页与 USB 串口桥接。使用期间保持 Type-C 连接、电脑唤醒和这个 PowerShell 窗口运行。浏览器管理页为 `http://127.0.0.1:8765/`；首次打开时，在“访问令牌”中粘贴 `config/dashboard.token` 的内容。记录写入 `companion/data/dashboard.db`；该文件在 companion 首次启动时创建。

如需手动改用 Wi-Fi HTTP 兼容模式，先确认电脑的局域网 IP，再让脚本创建/复用本地 token、配置设备并启动服务：

```powershell
.\scripts\start_companion.ps1 `
  -ListenAddress 0.0.0.0 `
  -ConfigurePort COM4 `
  -DashboardUrl http://192.168.1.23:8765
```

兼容模式地址不能填 `127.0.0.1`，板子与电脑必须在同一可互访局域网。它需要显式配置，不是 USB 断开后的自动故障切换，也不会把结果不确定的写入跨传输重试。不要每次生成新 token；设备和服务 token 不同会返回 401。需要轮换 token 时，删除/替换本地 token 后再次运行带 `-ConfigurePort` 和 `-DashboardUrl` 的完整命令。串口工具只修改 `dashboard` NVS 命名空间的 URL/token，不重写整块 NVS。默认 `http://xiaozhi-dashboard.local:8765` 在 v0.1 不可依赖，因为 companion 尚未发布 mDNS 名称。

USB 与 Wi-Fi HTTP 两种方式使用同一个 SQLite 数据库和浏览器管理页。

## 5. 真机检查单

- 屏幕方向、字体和 Dashboard 刷新正常。
- 热点配网、重启和断网重连正常。
- 浏览器能打开电脑 Dashboard；ESP32 能通过 Type-C 读取和新增当日日程/日志。
- xiaozhi.me 能发现并调用 `self.dashboard.set_view` 与 `self.dashboard.add_entry`。
- `self.dashboard.set_view` 传入 `view=dashboard` 时立即结束当前对话并显示 Dashboard。
- 未显式切换时，普通对话结束后默认在 30 秒无活动时自动显示 Dashboard。
- 录音、播放、唤醒、打断没有明显退化。
- 观察峰值内存与长时间运行稳定性。
- 单独验证分区烧录和 OTA 后再把版本标记为 hardware verified。

失败时保留完整串口日志、固件 commit、`manifest.json` 和复现步骤，供 v0.2 定位。
