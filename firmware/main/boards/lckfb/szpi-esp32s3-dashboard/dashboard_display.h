#ifndef LICHUANG_DASHBOARD_DISPLAY_H
#define LICHUANG_DASHBOARD_DISPLAY_H

#include "dashboard_types.h"
#include "display/lcd_display.h"

#include <array>
#include <atomic>
#include <string>

class DashboardDisplay : public SpiLcdDisplay {
public:
    DashboardDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                     int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                     bool swap_xy);

    void SetupUI() override;
    void SetStatus(const char* status) override;
    void UpdateStatusBar(bool update_all = false) override;

    std::string SetPreferredView(const std::string& view);
    void ApplySnapshot(const dashboard::Snapshot& snapshot, const std::string& sync_status);
    void SetSyncStatus(const std::string& sync_status);

private:
    void SyncViewToState();
    void SyncViewToStateLocked();
    void UpdateClockLocked();
    void UpdateSyncStatusLocked(const std::string& sync_status);
    void RenderSnapshotLocked(const dashboard::Snapshot& snapshot);
    static std::string FormatDate(const std::string& date);
    static std::string FormatMonthDay(const std::string& date);

    std::atomic<bool> dashboard_preferred_{true};
    bool snapshot_received_ = false;
    lv_obj_t* dashboard_root_ = nullptr;
    lv_obj_t* dashboard_date_label_ = nullptr;
    lv_obj_t* dashboard_clock_label_ = nullptr;
    lv_obj_t* schedule_count_label_ = nullptr;
    lv_obj_t* log_count_label_ = nullptr;
    lv_obj_t* schedule_empty_label_ = nullptr;
    lv_obj_t* log_empty_label_ = nullptr;
    std::array<lv_obj_t*, dashboard::kMaxVisibleEntriesPerSection> schedule_rows_{};
    std::array<lv_obj_t*, dashboard::kMaxVisibleEntriesPerSection> schedule_time_labels_{};
    std::array<lv_obj_t*, dashboard::kMaxVisibleEntriesPerSection> schedule_title_labels_{};
    std::array<lv_obj_t*, dashboard::kMaxVisibleEntriesPerSection> log_rows_{};
    std::array<lv_obj_t*, dashboard::kMaxVisibleEntriesPerSection> log_date_labels_{};
    std::array<lv_obj_t*, dashboard::kMaxVisibleEntriesPerSection> log_title_labels_{};
    lv_obj_t* sync_status_dot_ = nullptr;
    lv_obj_t* sync_status_label_ = nullptr;
};

#endif  // LICHUANG_DASHBOARD_DISPLAY_H
