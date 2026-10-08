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
    std::string CycleDashboardPage();
    std::string SelectedDashboardView() const;
    std::string CurrentView();
    void ApplySnapshot(const dashboard::Snapshot& snapshot, const std::string& sync_status);
    void SetSyncStatus(const std::string& sync_status);

private:
    enum class DashboardPage { kOverview, kSchedule, kLog };

    void SyncViewToState();
    void SyncViewToStateLocked();
    void ShowSelectedPageLocked();
    void UpdateClockLocked();
    void UpdateSyncStatusLocked(const std::string& sync_status);
    void RenderSnapshotLocked(const dashboard::Snapshot& snapshot);
    static const char* PageName(DashboardPage page);
    static std::string FormatDate(const std::string& date);
    static std::string FormatMonthDay(const std::string& date);

    std::atomic<bool> dashboard_preferred_{true};
    std::atomic<DashboardPage> selected_page_{DashboardPage::kOverview};
    bool snapshot_received_ = false;
    dashboard::Snapshot cached_snapshot_;
    std::string cached_sync_status_;

    lv_obj_t* dashboard_root_ = nullptr;
    lv_obj_t* dashboard_date_label_ = nullptr;
    lv_obj_t* dashboard_clock_label_ = nullptr;
    lv_obj_t* overview_page_ = nullptr;
    lv_obj_t* schedule_page_ = nullptr;
    lv_obj_t* log_page_ = nullptr;

    lv_obj_t* overview_schedule_count_label_ = nullptr;
    lv_obj_t* overview_log_count_label_ = nullptr;
    lv_obj_t* schedule_count_label_ = nullptr;
    lv_obj_t* log_count_label_ = nullptr;
    lv_obj_t* overview_schedule_empty_label_ = nullptr;
    lv_obj_t* overview_log_empty_label_ = nullptr;
    lv_obj_t* schedule_empty_label_ = nullptr;
    lv_obj_t* log_empty_label_ = nullptr;

    std::array<lv_obj_t*, dashboard::kMaxVisibleEntriesPerSection> overview_schedule_rows_{};
    std::array<lv_obj_t*, dashboard::kMaxVisibleEntriesPerSection> overview_schedule_time_labels_{};
    std::array<lv_obj_t*, dashboard::kMaxVisibleEntriesPerSection>
        overview_schedule_title_labels_{};
    std::array<lv_obj_t*, dashboard::kMaxVisibleEntriesPerSection> overview_log_rows_{};
    std::array<lv_obj_t*, dashboard::kMaxVisibleEntriesPerSection> overview_log_date_labels_{};
    std::array<lv_obj_t*, dashboard::kMaxVisibleEntriesPerSection> overview_log_title_labels_{};

    std::array<lv_obj_t*, dashboard::kMaxVisibleEntriesPerSection> schedule_rows_{};
    std::array<lv_obj_t*, dashboard::kMaxVisibleEntriesPerSection> schedule_time_labels_{};
    std::array<lv_obj_t*, dashboard::kMaxVisibleEntriesPerSection> schedule_title_labels_{};
    std::array<lv_obj_t*, dashboard::kMaxVisibleEntriesPerSection> log_rows_{};
    std::array<lv_obj_t*, dashboard::kMaxVisibleEntriesPerSection> log_date_labels_{};
    std::array<lv_obj_t*, dashboard::kMaxVisibleEntriesPerSection> log_title_labels_{};

    lv_obj_t* page_name_label_ = nullptr;
    std::array<lv_obj_t*, 3> page_dots_{};
    lv_obj_t* sync_status_dot_ = nullptr;
    lv_obj_t* sync_status_label_ = nullptr;
};

#endif  // LICHUANG_DASHBOARD_DISPLAY_H
