#include "dashboard_usb.h"

#include "dashboard_usb_protocol.h"

#include <esp_random.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <sys/time.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
using JsonString = std::unique_ptr<char, decltype(&cJSON_free)>;
constexpr uint32_t kHeartbeatExpiryMs = 15000;
constexpr uint32_t kRequestTimeoutMs = 4000;
uint32_t NowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
}  // namespace

DashboardUsb& DashboardUsb::GetInstance() {
    static DashboardUsb transport;
    return transport;
}

bool DashboardUsb::Initialize(std::function<void()> on_connected) {
    if (ready_ == nullptr) {
        ready_ = xSemaphoreCreateBinary();
    }
    on_connected_ = std::move(on_connected);
    return ready_ != nullptr;
}

bool DashboardUsb::IsConnected() const {
    const uint32_t seen = last_seen_ms_.load();
    return ready_ != nullptr && seen != 0 && NowMs() - seen < kHeartbeatExpiryMs;
}

bool DashboardUsb::HandleLine(const std::string& line) {
    if (line == "dashboard_usb hello") {
        const bool was_connected = IsConnected();
        last_seen_ms_.store(NowMs());
        if (!was_connected && on_connected_) {
            on_connected_();
        }
        return true;
    }
    constexpr char prefix[] = "dashboard_rpc ";
    if (!line.starts_with(prefix)) {
        return false;
    }
    if (ready_ == nullptr || line.size() > dashboard::kMaxSerialFrameBytes) {
        return true;
    }
    const std::string_view payload(line.data() + sizeof(prefix) - 1,
                                   line.size() - sizeof(prefix) + 1);
    if (!dashboard::IsShallowSerialJson(payload)) {
        return true;
    }
    Json root(cJSON_Parse(line.c_str() + sizeof(prefix) - 1), cJSON_Delete);
    if (!root || !cJSON_IsObject(root.get())) {
        return true;
    }
    const cJSON* version = cJSON_GetObjectItemCaseSensitive(root.get(), "v");
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(root.get(), "id");
    const cJSON* status = cJSON_GetObjectItemCaseSensitive(root.get(), "status");
    const cJSON* body = cJSON_GetObjectItemCaseSensitive(root.get(), "body");
    if (!cJSON_IsNumber(version) || version->valuedouble != 1 || !cJSON_IsString(id) ||
        !cJSON_IsNumber(status) || status->valuedouble != status->valueint ||
        status->valueint < 200 || status->valueint > 599 || !cJSON_IsObject(body)) {
        return true;
    }
    std::lock_guard<std::mutex> lock(response_mutex_);
    if (pending_id_.empty() || pending_id_ != id->valuestring) {
        return true;  // A late or unrelated response cannot acknowledge a write.
    }
    JsonString serialized(cJSON_PrintUnformatted(body), cJSON_free);
    if (!serialized) {
        return true;
    }
    response_ = serialized.get();
    response_status_ = status->valueint;
    last_seen_ms_.store(NowMs());
    // Upstream stores local wall time as a shifted epoch (see Ota::CheckVersion).
    // Match that convention, using the companion's Asia/Hong_Kong clock.
    const cJSON* clock = cJSON_GetObjectItemCaseSensitive(root.get(), "local_timestamp");
    if (cJSON_IsNumber(clock) && clock->valuedouble >= 946684800.0 &&
        clock->valuedouble < 7258118400.0 && std::floor(clock->valuedouble) == clock->valuedouble) {
        timeval now = {static_cast<time_t>(clock->valuedouble), 0};
        settimeofday(&now, nullptr);
    }
    pending_id_.clear();
    xSemaphoreGive(ready_);
    return true;
}

bool DashboardUsb::Request(const char* method, const std::string& body, std::string& response,
                           int& status) {
    if (!IsConnected()) {
        return false;
    }
    std::lock_guard<std::mutex> request_lock(request_mutex_);
    Json request(cJSON_CreateObject(), cJSON_Delete);
    Json payload(cJSON_Parse(body.c_str()), cJSON_Delete);
    if (!request || !payload || !cJSON_IsObject(payload.get())) {
        return false;
    }
    char id[24];
    std::snprintf(id, sizeof(id), "%08lx%08lx", static_cast<unsigned long>(esp_random()),
                  static_cast<unsigned long>(esp_random()));
    cJSON_AddNumberToObject(request.get(), "v", 1);
    cJSON_AddStringToObject(request.get(), "id", id);
    cJSON_AddStringToObject(request.get(), "method", method);
    cJSON_AddItemToObject(request.get(), "body", payload.release());
    JsonString encoded(cJSON_PrintUnformatted(request.get()), cJSON_free);
    if (!encoded) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(response_mutex_);
        while (xSemaphoreTake(ready_, 0) == pdTRUE) {
        }
        pending_id_ = id;
        response_.clear();
        response_status_ = 0;
    }
    // One stdio call keeps a complete frame together with concurrent ESP logs.
    std::printf("\nDASHBOARD_RPC %s\n", encoded.get());
    std::fflush(stdout);
    const bool received = xSemaphoreTake(ready_, pdMS_TO_TICKS(kRequestTimeoutMs)) == pdTRUE;
    std::lock_guard<std::mutex> lock(response_mutex_);
    pending_id_.clear();
    if (!received) {
        return false;
    }
    response = std::move(response_);
    status = response_status_;
    return true;
}
