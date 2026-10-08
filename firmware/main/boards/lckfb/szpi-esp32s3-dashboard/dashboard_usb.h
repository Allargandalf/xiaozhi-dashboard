#ifndef LICHUANG_DASHBOARD_USB_H
#define LICHUANG_DASHBOARD_USB_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

class DashboardUsb {
public:
    static DashboardUsb& GetInstance();
    bool Initialize(std::function<void()> on_connected);
    bool HandleLine(const std::string& line);
    bool IsConnected() const;
    // Called only from the low-priority Dashboard worker. One RPC in flight.
    bool Request(const char* method, const std::string& body, std::string& response, int& status);

private:
    SemaphoreHandle_t ready_ = nullptr;
    std::function<void()> on_connected_;
    std::atomic<uint32_t> last_seen_ms_{0};
    std::mutex request_mutex_;
    std::mutex response_mutex_;
    std::string pending_id_;
    std::string response_;
    int response_status_ = 0;
};

#endif
