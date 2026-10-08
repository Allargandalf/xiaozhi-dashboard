#include "dashboard_client.h"

#include "application.h"
#include "board.h"
#include "dashboard_display.h"
#include "dashboard_protocol.h"
#include "dashboard_usb.h"
#include "http.h"
#include "settings.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <nvs.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>

namespace {
constexpr size_t kQueueDepth = 4;
constexpr uint32_t kWorkerStackSize = 7168;
constexpr UBaseType_t kWorkerPriority = 1;
constexpr int kHttpTimeoutMs = 4000;
constexpr TickType_t kRefreshInterval = pdMS_TO_TICKS(30000);
const char* TAG = "DashboardClient";

using CJsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
using CJsonStringPtr = std::unique_ptr<char, decltype(&cJSON_free)>;

std::string JoinUrl(const std::string& base, const char* path) {
    if (!base.empty() && base.back() == '/') {
        return base.substr(0, base.size() - 1) + path;
    }
    return base + path;
}

bool CopyBounded(char* destination, size_t capacity, const std::string& source) {
    if (source.size() >= capacity) {
        return false;
    }
    std::memcpy(destination, source.c_str(), source.size() + 1);
    return true;
}

std::string JsonString(const cJSON* object, const char* key) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) && value->valuestring != nullptr ? value->valuestring : "";
}

bool ParseEntries(const cJSON* array, const char* expected_type, std::vector<dashboard::Entry>& out,
                  std::string& error) {
    if (!cJSON_IsArray(array)) {
        error = std::string(expected_type) + " entries is not an array";
        return false;
    }
    const cJSON* item = nullptr;
    cJSON_ArrayForEach (item, array) {
        if (!cJSON_IsObject(item)) {
            error = std::string(expected_type) + " entry is not an object";
            return false;
        }
        dashboard::Entry entry;
        const cJSON* id = cJSON_GetObjectItemCaseSensitive(item, "id");
        const cJSON* type = cJSON_GetObjectItemCaseSensitive(item, "type");
        const cJSON* date = cJSON_GetObjectItemCaseSensitive(item, "date");
        const cJSON* time = cJSON_GetObjectItemCaseSensitive(item, "time");
        const cJSON* title = cJSON_GetObjectItemCaseSensitive(item, "title");
        const cJSON* content = cJSON_GetObjectItemCaseSensitive(item, "content");
        const bool valid_time = cJSON_IsString(time) || cJSON_IsNull(time);
        if (!dashboard::IsPositiveInteger(id) || !cJSON_IsString(type) || !cJSON_IsString(date) ||
            !valid_time || !cJSON_IsString(title) || !cJSON_IsString(content)) {
            error = std::string(expected_type) + " entry is missing a required field";
            return false;
        }
        entry.id = id->valueint;
        entry.type = JsonString(item, "type");
        entry.date = JsonString(item, "date");
        entry.time = cJSON_IsNull(time) ? "" : JsonString(item, "time");
        entry.title = JsonString(item, "title");
        entry.content = JsonString(item, "content");
        if (entry.type != expected_type || !dashboard::IsValidDate(entry.date) ||
            !dashboard::IsValidTime(entry.time) || entry.title.empty()) {
            error = std::string(expected_type) + " entry has an invalid value";
            return false;
        }
        if (out.size() < dashboard::kMaxVisibleEntriesPerSection) {
            out.push_back(std::move(entry));
        }
    }
    return true;
}

std::string CurrentSyncLabel() {
    time_t now = time(nullptr);
    struct tm local_time = {};
    char buffer[32] = "已同步";
    if (localtime_r(&now, &local_time) != nullptr && local_time.tm_year >= 125) {
        strftime(buffer, sizeof(buffer), "已同步 %H:%M", &local_time);
    }
    return buffer;
}
}  // namespace

DashboardClient::DashboardClient(DashboardDisplay* display) : display_(display) { LoadConfig(); }

bool DashboardClient::Initialize() {
    if (queue_ != nullptr) {
        return true;
    }
    queue_ = xQueueCreate(kQueueDepth, sizeof(Job));
    if (queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create dashboard work queue");
        return false;
    }
    if (!DashboardUsb::GetInstance().Initialize([this]() { RequestRefresh(); })) {
        ESP_LOGE(TAG, "Failed to initialize dashboard USB transport");
        vQueueDelete(queue_);
        queue_ = nullptr;
        return false;
    }
    if (xTaskCreate(WorkerTaskEntry, "dashboard_sync", kWorkerStackSize, this, kWorkerPriority,
                    &worker_task_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create dashboard worker");
        vQueueDelete(queue_);
        queue_ = nullptr;
        return false;
    }
    return true;
}

DashboardClient::QueueResult DashboardClient::QueueEntry(const std::string& type,
                                                         const std::string& date,
                                                         const std::string& time,
                                                         const std::string& title,
                                                         const std::string& content) {
    QueueResult result;
    if (!dashboard::IsValidType(type)) {
        result.error = "type must be schedule or log";
        return result;
    }
    if (!dashboard::IsValidDate(date)) {
        result.error = "date must be a real YYYY-MM-DD date between 2000 and 2199";
        return result;
    }
    if (!dashboard::IsValidTime(time)) {
        result.error = "time must be empty or HH:MM";
        return result;
    }
    const std::string normalized_title = dashboard::TrimTitle(title);
    if (normalized_title.empty()) {
        result.error = "title must not be empty";
        return result;
    }
    if (queue_ == nullptr) {
        result.error = "dashboard worker is unavailable";
        return result;
    }
    std::string configured_url;
    std::string configured_token;
    CopyConfig(configured_url, configured_token);
    if (!DashboardUsb::GetInstance().IsConnected() &&
        configured_url == "http://xiaozhi-dashboard.local:8765") {
        result.error = "computer service is not connected; start the companion USB bridge";
        return result;
    }

    Job job;
    job.type = JobType::kAddEntry;
    const unsigned long random_a = static_cast<unsigned long>(esp_random());
    const unsigned long random_b = static_cast<unsigned long>(esp_random());
    std::snprintf(job.request_id, sizeof(job.request_id), "%08lx-%08lx", random_a, random_b);
    if (!CopyBounded(job.entry_type, sizeof(job.entry_type), type) ||
        !CopyBounded(job.date, sizeof(job.date), date) ||
        !CopyBounded(job.time, sizeof(job.time), time) ||
        !CopyBounded(job.title, sizeof(job.title), normalized_title) ||
        !CopyBounded(job.content, sizeof(job.content), content)) {
        result.error = "entry field exceeds the device limit";
        return result;
    }
    if (xQueueSend(queue_, &job, 0) != pdTRUE) {
        result.error = "dashboard write queue is full; entry was not saved";
        return result;
    }
    result.queued = true;
    result.request_id = job.request_id;
    return result;
}

void DashboardClient::RequestRefresh() {
    if (queue_ == nullptr) {
        return;
    }
    bool expected = false;
    if (!refresh_pending_.compare_exchange_strong(expected, true)) {
        return;
    }
    if (uxQueueMessagesWaiting(queue_) >= kQueueDepth - 1) {
        refresh_pending_.store(false);
        return;
    }
    Job job;
    job.type = JobType::kRefresh;
    if (xQueueSend(queue_, &job, 0) != pdTRUE) {
        refresh_pending_.store(false);
    }
}

bool DashboardClient::SaveConfig(const std::string& url, const std::string& token,
                                 std::string& error) {
    if (!dashboard::IsHttpUrl(url) || url.size() > 192 || token.size() > 192) {
        error = "invalid_url";
        return false;
    }
    nvs_handle_t handle = 0;
    esp_err_t status = nvs_open("dashboard", NVS_READWRITE, &handle);
    if (status == ESP_OK) {
        status = nvs_set_str(handle, "url", url.c_str());
    }
    if (status == ESP_OK) {
        status = token.empty() ? nvs_erase_key(handle, "token")
                               : nvs_set_str(handle, "token", token.c_str());
        if (status == ESP_ERR_NVS_NOT_FOUND && token.empty()) {
            status = ESP_OK;
        }
    }
    if (status == ESP_OK) {
        status = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    if (status != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save dashboard config: %s", esp_err_to_name(status));
        error = "save_failed";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        service_url_ = url;
        bearer_token_ = token;
    }
    RequestRefresh();
    return true;
}

bool DashboardClient::ClearConfig(std::string& error) {
    nvs_handle_t handle = 0;
    esp_err_t status = nvs_open("dashboard", NVS_READWRITE, &handle);
    if (status == ESP_OK) {
        status = nvs_erase_all(handle);
    }
    if (status == ESP_OK) {
        status = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    if (status != ESP_OK) {
        error = "save_failed";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        service_url_ = CONFIG_DASHBOARD_SERVICE_URL;
        bearer_token_ = CONFIG_DASHBOARD_SERVICE_TOKEN;
    }
    RequestRefresh();
    return true;
}

std::string DashboardClient::GetConfigSummary() const {
    std::string url;
    std::string token;
    CopyConfig(url, token);
    CJsonPtr json(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddBoolToObject(json.get(), "ok", true);
    cJSON_AddStringToObject(json.get(), "url", url.c_str());
    cJSON_AddBoolToObject(json.get(), "token_set", !token.empty());
    cJSON_AddBoolToObject(json.get(), "usb_connected", DashboardUsb::GetInstance().IsConnected());
    CJsonStringPtr output(cJSON_PrintUnformatted(json.get()), cJSON_free);
    return output != nullptr ? output.get() : R"({"ok":false,"error":"save_failed"})";
}

void DashboardClient::WorkerTaskEntry(void* arg) {
    static_cast<DashboardClient*>(arg)->WorkerTask();
}

void DashboardClient::WorkerTask() {
    vTaskDelay(pdMS_TO_TICKS(3000));
    FetchDashboard();
    while (true) {
        Job job;
        const TickType_t interval =
            DashboardUsb::GetInstance().IsConnected() ? pdMS_TO_TICKS(5000) : kRefreshInterval;
        if (xQueueReceive(queue_, &job, interval) == pdTRUE) {
            if (job.type == JobType::kAddEntry) {
                if (PostEntry(job)) {
                    FetchDashboard();
                }
            } else {
                refresh_pending_.store(false);
                FetchDashboard();
            }
        } else {
            FetchDashboard();
        }
    }
}

bool DashboardClient::PostEntry(const Job& job) {
    std::string service_url;
    std::string token;
    CopyConfig(service_url, token);

    CJsonPtr json(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddStringToObject(json.get(), "type", job.entry_type);
    cJSON_AddStringToObject(json.get(), "date", job.date);
    cJSON_AddStringToObject(json.get(), "time", job.time);
    cJSON_AddStringToObject(json.get(), "title", job.title);
    cJSON_AddStringToObject(json.get(), "content", job.content);
    cJSON_AddStringToObject(json.get(), "request_id", job.request_id);
    CJsonStringPtr body(cJSON_PrintUnformatted(json.get()), cJSON_free);
    if (body == nullptr) {
        ReportSyncError("写入编码失败");
        return false;
    }

    // Keep one transport for both attempts. A lost USB confirmation must not
    // silently write to a different computer/database through HTTP fallback.
    if (DashboardUsb::GetInstance().IsConnected()) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            std::string response;
            std::string error;
            int status = 0;
            if (!DashboardUsb::GetInstance().Request("entries.add", body.get(), response, status)) {
                continue;
            }
            if (status >= 400 && status < 500) {
                last_write_problem_ = "上次写入被拒绝";
                ReportSyncError("");
                return false;
            }
            if (status >= 200 && status < 300 && ValidatePostResponse(response, job, error)) {
                last_write_problem_.clear();
                ReportSyncError("USB 写入已保存");
                return true;
            }
        }
        last_write_problem_ = "上次写入结果未知";
        ReportSyncError("");
        return false;
    }
    if (service_url == "http://xiaozhi-dashboard.local:8765") {
        last_write_problem_ = "上次写入未保存";
        ReportSyncError("连接电脑以同步");
        return false;
    }

    const std::string url = JoinUrl(service_url, "/api/entries");
    for (int attempt = 0; attempt < 2; ++attempt) {
        auto http = Board::GetInstance().GetNetwork()->CreateHttp(0);
        if (http == nullptr) {
            continue;
        }
        http->SetTimeout(kHttpTimeoutMs);
        http->SetHeader("Content-Type", "application/json");
        http->SetHeader("Accept", "application/json");
        if (!token.empty()) {
            http->SetHeader("Authorization", "Bearer " + token);
        }
        http->SetContent(std::string(body.get()));
        auto opened = http->Open("POST", url);
        if (!opened) {
            ESP_LOGW(TAG, "POST %s attempt %d failed: %s", url.c_str(), attempt + 1,
                     opened.error().ToString().c_str());
            http->Close();
            continue;
        }
        auto status = http->GetStatusCode();
        if (status && *status >= 400 && *status < 500) {
            ESP_LOGW(TAG, "POST %s was rejected with %d", url.c_str(), *status);
            http->Close();
            last_write_problem_ = "上次写入被拒绝";
            ReportSyncError("");
            return false;
        }
        if (!status || *status < 200 || *status >= 300) {
            ESP_LOGW(TAG, "POST %s attempt %d returned %d", url.c_str(), attempt + 1,
                     status ? *status : -1);
            http->Close();
            continue;
        }
        std::string response;
        std::string error;
        const bool read_ok = ReadResponse(http.get(), response, error);
        http->Close();
        if (read_ok && ValidatePostResponse(response, job, error)) {
            last_write_problem_.clear();
            ReportSyncError("写入已保存，正在同步");
            return true;
        }
        ESP_LOGW(TAG, "POST %s response was not a confirmation: %s", url.c_str(), error.c_str());
    }
    last_write_problem_ = "上次写入结果未知";
    ReportSyncError("");
    return false;
}

bool DashboardClient::FetchDashboard() {
    std::string service_url;
    std::string token;
    CopyConfig(service_url, token);
    if (DashboardUsb::GetInstance().IsConnected()) {
        std::string body;
        std::string error;
        int status = 0;
        if (!DashboardUsb::GetInstance().Request("dashboard.get", "{}", body, status)) {
            ReportSyncError("连接电脑以同步");
            return false;
        }
        dashboard::Snapshot snapshot;
        if (status != 200 || !ParseSnapshot(body, snapshot, error)) {
            ReportSyncError("USB 数据同步失败");
            return false;
        }
        ReportSnapshot(std::move(snapshot), true);
        return true;
    }
    if (service_url == "http://xiaozhi-dashboard.local:8765") {
        ReportSyncError("连接电脑以同步");
        return false;
    }
    const std::string url = JoinUrl(service_url, "/api/dashboard?limit=4&content_limit=32");
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(0);
    if (http == nullptr) {
        ReportSyncError("网络不可用");
        return false;
    }
    http->SetTimeout(kHttpTimeoutMs);
    http->SetHeader("Accept", "application/json");
    if (!token.empty()) {
        http->SetHeader("Authorization", "Bearer " + token);
    }
    auto opened = http->Open("GET", url);
    if (!opened) {
        ESP_LOGD(TAG, "GET %s failed: %s", url.c_str(), opened.error().ToString().c_str());
        http->Close();
        ReportSyncError("电脑服务未连接");
        return false;
    }
    auto status = http->GetStatusCode();
    if (!status || *status < 200 || *status >= 300) {
        ESP_LOGW(TAG, "GET %s returned %d", url.c_str(), status ? *status : -1);
        http->Close();
        ReportSyncError("同步失败");
        return false;
    }
    std::string body;
    std::string error;
    if (!ReadResponse(http.get(), body, error)) {
        http->Close();
        ReportSyncError(error);
        return false;
    }
    http->Close();

    dashboard::Snapshot snapshot;
    if (!ParseSnapshot(body, snapshot, error)) {
        ESP_LOGW(TAG, "Invalid dashboard response: %s", error.c_str());
        ReportSyncError("数据格式错误");
        return false;
    }
    ReportSnapshot(std::move(snapshot));
    return true;
}

bool DashboardClient::ReadResponse(Http* http, std::string& body, std::string& error) const {
    const size_t declared_length = http->GetBodyLength();
    if (declared_length > dashboard::kMaxResponseBytes) {
        error = "数据过大";
        return false;
    }
    body.clear();
    body.reserve(std::min(declared_length, dashboard::kMaxResponseBytes));
    std::array<char, 512> buffer;
    while (true) {
        auto read = http->Read(buffer.data(), buffer.size());
        if (!read) {
            ESP_LOGW(TAG, "Dashboard HTTP read failed: %s", read.error().ToString().c_str());
            error = "读取失败";
            return false;
        }
        if (*read == 0) {
            return true;
        }
        if (body.size() + *read > dashboard::kMaxResponseBytes) {
            error = "数据过大";
            return false;
        }
        body.append(buffer.data(), *read);
    }
}

bool DashboardClient::ParseSnapshot(const std::string& body, dashboard::Snapshot& snapshot,
                                    std::string& error) const {
    CJsonPtr root(cJSON_ParseWithLength(body.data(), body.size()), cJSON_Delete);
    if (root == nullptr || !cJSON_IsObject(root.get())) {
        error = "root is not an object";
        return false;
    }
    snapshot.date = JsonString(root.get(), "date");
    if (!dashboard::IsValidDate(snapshot.date)) {
        error = "date is invalid";
        return false;
    }
    std::vector<dashboard::Entry> schedules;
    std::vector<dashboard::Entry> work_logs;
    if (!ParseEntries(cJSON_GetObjectItemCaseSensitive(root.get(), "schedules"), "schedule",
                      schedules, error) ||
        !ParseEntries(cJSON_GetObjectItemCaseSensitive(root.get(), "work_logs"), "log", work_logs,
                      error)) {
        return false;
    }
    snapshot.schedules = std::move(schedules);
    snapshot.work_logs = std::move(work_logs);
    return true;
}

bool DashboardClient::ValidatePostResponse(const std::string& body, const Job& job,
                                           std::string& error) const {
    dashboard::Entry expected;
    expected.type = job.entry_type;
    expected.date = job.date;
    expected.time = job.time;
    expected.title = job.title;
    expected.content = job.content;
    return dashboard::ValidatePostConfirmation(body, expected, error);
}

void DashboardClient::ReportSyncError(const std::string& detail) {
    std::string status = detail;
    if (!last_write_problem_.empty() && status != last_write_problem_) {
        if (!status.empty()) {
            status += " · ";
        }
        status += last_write_problem_;
    }
    Application::GetInstance().Schedule(
        [display = display_, status = std::move(status)]() { display->SetSyncStatus(status); });
}

void DashboardClient::ReportSnapshot(dashboard::Snapshot snapshot, bool via_usb) {
    std::string status = via_usb ? "USB 已连接" : CurrentSyncLabel();
    if (!last_write_problem_.empty()) {
        status += " · " + last_write_problem_;
    }
    Application::GetInstance().Schedule(
        [display = display_, snapshot = std::move(snapshot), status = std::move(status)]() {
            display->ApplySnapshot(snapshot, status);
        });
}

void DashboardClient::LoadConfig() {
    Settings settings("dashboard", false);
    std::lock_guard<std::mutex> lock(config_mutex_);
    service_url_ = settings.GetString("url", CONFIG_DASHBOARD_SERVICE_URL);
    bearer_token_ = settings.GetString("token", CONFIG_DASHBOARD_SERVICE_TOKEN);
    if (!dashboard::IsHttpUrl(service_url_)) {
        service_url_ = CONFIG_DASHBOARD_SERVICE_URL;
    }
}

void DashboardClient::CopyConfig(std::string& url, std::string& token) const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    url = service_url_;
    token = bearer_token_;
}
