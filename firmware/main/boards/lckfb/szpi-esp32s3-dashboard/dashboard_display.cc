#include "dashboard_display.h"

#include "application.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace {
constexpr lv_coord_t kHeaderHeight = 38;
constexpr lv_coord_t kContentHeight = 151;
constexpr lv_coord_t kFooterHeight = 23;

constexpr uint32_t kBackground = 0xF7F3EA;
constexpr uint32_t kSurface = 0xFFFFFF;
constexpr uint32_t kBorder = 0xDED8CB;
constexpr uint32_t kPrimaryText = 0x27302D;
constexpr uint32_t kSecondaryText = 0x68736F;
constexpr uint32_t kMutedText = 0x99A09D;
constexpr uint32_t kSchedule = 0x2F7D67;
constexpr uint32_t kScheduleTime = 0x3B6FA7;
constexpr uint32_t kScheduleSurface = 0xE8F4EE;
constexpr uint32_t kLog = 0xB8673E;
constexpr uint32_t kLogSurface = 0xF8ECE3;

void MakeTransparent(lv_obj_t* object) {
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

void MakePanel(lv_obj_t* panel) {
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(kBorder), 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(kSurface), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(panel, 6, 0);
    lv_obj_set_style_pad_row(panel, 2, 0);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* MakePage(lv_obj_t* parent) {
    lv_obj_t* page = lv_obj_create(parent);
    lv_obj_set_size(page, LV_PCT(100), kContentHeight);
    MakeTransparent(page);
    return page;
}

void MakeSectionHeader(lv_obj_t* parent, const char* title, lv_color_t accent,
                       lv_obj_t** count_label) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 23);
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
}

void MakeDataRow(lv_obj_t* parent, lv_coord_t height, lv_coord_t prefix_width,
                 lv_color_t prefix_color, lv_color_t background, bool filled, lv_obj_t** row,
                 lv_obj_t** prefix_label, lv_obj_t** title_label) {
    *row = lv_obj_create(parent);
    lv_obj_set_size(*row, LV_PCT(100), height);
    MakeTransparent(*row);
    if (filled) {
        lv_obj_set_style_radius(*row, 6, 0);
        lv_obj_set_style_bg_color(*row, background, 0);
        lv_obj_set_style_bg_opa(*row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(*row, 6, 0);
        lv_obj_set_style_pad_right(*row, 6, 0);
    }
    lv_obj_set_style_pad_column(*row, 5, 0);
    lv_obj_set_flex_flow(*row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(*row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    *prefix_label = lv_label_create(*row);
    lv_obj_set_width(*prefix_label, prefix_width);
    lv_label_set_long_mode(*prefix_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(*prefix_label, prefix_color, 0);

    *title_label = lv_label_create(*row);
    lv_obj_set_flex_grow(*title_label, 1);
    lv_label_set_long_mode(*title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(*title_label, lv_color_hex(kPrimaryText), 0);
}

void MakeEmptyLabel(lv_obj_t* parent, const char* text, lv_obj_t** label) {
    *label = lv_label_create(parent);
    lv_obj_set_width(*label, LV_PCT(100));
    lv_obj_set_flex_grow(*label, 1);
    lv_label_set_long_mode(*label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(*label, text);
    lv_obj_set_style_pad_top(*label, 18, 0);
    lv_obj_set_style_text_align(*label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(*label, lv_color_hex(kMutedText), 0);
}

void SetCount(lv_obj_t* label, size_t count) {
    char text[2] = {
        static_cast<char>('0' + std::min(count, dashboard::kMaxVisibleEntriesPerSection)), '\0'};
    lv_label_set_text(label, text);
}

std::string TitleWithContent(const dashboard::Entry& entry) {
    if (entry.content.empty()) {
        return entry.title;
    }
    return entry.title + " · " + entry.content;
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
    lv_obj_set_style_text_color(dashboard_root_, lv_color_hex(kPrimaryText), 0);
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
    lv_obj_set_size(date_accent, 3, 26);
    lv_obj_set_style_radius(date_accent, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(date_accent, 0, 0);
    lv_obj_set_style_bg_color(date_accent, lv_color_hex(kSchedule), 0);
    lv_obj_set_style_bg_opa(date_accent, LV_OPA_COVER, 0);
    lv_obj_remove_flag(date_accent, LV_OBJ_FLAG_SCROLLABLE);

    dashboard_date_label_ = lv_label_create(date_group);
    lv_obj_set_flex_grow(dashboard_date_label_, 1);
    lv_label_set_long_mode(dashboard_date_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(dashboard_date_label_, lv_color_hex(kPrimaryText), 0);

    lv_obj_t* clock_box = lv_obj_create(header);
    lv_obj_set_size(clock_box, 76, 32);
    lv_obj_set_style_radius(clock_box, 9, 0);
    lv_obj_set_style_border_width(clock_box, 1, 0);
    lv_obj_set_style_border_color(clock_box, lv_color_hex(0xB8D5C8), 0);
    lv_obj_set_style_bg_color(clock_box, lv_color_hex(kScheduleSurface), 0);
    lv_obj_set_style_bg_opa(clock_box, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(clock_box, 0, 0);
    lv_obj_remove_flag(clock_box, LV_OBJ_FLAG_SCROLLABLE);

    dashboard_clock_label_ = lv_label_create(clock_box);
    lv_obj_center(dashboard_clock_label_);
    lv_obj_set_style_text_color(dashboard_clock_label_, lv_color_hex(kPrimaryText), 0);
    lv_obj_set_style_text_letter_space(dashboard_clock_label_, 1, 0);

    overview_page_ = MakePage(dashboard_root_);
    lv_obj_set_style_pad_column(overview_page_, 6, 0);
    lv_obj_set_flex_flow(overview_page_, LV_FLEX_FLOW_ROW);

    lv_obj_t* overview_schedule_panel = lv_obj_create(overview_page_);
    lv_obj_set_height(overview_schedule_panel, LV_PCT(100));
    lv_obj_set_flex_grow(overview_schedule_panel, 1);
    MakePanel(overview_schedule_panel);
    lv_obj_set_flex_flow(overview_schedule_panel, LV_FLEX_FLOW_COLUMN);
    MakeSectionHeader(overview_schedule_panel, "今日日程", lv_color_hex(kSchedule),
                      &overview_schedule_count_label_);
    for (size_t index = 0; index < overview_schedule_rows_.size(); ++index) {
        MakeDataRow(overview_schedule_panel, 23, 44, lv_color_hex(kScheduleTime),
                    lv_color_hex(kScheduleSurface), false, &overview_schedule_rows_[index],
                    &overview_schedule_time_labels_[index],
                    &overview_schedule_title_labels_[index]);
        lv_obj_add_flag(overview_schedule_rows_[index], LV_OBJ_FLAG_HIDDEN);
    }
    MakeEmptyLabel(overview_schedule_panel, "等待日程同步", &overview_schedule_empty_label_);

    lv_obj_t* overview_log_panel = lv_obj_create(overview_page_);
    lv_obj_set_height(overview_log_panel, LV_PCT(100));
    lv_obj_set_flex_grow(overview_log_panel, 1);
    MakePanel(overview_log_panel);
    lv_obj_set_flex_flow(overview_log_panel, LV_FLEX_FLOW_COLUMN);
    MakeSectionHeader(overview_log_panel, "工作记录", lv_color_hex(kLog),
                      &overview_log_count_label_);
    for (size_t index = 0; index < overview_log_rows_.size(); ++index) {
        MakeDataRow(overview_log_panel, 23, 44, lv_color_hex(kLog), lv_color_hex(kLogSurface),
                    false, &overview_log_rows_[index], &overview_log_date_labels_[index],
                    &overview_log_title_labels_[index]);
        lv_obj_add_flag(overview_log_rows_[index], LV_OBJ_FLAG_HIDDEN);
    }
    MakeEmptyLabel(overview_log_panel, "等待工作记录同步", &overview_log_empty_label_);

    schedule_page_ = MakePage(dashboard_root_);
    lv_obj_t* schedule_panel = lv_obj_create(schedule_page_);
    lv_obj_set_size(schedule_panel, LV_PCT(100), LV_PCT(100));
    MakePanel(schedule_panel);
    lv_obj_set_flex_flow(schedule_panel, LV_FLEX_FLOW_COLUMN);
    MakeSectionHeader(schedule_panel, "今日日程", lv_color_hex(kSchedule), &schedule_count_label_);
    for (size_t index = 0; index < schedule_rows_.size(); ++index) {
        MakeDataRow(schedule_panel, 26, 52, lv_color_hex(kScheduleTime),
                    lv_color_hex(kScheduleSurface), true, &schedule_rows_[index],
                    &schedule_time_labels_[index], &schedule_title_labels_[index]);
        lv_obj_add_flag(schedule_rows_[index], LV_OBJ_FLAG_HIDDEN);
    }
    MakeEmptyLabel(schedule_panel, "等待日程同步", &schedule_empty_label_);

    log_page_ = MakePage(dashboard_root_);
    lv_obj_t* log_panel = lv_obj_create(log_page_);
    lv_obj_set_size(log_panel, LV_PCT(100), LV_PCT(100));
    MakePanel(log_panel);
    lv_obj_set_flex_flow(log_panel, LV_FLEX_FLOW_COLUMN);
    MakeSectionHeader(log_panel, "工作记录", lv_color_hex(kLog), &log_count_label_);
    for (size_t index = 0; index < log_rows_.size(); ++index) {
        MakeDataRow(log_panel, 26, 52, lv_color_hex(kLog), lv_color_hex(kLogSurface), true,
                    &log_rows_[index], &log_date_labels_[index], &log_title_labels_[index]);
        lv_obj_add_flag(log_rows_[index], LV_OBJ_FLAG_HIDDEN);
    }
    MakeEmptyLabel(log_panel, "等待工作记录同步", &log_empty_label_);

    lv_obj_t* footer = lv_obj_create(dashboard_root_);
    lv_obj_set_size(footer, LV_PCT(100), kFooterHeight);
    MakeTransparent(footer);
    lv_obj_set_style_pad_column(footer, 6, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t* page_indicator = lv_obj_create(footer);
    lv_obj_set_height(page_indicator, LV_PCT(100));
    MakeTransparent(page_indicator);
    lv_obj_set_style_pad_column(page_indicator, 5, 0);
    lv_obj_set_flex_flow(page_indicator, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(page_indicator, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    page_name_label_ = lv_label_create(page_indicator);
    lv_obj_set_width(page_name_label_, 34);
    lv_obj_set_style_text_color(page_name_label_, lv_color_hex(kSecondaryText), 0);
    for (lv_obj_t*& dot : page_dots_) {
        dot = lv_obj_create(page_indicator);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_obj_t* sync_group = lv_obj_create(footer);
    lv_obj_set_height(sync_group, LV_PCT(100));
    lv_obj_set_flex_grow(sync_group, 1);
    MakeTransparent(sync_group);
    lv_obj_set_style_pad_column(sync_group, 5, 0);
    lv_obj_set_flex_flow(sync_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sync_group, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    sync_status_dot_ = lv_obj_create(sync_group);
    lv_obj_set_size(sync_status_dot_, 6, 6);
    lv_obj_set_style_radius(sync_status_dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(sync_status_dot_, 0, 0);
    lv_obj_set_style_bg_opa(sync_status_dot_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(sync_status_dot_, LV_OBJ_FLAG_SCROLLABLE);

    sync_status_label_ = lv_label_create(sync_group);
    lv_obj_set_flex_grow(sync_status_label_, 1);
    lv_label_set_long_mode(sync_status_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(sync_status_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(sync_status_label_, lv_color_hex(kSecondaryText), 0);

    UpdateClockLocked();
    UpdateSyncStatusLocked(cached_sync_status_);
    if (snapshot_received_) {
        RenderSnapshotLocked(cached_snapshot_);
    }
    ShowSelectedPageLocked();
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
    if (view == "assistant") {
        dashboard_preferred_.store(false);
    } else if (view == "dashboard") {
        dashboard_preferred_.store(true);
    } else if (view == "overview") {
        selected_page_.store(DashboardPage::kOverview);
        dashboard_preferred_.store(true);
    } else if (view == "schedule") {
        selected_page_.store(DashboardPage::kSchedule);
        dashboard_preferred_.store(true);
    } else if (view == "log") {
        selected_page_.store(DashboardPage::kLog);
        dashboard_preferred_.store(true);
    }
    SyncViewToState();
    return CurrentView();
}

std::string DashboardDisplay::CycleDashboardPage() {
    const DashboardPage current = selected_page_.load();
    const DashboardPage next = current == DashboardPage::kOverview   ? DashboardPage::kSchedule
                               : current == DashboardPage::kSchedule ? DashboardPage::kLog
                                                                     : DashboardPage::kOverview;
    selected_page_.store(next);
    dashboard_preferred_.store(true);
    SyncViewToState();
    return PageName(next);
}

std::string DashboardDisplay::SelectedDashboardView() const {
    return PageName(selected_page_.load());
}

std::string DashboardDisplay::CurrentView() {
    DisplayLockGuard lock(this);
    if (!lock || dashboard_root_ == nullptr ||
        lv_obj_has_flag(dashboard_root_, LV_OBJ_FLAG_HIDDEN)) {
        return "assistant";
    }
    return PageName(selected_page_.load());
}

void DashboardDisplay::ApplySnapshot(const dashboard::Snapshot& snapshot,
                                     const std::string& sync_status) {
    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }
    cached_snapshot_ = snapshot;
    cached_sync_status_ = sync_status;
    snapshot_received_ = true;
    if (dashboard_root_ != nullptr) {
        RenderSnapshotLocked(cached_snapshot_);
        UpdateSyncStatusLocked(cached_sync_status_);
    }
}

void DashboardDisplay::SetSyncStatus(const std::string& sync_status) {
    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }
    cached_sync_status_ = sync_status;
    if (sync_status_label_ != nullptr) {
        UpdateSyncStatusLocked(cached_sync_status_);
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
    ShowSelectedPageLocked();
    const bool show_dashboard = dashboard_preferred_.load() &&
                                Application::GetInstance().GetDeviceState() == kDeviceStateIdle;
    if (show_dashboard) {
        lv_obj_remove_flag(dashboard_root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(dashboard_root_);
    } else {
        lv_obj_add_flag(dashboard_root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void DashboardDisplay::ShowSelectedPageLocked() {
    if (overview_page_ == nullptr || schedule_page_ == nullptr || log_page_ == nullptr) {
        return;
    }
    const DashboardPage selected = selected_page_.load();
    lv_obj_t* pages[] = {overview_page_, schedule_page_, log_page_};
    const size_t selected_index = selected == DashboardPage::kOverview   ? 0
                                  : selected == DashboardPage::kSchedule ? 1
                                                                         : 2;
    const uint32_t selected_accent = selected == DashboardPage::kSchedule ? kScheduleTime
                                     : selected == DashboardPage::kLog    ? kLog
                                                                          : kSchedule;
    for (size_t index = 0; index < 3; ++index) {
        if (index == selected_index) {
            lv_obj_remove_flag(pages[index], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(pages[index], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_bg_color(page_dots_[index],
                                  lv_color_hex(index == selected_index ? selected_accent : kBorder),
                                  0);
    }
    lv_label_set_text(page_name_label_, selected == DashboardPage::kOverview   ? "总览"
                                        : selected == DashboardPage::kSchedule ? "日程"
                                                                               : "记录");
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
    lv_obj_set_style_bg_color(sync_status_dot_, lv_color_hex(synced ? kSchedule : kLog), 0);
}

void DashboardDisplay::RenderSnapshotLocked(const dashboard::Snapshot& snapshot) {
    const std::string date = FormatDate(snapshot.date);
    lv_label_set_text(dashboard_date_label_, date.c_str());

    SetCount(overview_schedule_count_label_, snapshot.schedules.size());
    SetCount(schedule_count_label_, snapshot.schedules.size());
    const bool schedule_empty = snapshot.schedules.empty();
    lv_label_set_text(overview_schedule_empty_label_, "今天没有安排");
    lv_label_set_text(schedule_empty_label_, "今天没有安排");
    if (schedule_empty) {
        lv_obj_remove_flag(overview_schedule_empty_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(schedule_empty_label_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(overview_schedule_empty_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(schedule_empty_label_, LV_OBJ_FLAG_HIDDEN);
    }
    for (size_t index = 0; index < schedule_rows_.size(); ++index) {
        if (index < snapshot.schedules.size()) {
            const auto& entry = snapshot.schedules[index];
            const char* time = entry.time.empty() ? "全天" : entry.time.c_str();
            lv_label_set_text(overview_schedule_time_labels_[index], time);
            lv_label_set_text(overview_schedule_title_labels_[index], entry.title.c_str());
            lv_label_set_text(schedule_time_labels_[index], time);
            const std::string detail = TitleWithContent(entry);
            lv_label_set_text(schedule_title_labels_[index], detail.c_str());
            lv_obj_remove_flag(overview_schedule_rows_[index], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(schedule_rows_[index], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(overview_schedule_rows_[index], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(schedule_rows_[index], LV_OBJ_FLAG_HIDDEN);
        }
    }

    SetCount(overview_log_count_label_, snapshot.work_logs.size());
    SetCount(log_count_label_, snapshot.work_logs.size());
    const bool log_empty = snapshot.work_logs.empty();
    lv_label_set_text(overview_log_empty_label_, "今天还没有工作记录");
    lv_label_set_text(log_empty_label_, "今天还没有工作记录");
    if (log_empty) {
        lv_obj_remove_flag(overview_log_empty_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(log_empty_label_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(overview_log_empty_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(log_empty_label_, LV_OBJ_FLAG_HIDDEN);
    }
    for (size_t index = 0; index < log_rows_.size(); ++index) {
        if (index < snapshot.work_logs.size()) {
            const auto& entry = snapshot.work_logs[index];
            const std::string entry_date = FormatMonthDay(entry.date);
            lv_label_set_text(overview_log_date_labels_[index], entry_date.c_str());
            lv_label_set_text(overview_log_title_labels_[index], entry.title.c_str());
            lv_label_set_text(log_date_labels_[index], entry_date.c_str());
            const std::string detail = TitleWithContent(entry);
            lv_label_set_text(log_title_labels_[index], detail.c_str());
            lv_obj_remove_flag(overview_log_rows_[index], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(log_rows_[index], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(overview_log_rows_[index], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(log_rows_[index], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

const char* DashboardDisplay::PageName(DashboardPage page) {
    switch (page) {
        case DashboardPage::kOverview:
            return "overview";
        case DashboardPage::kSchedule:
            return "schedule";
        case DashboardPage::kLog:
            return "log";
    }
    return "overview";
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
