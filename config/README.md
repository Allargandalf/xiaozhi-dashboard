# 运行配置

v0.1 不把服务地址或密钥编译进个人固件，也不把密钥写入仓库。日常 USB 模式只在本机 `127.0.0.1:8765` 运行，不需要 token。只有显式启用局域网 HTTP 兼容模式时才需要配置服务地址和 token。设备端兼容配置写入 ESP32 的 `dashboard` NVS 命名空间，可通过串口随时修改，无需重新编译。

## 电脑端

直接运行 `dashboard_service.py` 时支持以下环境变量。日常使用的 `start_companion.ps1` 会显式传入监听地址、端口和数据库路径，并在默认回环模式清除继承的 `DASHBOARD_TOKEN`，防止旧环境设置改变本地行为：

| 变量 | 默认值 | 说明 |
| --- | --- | --- |
| `DASHBOARD_HOST` | `127.0.0.1` | 仅在显式启用局域网 HTTP 兼容模式时设为 `0.0.0.0` |
| `DASHBOARD_PORT` | `8765` | HTTP 监听端口 |
| `DASHBOARD_DB` | `companion/data/dashboard.db` | SQLite 文件位置 |
| `DASHBOARD_TOKEN` | 空 | 可选 Bearer token；默认本机模式保持为空 |

日常使用在项目根目录运行：

```powershell
.\scripts\start_companion.ps1 -SerialPort COM4
```

它在同一进程中启动 Web 管理页和 USB 桥接，明确使用 `companion/data/dashboard.db`。浏览器打开 <http://127.0.0.1:8765/>，不需要填写凭据；不要使用旧测试页的 `8771` 端口。只有主动传入 `-DatabasePath` 才会让该进程改用另一个 SQLite 文件。

只有手动改用 Wi-Fi HTTP 兼容模式时，才监听局域网、配置设备并启用持久 token：

```powershell
.\scripts\start_companion.ps1 `
  -ListenAddress 0.0.0.0 `
  -TokenFile .\config\dashboard.token `
  -ConfigurePort COM7 `
  -DashboardUrl http://192.168.1.23:8765
```

非回环监听时，脚本会生成或复用 token；`-TokenFile` 可明确指定保存位置。不要把 token 提交到仓库。需要轮换 token 时，用同一条带 `-ConfigurePort` / `-DashboardUrl` 的命令重新同步设备，确保服务和设备一致。

如果绕过启动脚本自行启用局域网模式，需要长期保存同一个 token，并让服务、ESP32 或其他 API 客户端使用相同值。本机管理页不提供凭据输入，应从 `http://127.0.0.1:8765/` 使用默认本机模式。

## ESP32 端

设备保存两个兼容模式键：`url` 与 `token`。默认的 USB 串口同步不读取电脑局域网地址，也不需要 token。只有手动启用 Wi-Fi HTTP 兼容模式时，才把 `url` 改成电脑的实际局域网 IP；构建回退值 `http://xiaozhi-dashboard.local:8765` 不能依赖，因为 v0.1 companion 尚未发布 mDNS 名称。

启动脚本已代为完成兼容模式配置。需要单独重配时，先从本地文件加载同一个 token，再执行：

```powershell
$env:DASHBOARD_TOKEN = (Get-Content -Raw config/dashboard.token).Trim()
python scripts/configure_device.py --port COM7 `
  --url http://192.168.1.23:8765 `
  --token-env DASHBOARD_TOKEN
```

脚本通过 115200 8N1 串口设置并回读配置，不打印 token。`--clear-token` 只适用于返回默认 USB/本机模式，不应用于暴露到局域网的 HTTP 服务。
