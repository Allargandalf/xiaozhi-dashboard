# 烧录与首次真机验证

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

激活 ESP-IDF 6.0.1+ 环境，连接板子并确认串口，例如 `COM7`：

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

首次启动且没有 Wi-Fi 凭据时，设备沿用官方热点配网流程。按屏幕/语音提示完成配网，并确保板子和电脑处于同一可互访网络。

完成热点配网后先停止已经运行的 companion。确认电脑的局域网 IP，然后用一个命令创建/复用本地 token、配置设备并启动服务：

```powershell
.\scripts\start_companion.ps1 `
  -ConfigurePort COM7 `
  -DashboardUrl http://192.168.1.23:8765
```

这里不能填 `127.0.0.1`。脚本先从 `config/dashboard.token` 加载同一个 token，通过串口写入并回读设备配置，然后才以前台方式启动服务。它不会显示 token；后续服务重启只运行：

```powershell
.\scripts\start_companion.ps1
```

不要每次生成新 token；设备和服务 token 不同会返回 401。需要轮换 token 时，删除/替换本地 token 后再次运行带 `-ConfigurePort` 和 `-DashboardUrl` 的完整命令。串口工具只修改 `dashboard` NVS 命名空间的 URL/token，不重写整块 NVS。默认 `http://xiaozhi-dashboard.local:8765` 在 v0.1 不可依赖，因为 companion 尚未发布 mDNS 名称。

## 5. 真机检查单

- 屏幕方向、字体和 Dashboard 刷新正常。
- 热点配网、重启和断网重连正常。
- 浏览器能打开电脑 Dashboard；ESP32 能读取当日日程/日志。
- xiaozhi.me 能发现并调用 `self.dashboard.set_view` 与 `self.dashboard.add_entry`。
- 对话时返回 Assistant UI，结束后可回 Dashboard。
- 录音、播放、唤醒、打断没有明显退化。
- 观察峰值内存与长时间运行稳定性。
- 单独验证分区烧录和 OTA 后再把版本标记为 hardware verified。

失败时保留完整串口日志、固件 commit、`manifest.json` 和复现步骤，供 v0.2 定位。
