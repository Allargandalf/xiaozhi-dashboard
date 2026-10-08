#ifndef LICHUANG_DASHBOARD_CLIENT_H
#define LICHUANG_DASHBOARD_CLIENT_H

#include "dashboard_types.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>
#include <mutex>
#include <string>

class DashboardDisplay;

class DashboardClient {
public:
    struct QueueResult {
        bool queued = false;
        std::string request_id;
        std::string error;
    };

    explicit DashboardClient(DashboardDisplay* display);

    bool Initialize();
    QueueResult QueueEntry(const std::string& type, const std::string& date,
                           const std::string& time, const std::string& title,
                           const std::string& content);
    void RequestRefresh();

    bool SaveConfig(const std::string& url, const std::string& token, std::string& error);
    bool ClearConfig(std::string& error);
    std::string GetConfigSummary() const;

private:
    enum class JobType : uint8_t {
        kRefresh,
        kAddEntry,
    };

    struct Job {
        JobType type = JobType::kRefresh;
        char entry_type[9] = {};
        char date[11] = {};
        char time[6] = {};
        char title[97] = {};
        char content[257] = {};
        char request_id[40] = {};
    };

    static void WorkerTaskEntry(void* arg);
    void WorkerTask();
    bool PostEntry(const Job& job);
    bool FetchDashboard();
    bool ReadResponse(class Http* http, std::string& body, std::string& error) const;
    bool ParseSnapshot(const std::string& body, dashboard::Snapshot& snapshot,
                       std::string& error) const;
    bool ValidatePostResponse(const std::string& body, const Job& job, std::string& error) const;
    void ReportSyncError(const std::string& detail);
    void ReportSnapshot(dashboard::Snapshot snapshot, bool via_usb = false);
    void LoadConfig();
    void CopyConfig(std::string& url, std::string& token) const;

    DashboardDisplay* display_;
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t worker_task_ = nullptr;
    std::atomic<bool> refresh_pending_ = false;
    mutable std::mutex config_mutex_;
    std::string service_url_;
    std::string bearer_token_;
    std::string last_write_problem_;
};

#endif  // LICHUANG_DASHBOARD_CLIENT_H
