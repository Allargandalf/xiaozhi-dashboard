# 运行配置

v0.1 不把服务地址或密钥编译进个人固件，也不把密钥写入仓库。电脑端通过环境变量配置；设备端配置写入 ESP32 的 `dashboard` NVS 命名空间，可通过串口随时修改，无需重新编译。

## 电脑端

支持以下环境变量：

| 变量 | 默认值 | 说明 |
| --- | --- | --- |
| `DASHBOARD_HOST` | `127.0.0.1` | 供 ESP32 访问时设为 `0.0.0.0` |
| `DASHBOARD_PORT` | `8765` | HTTP 监听端口 |
| `DASHBOARD_DB` | `companion/data/dashboard.db` | SQLite 文件位置 |
| `DASHBOARD_TOKEN` | 空 | Bearer token；监听局域网时应设置 |

推荐用启动脚本创建并复用本地 token。首次烧录后可在启动服务前顺便配置设备：

```powershell
.\scripts\start_companion.ps1 `
  -ConfigurePort COM7 `
  -DashboardUrl http://192.168.1.23:8765
```

脚本把随机 token 保存到已被 Git 忽略的 `config/dashboard.token`，配置设备后启动前台服务。后续重启只运行 `.\scripts\start_companion.ps1`，会复用相同 token。不要删除 token 文件后直接重启服务，否则设备保存的旧 token 将无法鉴权；需要换 token 时，用同一条带 `-ConfigurePort` / `-DashboardUrl` 的命令重新同步设备。

如果不使用脚本，必须自行长期保存同一个 token，在配置设备和每次启动服务前读入同一环境变量。不要把真实 token 提交到仓库。

## ESP32 端

设备保存两个键：`url` 与 `token`。默认 URL 是 `http://xiaozhi-dashboard.local:8765`，但 v0.1 的电脑服务尚未发布 mDNS 名称，所以首次真机调试必须改成电脑的局域网 IP。

启动脚本已代为完成下面的串口命令。需要单独重配时，先从本地文件加载同一个 token，再执行：

```powershell
$env:DASHBOARD_TOKEN = (Get-Content -Raw config/dashboard.token).Trim()
python scripts/configure_device.py --port COM7 `
  --url http://192.168.1.23:8765 `
  --token-env DASHBOARD_TOKEN
```

脚本通过 115200 8N1 串口设置并回读配置，不打印 token。若确实要关闭鉴权，显式使用 `--clear-token`。
