# Xiaozhi Dashboard Companion

本地 companion 使用 Python 标准库提供 SQLite 日程/工作日志存储、HTTP API 和浏览器管理页。无需安装第三方包；建议 Python 3.11 或更高版本，最低支持 Python 3.9。

## 启动

在 `companion` 目录运行：

```powershell
python dashboard_service.py
```

默认监听 `127.0.0.1:8765`，数据写入 `data/dashboard.db`。打开 <http://127.0.0.1:8765/> 可管理记录并查看 320 × 240 设备布局预览。

ESP32 需要从局域网访问电脑时，建议设置随机 token，并监听所有网卡：

```powershell
$env:DASHBOARD_TOKEN = "请替换为足够长的随机值"
python dashboard_service.py --host 0.0.0.0
```

然后将 ESP32 的服务地址配置为电脑的局域网 IP，例如 `http://192.168.1.20:8765`，请求携带 `Authorization: Bearer <token>`。Windows 防火墙可能会在首次启动时询问是否允许专用网络访问。

支持的配置：

| CLI 参数 | 环境变量 | 默认值 |
|---|---|---|
| `--host` | `DASHBOARD_HOST` | `127.0.0.1` |
| `--port` | `DASHBOARD_PORT` | `8765` |
| `--db` | `DASHBOARD_DB` | `companion/data/dashboard.db` |
| `--token` | `DASHBOARD_TOKEN` | 未设置（不鉴权） |

token 不应写入仓库。优先使用环境变量；命令行参数可能出现在 shell 历史或进程信息里。绑定 `0.0.0.0` 时应启用 token。

## API 契约

日期使用 `YYYY-MM-DD`，时间使用 24 小时制 `HH:MM`。未传日期的 Dashboard 请求按 `Asia/Hong_Kong` 的当前日期处理。所有 JSON 响应使用 UTF-8。配置 token 后，全部 `/api/*` 请求都必须携带 Bearer token。

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

覆盖 CRUD、参数校验、Bearer 鉴权、重启持久化、香港时区默认日期、请求体上限和 `request_id` 幂等。
