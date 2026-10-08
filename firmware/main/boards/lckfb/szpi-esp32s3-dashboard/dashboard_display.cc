#include "dashboard_display.h"

#include "application.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace {
constexpr lv_coord_t kHeaderHeight = 42;
constexpr lv_coord_t kFooterHeight = 20;

constexpr uint32_t kBackground = 0x08131F;
constexpr uint32_t kSurface = 0x102331;
constexpr uint32_t kBorder = 0x1D3A47;
constexpr uint32_t kPrimaryText = 0xE7EEF3;
constexpr uint32_t kSecondaryText = 0x8EA5B3;
constexpr uint32_t kMutedText = 0x6F8794;
constexpr uint32_t kTeal = 0x2DD4BF;
constexpr uint32_t kTealSurface = 0x143439;
constexpr uint32_t kWarm = 0xF2B56B;

void MakeTransparent(lv_obj_t* object) {
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

void MakePanel(lv_obj_t* panel, lv_color_t background) {
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(kBorder), 0);
    lv_obj_set_style_bg_color(panel, background, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(panel, 7, 0);
    lv_obj_set_style_pad_row(panel, 3, 0);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* MakeSectionHeader(lv_obj_t* parent, const char* title, lv_color_t accent,
                            lv_obj_t** count_label) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 22);
    MakeTransparent(row);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t* title_label = lv_label_create(row);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, accent, 0);

    *count_label = lv_label_create(row);
    lv_label_set_text(*count_label, "0");
    lv_obj_set_style_text_color(*count_label, lv_color_hex(kSecondaryText), 0);
    return row;
}

void MakeDataRow(lv_obj_t* parent, lv_coord_t prefix_width, lv_color_t accent, lv_obj_t** row,
                 lv_obj_t** prefix_label, lv_obj_t** title_label) {
    *row = lv_obj_create(parent);
    lv_obj_set_size(*row, LV_PCT(100), 22);
    MakeTransparent(*row);
    lv_obj_set_style_pad_column(*row, 5, 0);
    lv_obj_set_flex_flow(*row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(*row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    *prefix_label = lv_label_create(*row);
    lv_obj_set_width(*prefix_label, prefix_width);
    lv_label_set_long_mode(*prefix_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(*prefix_label, accent, 0);

    *title_label = lv_label_create(*row);
    lv_obj_set_flex_grow(*title_label, 1);
    lv_label_set_long_mode(*title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(*title_label, lv_color_hex(kPrimaryText), 0);
}
}  // namespace

DashboardDisplay::DashboardDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                                   int width, int height, int offset_x, int offset_y, bool mirror_x,
                                   bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {}

void DashboardDisplay::SetupUI() {
    SpiLcdDisplay::SetupUI();

    DisplayLockGuard lock(this);
    if (!lock || dashboard_root_ != nullptr) {
        return;
    }

    dashboard_root_ = lv_obj_create(lv_screen_active());
    lv_obj_set_size(dashboard_root_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(dashboard_root_, 0, 0);
    lv_obj_set_style_border_width(dashboard_root_, 0, 0);
    lv_obj_set_style_bg_color(dashboard_root_, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(dashboard_root_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(dashboard_root_, 8, 0);
    lv_obj_set_style_pad_row(dashboard_root_, 6, 0);
    lv_obj_set_flex_flow(dashboard_root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(dashboard_root_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(dashboard_root_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* header = lv_obj_create(dashboard_root_);
    lv_obj_set_size(header, LV_PCT(100), kHeaderHeight);
    MakeTransparent(header);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t* date_group = lv_obj_create(header);
    lv_obj_set_height(date_group, LV_PCT(100));
    lv_obj_set_flex_grow(date_group, 1);
    MakeTransparent(date_group);
    lv_obj_set_style_pad_column(date_group, 8, 0);
    lv_obj_set_flex_flow(date_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(date_group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t* date_accent = lv_obj_create(date_group);
    lv_obj_set_size(date_accent, 3, 28);
    lv_obj_set_style_radius(date_accent, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(date_accent, 0, 0);
    lv_obj_set_style_bg_color(date_accent, lv_color_hex(kTeal), 0);
    lv_obj_set_style_bg_opa(date_accent, LV_OPA_COVER, 0);
    lv_obj_remove_flag(date_accent, LV_OBJ_FLAG_SCROLLABLE);

    dashboard_date_label_ = lv_label_create(date_group);
    lv_obj_set_flex_grow(dashboard_date_label_, 1);
    lv_label_set_long_mode(dashboard_date_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(dashboard_date_label_, lv_color_hex(kPrimaryText), 0);

    lv_obj_t* clock_box = lv_obj_create(header);
    lv_obj_set_size(clock_box, 82, 36);
    lv_obj_set_style_radius(clock_box, 9, 0);
    lv_obj_set_style_border_width(clock_box, 1, 0);
    lv_obj_set_style_border_color(clock_box, lv_color_hex(0x23605F), 0);
    lv_obj_set_style_bg_color(clock_box, lv_color_hex(kTealSurface), 0);
    lv_obj_set_style_bg_opa(clock_box, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(clock_box, 0, 0);
    lv_obj_remove_flag(clock_box, LV_OBJ_FLAG_SCROLLABLE);

    dashboard_clock_label_ = lv_label_create(clock_box);
    lv_obj_center(dashboard_clock_label_);
    lv_obj_set_style_text_color(dashboard_clock_label_, lv_color_hex(0xF4F7F9), 0);
    lv_obj_set_style_text_letter_space(dashboard_clock_label_, 1, 0);

    lv_obj_t* columns = lv_obj_create(dashboard_root_);
    lv_obj_set_width(columns, LV_PCT(100));
    lv_obj_set_flex_grow(columns, 1);
    MakeTransparent(columns);
    lv_obj_set_style_pad_column(columns, 6, 0);
    lv_obj_set_flex_flow(columns, LV_FLEX_FLOW_ROW);

    lv_obj_t* schedule_panel = lv_obj_create(columns);
    lv_obj_set_height(schedule_panel, LV_PCT(100));
    lv_obj_set_flex_grow(schedule_panel, 1);
    MakePanel(schedule_panel, lv_color_hex(kSurface));
    lv_obj_set_flex_flow(schedule_panel, LV_FLEX_FLOW_COLUMN);
    MakeSectionHeader(schedule_panel, "今日日程", lv_color_hex(kTeal), &schedule_count_label_);
    for (size_t index = 0; index < schedule_rows_.size(); ++index) {
        MakeDataRow(schedule_panel, 48, lv_color_hex(kTeal), &schedule_rows_[index],
                    &schedule_time_labels_[index], &schedule_title_labels_[index]);
        lv_obj_add_flag(schedule_rows_[index], LV_OBJ_FLAG_HIDDEN);
    }
    schedule_empty_label_ = lv_label_create(schedule_panel);
    lv_obj_set_width(schedule_empty_label_, LV_PCT(100));
    lv_obj_set_flex_grow(schedule_empty_label_, 1);
    lv_label_set_long_mode(schedule_empty_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(schedule_empty_label_, "等待日程同步");
    lv_obj_set_style_pad_top(schedule_empty_label_, 20, 0);
    lv_obj_set_style_text_align(schedule_empty_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(schedule_empty_label_, lv_color_hex(kMutedText), 0);

    lv_obj_t* log_panel = lv_obj_create(columns);
    lv_obj_set_height(log_panel, LV_PCT(100));
    lv_obj_set_flex_grow(log_panel, 1);
    MakePanel(log_panel, lv_color_hex(kSurface));
    lv_obj_set_flex_flow(log_panel, LV_FLEX_FLOW_COLUMN);
    MakeSectionHeader(log_panel, "工作记录", lv_color_hex(kWarm), &log_count_label_);
    for (size_t index = 0; index < log_rows_.size(); ++index) {
        MakeDataRow(log_panel, 48, lv_color_hex(kWarm), &log_rows_[index], &log_date_labels_[index],
                    &log_title_labels_[index]);
        lv_obj_add_flag(log_rows_[index], LV_OBJ_FLAG_HIDDEN);
    }
    log_empty_label_ = lv_label_create(log_panel);
    lv_obj_set_width(log_empty_label_, LV_PCT(100));
    lv_obj_set_flex_grow(log_empty_label_, 1);
    lv_label_set_long_mode(log_empty_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(log_empty_label_, "等待工作记录同步");
    lv_obj_set_style_pad_top(log_empty_label_, 20, 0);
    lv_obj_set_style_text_align(log_empty_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(log_empty_label_, lv_color_hex(kMutedText), 0);

    lv_obj_t* footer = lv_obj_create(dashboard_root_);
    lv_obj_set_size(footer, LV_PCT(100), kFooterHeight);
    MakeTransparent(footer);
    lv_obj_set_style_pad_column(footer, 6, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    sync_status_dot_ = lv_obj_create(footer);
    lv_obj_set_size(sync_status_dot_, 6, 6);
    lv_obj_set_style_radius(sync_status_dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(sync_status_dot_, 0, 0);
    lv_obj_set_style_bg_opa(sync_status_dot_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(sync_status_dot_, LV_OBJ_FLAG_SCROLLABLE);

    sync_status_label_ = lv_label_create(footer);
    lv_obj_set_width(sync_status_label_, LV_PCT(78));
    lv_label_set_long_mode(sync_status_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(sync_status_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(sync_status_label_, lv_color_hex(kSecondaryText), 0);

    UpdateClockLocked();
    UpdateSyncStatusLocked("连接电脑以同步");

    SyncViewToStateLocked();
}

void DashboardDisplay::SetStatus(const char* status) {
    SpiLcdDisplay::SetStatus(status);
    SyncViewToState();
}

void DashboardDisplay::UpdateStatusBar(bool update_all) {
    SpiLcdDisplay::UpdateStatusBar(update_all);
    DisplayLockGuard lock(this);
    if (lock) {
        UpdateClockLocked();
        SyncViewToStateLocked();
    }
}

std::string DashboardDisplay::SetPreferredView(const std::string& view) {
    dashboard_preferred_.store(view == "dashboard");
    SyncViewToState();
    const bool dashboard_visible = dashboard_preferred_.load() &&
                                   Application::GetInstance().GetDeviceState() == kDeviceStateIdle;
    return dashboard_visible ? "dashboard" : "assistant";
}

void DashboardDisplay::ApplySnapshot(const dashboard::Snapshot& snapshot,
                                     const std::string& sync_status) {
    DisplayLockGuard lock(this);
    if (!lock || dashboard_root_ == nullptr) {
        return;
    }
    RenderSnapshotLocked(snapshot);
    UpdateSyncStatusLocked(sync_status);
}

void DashboardDisplay::SetSyncStatus(const std::string& sync_status) {
    DisplayLockGuard lock(this);
    if (lock && sync_status_label_ != nullptr) {
        UpdateSyncStatusLocked(sync_status);
    }
}

void DashboardDisplay::SyncViewToState() {
    DisplayLockGuard lock(this);
    if (lock) {
        SyncViewToStateLocked();
    }
}

void DashboardDisplay::SyncViewToStateLocked() {
    if (dashboard_root_ == nullptr) {
        return;
    }
    const bool show_dashboard = dashboard_preferred_.load() &&
                                Application::GetInstance().GetDeviceState() == kDeviceStateIdle;
    if (show_dashboard) {
        lv_obj_remove_flag(dashboard_root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(dashboard_root_);
    } else {
        lv_obj_add_flag(dashboard_root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void DashboardDisplay::UpdateClockLocked() {
    if (dashboard_clock_label_ == nullptr || dashboard_date_label_ == nullptr) {
        return;
    }
    const time_t now = time(nullptr);
    struct tm local_time = {};
    if (localtime_r(&now, &local_time) != nullptr && local_time.tm_year >= 125) {
        char clock[6] = {};
        strftime(clock, sizeof(clock), "%H:%M", &local_time);
        lv_label_set_text(dashboard_clock_label_, clock);
        if (!snapshot_received_) {
            char date[11] = {};
            strftime(date, sizeof(date), "%Y-%m-%d", &local_time);
            const std::string formatted_date = FormatDate(date);
            lv_label_set_text(dashboard_date_label_, formatted_date.c_str());
        }
        return;
    }
    lv_label_set_text(dashboard_clock_label_, "待校时");
    if (!snapshot_received_) {
        lv_label_set_text(dashboard_date_label_, "日期待同步");
    }
}

void DashboardDisplay::UpdateSyncStatusLocked(const std::string& sync_status) {
    if (sync_status_label_ == nullptr || sync_status_dot_ == nullptr) {
        return;
    }
    const std::string label = sync_status.empty() ? "连接电脑以同步" : sync_status;
    lv_label_set_text(sync_status_label_, label.c_str());
    const bool synced = label.rfind("已同步", 0) == 0 || label.find("已连接") != std::string::npos;
    lv_obj_set_style_bg_color(sync_status_dot_, lv_color_hex(synced ? kTeal : kWarm), 0);
}

void DashboardDisplay::RenderSnapshotLocked(const dashboard::Snapshot& snapshot) {
    snapshot_received_ = true;
    const std::string date = FormatDate(snapshot.date);
    lv_label_set_text(dashboard_date_label_, date.c_str());

    char count[2] = {static_cast<char>('0' + std::min(snapshot.schedules.size(),
                                                      dashboard::kMaxVisibleEntriesPerSection)),
                     '\0'};
    lv_label_set_text(schedule_count_label_, count);
    const bool schedule_empty = snapshot.schedules.empty();
    if (schedule_empty) {
        lv_label_set_text(schedule_empty_label_, "今天没有安排");
        lv_obj_remove_flag(schedule_empty_label_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(schedule_empty_label_, LV_OBJ_FLAG_HIDDEN);
    }
    for (size_t index = 0; index < schedule_rows_.size(); ++index) {
        if (index < snapshot.schedules.size()) {
            const auto& entry = snapshot.schedules[index];
            lv_label_set_text(schedule_time_labels_[index],
                              entry.time.empty() ? "全天" : entry.time.c_str());
            lv_label_set_text(schedule_title_labels_[index], entry.title.c_str());
            lv_obj_remove_flag(schedule_rows_[index], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(schedule_rows_[index], LV_OBJ_FLAG_HIDDEN);
        }
    }

    count[0] = static_cast<char>(
        '0' + std::min(snapshot.work_logs.size(), dashboard::kMaxVisibleEntriesPerSection));
    lv_label_set_text(log_count_label_, count);
    const bool log_empty = snapshot.work_logs.empty();
    if (log_empty) {
        lv_label_set_text(log_empty_label_, "今天还没有工作记录");
        lv_obj_remove_flag(log_empty_label_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(log_empty_label_, LV_OBJ_FLAG_HIDDEN);
    }
    for (size_t index = 0; index < log_rows_.size(); ++index) {
        if (index < snapshot.work_logs.size()) {
            const auto& entry = snapshot.work_logs[index];
            const std::string entry_date = FormatMonthDay(entry.date);
            lv_label_set_text(log_date_labels_[index], entry_date.c_str());
            lv_label_set_text(log_title_labels_[index], entry.title.c_str());
            lv_obj_remove_flag(log_rows_[index], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(log_rows_[index], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

std::string DashboardDisplay::FormatDate(const std::string& date) {
    if (!dashboard::IsValidDate(date)) {
        return "日期待同步";
    }
    const int year = std::stoi(date.substr(0, 4));
    const int month = std::stoi(date.substr(5, 2));
    const int day = std::stoi(date.substr(8, 2));
    struct tm value = {};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_isdst = -1;
    mktime(&value);
    static constexpr const char* kWeekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
    char formatted[32] = {};
    std::snprintf(formatted, sizeof(formatted), "%02d月%02d日 周%s", month, day,
                  kWeekdays[value.tm_wday]);
    return formatted;
}

std::string DashboardDisplay::FormatMonthDay(const std::string& date) {
    if (!dashboard::IsValidDate(date)) {
        return "日期";
    }
    return date.substr(5, 2) + "/" + date.substr(8, 2);
}
