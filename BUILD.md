# 构建与验证

## 环境

- Python 3.9+
- ESP-IDF 6.0.1 或更高版本；推荐 6.1
- Git

先进入 ESP-IDF 提供的终端，或执行对应安装目录的 `export.ps1` / `export.sh`，然后确认版本：

```powershell
idf.py --version
python --version
```

ESP-IDF 5.x 不受支持。附带的 GitHub Actions 模板使用官方 `espressif/idf:v6.1` 镜像；当前未启用，ESP-IDF 6.1 尚未验证。

## 固件构建

构建命令必须使用独立板卡和独立设备身份：

```powershell
Push-Location firmware
python scripts/build.py lckfb/szpi-esp32s3-dashboard `
  --name lichuang-dev-dashboard `
  --language zh-CN `
  --zip
Pop-Location
```

成功后，官方构建脚本会生成 `firmware/build/merged-binary.bin`，并在使用 `--zip` 时生成 `firmware/releases/v<版本>_lckfb-lichuang-dev-dashboard.zip`。

## 主机测试

全新 checkout 必须先完成上面的 canonical 固件构建。构建会解析 `managed_components`，Dashboard 的实际 cJSON 回归测试需要这些头文件；提前运行完整固件主机测试会因依赖尚未解析而失败。该 C++ host test 还需要可用的 `c++` 编译器（Windows 可通过 `CXX` 指定，例如 `zig c++`）。

在项目根目录执行：

```powershell
python -m unittest discover -s scripts/tests -v
python -m unittest discover -s companion/tests -v
Push-Location firmware
python -m unittest discover -s scripts/tests -v
Pop-Location
```

固件测试覆盖官方构建脚本与 Dashboard 参数/协议逻辑；companion 测试覆盖 SQLite 和 HTTP API；顶层测试覆盖发布包校验与串口配置协议。

## 生成 v0.1 发布目录

保持 ESP-IDF 环境处于激活状态。`release/` 除随仓库保存的 `README.md` 和 `release_notes.md` 外不能残留旧的生成文件：

```powershell
python scripts/package_release.py `
  --build-dir firmware/build `
  --output-dir release `
  --release-version v0.1-pre-flash
```

打包器读取并严格校验 `flasher_args.json`，用当前 ESP-IDF 环境中的 esptool 重新合并镜像，并输出：

- `merged-firmware.bin`
- 构建涉及的各个分区 `.bin`
- `flash_args` 与原始 `flasher_args.json`
- `manifest.json`（板卡身份、来源 revision、工具版本、文件大小与 SHA-256）
- `SHA256SUMS`

要获得可复现的 `generated_at`，构建前设置 `SOURCE_DATE_EPOCH`。发布目录包含来源与工具版本；相同源码、SDK 和构建配置才应比较二进制哈希。

## CI 结果

`config/ci/preflash.yml` 是未启用的自动构建模板。当前 GitHub CLI 登录未获得 `workflow` 权限，因此本次仅同步源码与发布文件，不创建活动工作流。

以后明确授权并完成 GitHub 权限配置后，可将该模板放到 `.github/workflows/preflash.yml`。启用后，它会在 push、PR 和手动运行时执行主机测试、ESP-IDF 6.1 完整构建和发布打包，并上传保留 14 天的 artifact。此次交付的实际验证环境是本机 ESP-IDF 6.0.1；不能把未执行的 CI 视为通过。

CI 或本地编译通过只说明代码和镜像结构通过检查，不等于真机验证。

## v0.1 离线验证记录

2026-10-02 在 Windows / ESP-IDF 6.0.1 完成：

- 固件主机测试：100 项通过。
- companion API 测试：7 项通过。
- 顶层发布与串口工具测试：12 项通过。
- canonical `lichuang-dev-dashboard` 完整构建通过。
- 应用镜像 2,855,104 bytes，app 分区剩余约 31%。
- assets 镜像 1,775,677 bytes，位于 8 MiB assets 分区容量内。
- 静态 DIRAM 158,229 / 341,760 bytes（46.3%）。

这些数字来自链接/镜像产物。PSRAM 实际容量、运行时峰值内存、屏幕、网络、音频和 OTA 仍需真机测量。
