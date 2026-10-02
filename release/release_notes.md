# v0.1 pre-flash

状态：**ESP-IDF 6.0.1 compile verified，hardware unverified**。本版用于首次烧录和办公室真机联调。2026-10-02 已通过固件主机测试 100 项、companion 测试 7 项、顶层工具测试 12 项及 canonical 完整构建；实际对外发布包仍必须同时包含 `merged-firmware.bin`、`manifest.json` 和 `SHA256SUMS`，并与对应 commit 一致。

应用镜像约 2.7 MiB（app 分区剩余约 31%），assets 约 1.7 MiB，静态 DIRAM 占用约 46%。准确文件大小与 SHA-256 见发布包 manifest。这些是离线构建数据，不代表 PSRAM 实际容量或运行时峰值。

## 包含内容

- 立创·实战派 ESP32-S3 独立板卡/OTA 身份 `lichuang-dev-dashboard`。
- Assistant 与 Dashboard 双界面，日程和带日期的工作日志展示。
- 设备端 `self.dashboard.set_view`、`self.dashboard.add_entry` MCP 工具。
- 电脑端 SQLite/HTTP companion、浏览器管理页与 Bearer token 鉴权。
- 串口运行配置工具；修改电脑 IP/token 不需要重编固件。
- 完整镜像、分区镜像、flash args、SHA-256 和来源/工具版本 manifest 的自动打包流程。

## 已知限制

- companion 尚未发布 mDNS 名称；必须把设备 URL 配成电脑的局域网 IP。
- 未接入 Apple Calendar，数据只保存在电脑 SQLite。
- 屏幕、真实 Wi-Fi、xiaozhi.me MCP 调用、音频并发、内存峰值、重启、配网和 OTA 均需真机验证。
- 完整镜像可能清除原有 NVS/OTA 状态；保留设备数据时按 `FLASH.md` 使用并检查分区烧录。

构建见 `BUILD.md`，烧录与真机检查单见 `FLASH.md`。
