# Xiaozhi Dashboard Companion

本地 companion 提供 SQLite 日程/工作日志存储、HTTP API、USB 串口同步和浏览器管理页。HTTP-only 模式只使用 Python 标准库；默认的 USB 模式还需要 `pyserial`。建议 Python 3.11 或更高版本，最低支持 Python 3.9。

## 启动

日常使用建议在项目根目录运行：

双 Type-C 板卡应接 USB 转 UART（CH340）接口，当前固件未通过另一原生 USB 接口同步数据。下例 `COM4` 是本机的 CH340 串口编号，其他电脑可能不同。

```powershell
python -m pip install pyserial
.\scripts\start_companion.ps1 -SerialPort COM4
```

`COM4` 替换为设备实际串口。`pyserial` 是 USB 模式唯一的额外 Python 依赖。脚本在同一进程中启动 Web 管理页和 USB 串口桥接，Web 服务默认只监听 `127.0.0.1:8765`。脚本还会创建或复用被 Git 忽略的 `config/dashboard.token`；浏览器首次打开管理页时，在“访问令牌”中粘贴该文件内容。设备与电脑不需要位于同一局域网。使用期间应保持 Type-C 线连接、电脑唤醒和进程运行。设备从 companion 自动取得 `Asia/Hong_Kong` 日期和时间，无需单独校时。

旧的 Wi-Fi HTTP 路径保留为手动配置的兼容模式。它要求设备与电脑处于同一可互访的局域网，并可同时指定配置串口和电脑的局域网地址：

```powershell
.\scripts\start_companion.ps1 `
  -ListenAddress 0.0.0.0 `
  -ConfigurePort COM7 `
  -DashboardUrl http://192.168.1.20:8765
```

也可以在 `companion` 目录直接启动服务。省略 `--serial-port` 时仅启动 Web/API；传入串口时会在同一进程中启动 USB 桥接：

```powershell
python dashboard_service.py
python dashboard_service.py --serial-port COM4 --serial-baud 115200
```

直接启动时默认监听 `127.0.0.1:8765`。打开 <http://127.0.0.1:8765/> 可管理记录并查看 320 × 240 设备布局预览。

日程和工作日志实际保存在项目内的 `companion/data/dashboard.db`。仓库初始没有这个 SQLite 文件；companion 第一次启动时才会创建它。若设置 `DASHBOARD_DB` 或传入 `--db`，则改为使用指定位置，例如：

```powershell
$env:DASHBOARD_DB = "D:\dashboard-data\dashboard.db"
python dashboard_service.py

python dashboard_service.py --db D:\dashboard-data\dashboard.db
```

使用 Wi-Fi HTTP 兼容模式时，建议设置随机 token，并监听所有网卡：

```powershell
$env:DASHBOARD_TOKEN = "请替换为足够长的随机值"
python dashboard_service.py --host 0.0.0.0
```

然后将 ESP32 的服务地址配置为电脑的局域网 IP，例如 `http://192.168.1.20:8765`，请求携带 `Authorization: Bearer <token>`。Windows 防火墙可能会在首次启动时询问是否允许专用网络访问。

只有 Wi-Fi HTTP 兼容模式要求设备与电脑处于同一可互访的局域网。该模式必须显式配置，不是 USB 断开后的自动故障切换；结果不确定的写入不会跨 USB/HTTP 重试。默认 USB 方式使用一根支持数据传输的 Type-C 线完成供电、烧录、串口配置和日程/日志同步；只有充电功能的线不行。关闭 PowerShell 窗口、停止进程、拔掉线或让电脑睡眠都会使设备暂时无法同步。

支持的配置：

| CLI 参数 | 环境变量 | 默认值 |
|---|---|---|
| `--host` | `DASHBOARD_HOST` | `127.0.0.1` |
| `--port` | `DASHBOARD_PORT` | `8765` |
| `--db` | `DASHBOARD_DB` | `companion/data/dashboard.db` |
| `--token` | `DASHBOARD_TOKEN` | 未设置（不鉴权） |
| `--serial-port` | — | 未设置（不启用 USB） |
| `--serial-baud` | — | `115200` |

token 不应写入仓库。优先使用环境变量；命令行参数可能出现在 shell 历史或进程信息里。绑定 `0.0.0.0` 时应启用 token。

## API 契约

日期使用 `YYYY-MM-DD`，时间使用 24 小时制 `HH:MM`。未传日期的 Dashboard 请求和 USB 同步都按 `Asia/Hong_Kong` 的当前日期处理；USB 响应还会为设备提供同一时区的当前时间。USB 与 HTTP 共用同一套校验和 SQLite 记录。所有 JSON 响应使用 UTF-8。配置 token 后，全部 `/api/*` 请求都必须携带 Bearer token。

### 读取一天的数据

```http
GET /api/dashboard?date=2026-10-02&limit=10&content_limit=500
```

`date` 可省略。`limit` 是每种类型最多返回的记录数，默认 10、最大 100；`content_limit` 是每条内容最多返回的字符数，默认 500、最大 10,000。浏览器管理页会请求最大值，ESP32 可使用较小值控制内存。响应：

```json
{
  "date": "2026-10-02",
  "schedules": [
    {"id": 1, "type": "schedule", "date": "2026-10-02", "time": "15:00", "title": "和 mentor 开会", "content": ""}
  ],
  "work_logs": [
    {"id": 2, "type": "log", "date": "2026-10-02", "time": null, "title": "完成 Dashboard API", "content": "通过本地测试"}
  ],
  "counts": {"schedules": 1, "work_logs": 1},
  "truncated": {"schedules": false, "work_logs": false, "content_ids": []}
}
```

日程优先按时间排序，无时间项随后；工作日志按可选时间和创建顺序排序。`counts` 是当天数据库中的完整数量。记录数量被 `limit` 截断时，相应布尔值为 `true`；内容被 `content_limit` 截断的记录 ID 会出现在 `content_ids` 中。

### 新增记录

```http
POST /api/entries
Content-Type: application/json

{
  "type": "schedule",
  "date": "2026-10-02",
  "time": "15:00",
  "title": "和 mentor 开会",
  "content": "讨论项目进度",
  "request_id": "voice-turn-20261002-001"
}
```

`type`、`date`、`title` 必填；`time`、`content`、`request_id` 可选。建议 ESP32 为一次 MCP 写入生成稳定且唯一的 `request_id`。首次创建返回 `201`：

```json
{"entry":{"id":1,"type":"schedule","date":"2026-10-02","time":"15:00","title":"和 mentor 开会","content":"讨论项目进度"},"idempotent":false}
```

相同 `request_id` 和相同规范化内容的重试不重复写入，返回原记录、`200` 和 `idempotent: true`。同一 `request_id` 携带不同内容返回 `409 idempotency_conflict`。

### 修改记录

```http
PATCH /api/entries/1
Content-Type: application/json

{"time":"15:30","content":"时间有调整"}
```

至少传一个可编辑字段；字段为 `type`、`date`、`time`、`title`、`content`。使用 `null` 或空字符串可清除 `time`。成功返回 `200` 和 `{"entry": ...}`。

### 删除记录

```http
DELETE /api/entries/1
```

成功返回 `204`，记录不存在返回 `404`。

### 健康检查

```http
GET /api/health
```

返回 `{"status":"ok"}`。启用 token 时同样需要鉴权。

### 错误格式与限制

错误统一为：

```json
{"error":{"code":"invalid_field","message":"time must use 24-hour HH:MM format","details":{"field":"time"}}}
```

JSON 请求体最大 64 KiB，读取超时为 10 秒；标题最大 200 字符；内容最大 10,000 字符；`request_id` 最大 128 字符。未知字段和无效 Unicode 会被拒绝，避免设备端拼写或编码错误被静默忽略。

## 测试

测试会启动真实的本地 HTTP 服务并使用临时 SQLite 数据库：

```powershell
python -m unittest discover -s tests -v
```

覆盖 HTTP CRUD、参数校验、Bearer 鉴权、重启持久化、USB 帧边界与 allowlist、USB/HTTP 共用 SQLite、香港时区日期和设备校时、请求体上限以及 `request_id` 幂等。
