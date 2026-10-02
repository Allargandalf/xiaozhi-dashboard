#include "dashboard_display.h"

#include "application.h"

#include <esp_log.h>

#include <sstream>

namespace {
void MakePanel(lv_obj_t* panel, lv_color_t background) {
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, background, 0);
    lv_obj_set_style_pad_all(panel, 7, 0);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
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
    lv_obj_set_style_bg_color(dashboard_root_, lv_color_hex(0xF3F5F8), 0);
    lv_obj_set_style_pad_all(dashboard_root_, 8, 0);
    lv_obj_set_style_pad_row(dashboard_root_, 6, 0);
    lv_obj_set_flex_flow(dashboard_root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(dashboard_root_, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* header = lv_obj_create(dashboard_root_);
    lv_obj_set_size(header, LV_PCT(100), 30);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Dashboard");
    lv_obj_set_style_text_color(title, lv_color_hex(0x172033), 0);

    dashboard_date_label_ = lv_label_create(header);
    lv_label_set_text(dashboard_date_label_, "---- -- --");
    lv_obj_set_style_text_color(dashboard_date_label_, lv_color_hex(0x526077), 0);

    lv_obj_t* columns = lv_obj_create(dashboard_root_);
    lv_obj_set_width(columns, LV_PCT(100));
    lv_obj_set_flex_grow(columns, 1);
    lv_obj_set_style_bg_opa(columns, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(columns, 0, 0);
    lv_obj_set_style_pad_all(columns, 0, 0);
    lv_obj_set_style_pad_column(columns, 6, 0);
    lv_obj_set_flex_flow(columns, LV_FLEX_FLOW_ROW);

    lv_obj_t* schedule_panel = lv_obj_create(columns);
    lv_obj_set_height(schedule_panel, LV_PCT(100));
    lv_obj_set_flex_grow(schedule_panel, 1);
    MakePanel(schedule_panel, lv_color_hex(0xE8F0FF));
    lv_obj_set_flex_flow(schedule_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_t* schedule_title = lv_label_create(schedule_panel);
    lv_label_set_text(schedule_title, "今日日程");
    lv_obj_set_style_text_color(schedule_title, lv_color_hex(0x2457A6), 0);
    schedule_label_ = lv_label_create(schedule_panel);
    lv_obj_set_width(schedule_label_, LV_PCT(100));
    lv_obj_set_flex_grow(schedule_label_, 1);
    lv_label_set_long_mode(schedule_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(schedule_label_, "暂无日程");
    lv_obj_set_style_text_color(schedule_label_, lv_color_hex(0x23314D), 0);

    lv_obj_t* log_panel = lv_obj_create(columns);
    lv_obj_set_height(log_panel, LV_PCT(100));
    lv_obj_set_flex_grow(log_panel, 1);
    MakePanel(log_panel, lv_color_hex(0xEAF7EF));
    lv_obj_set_flex_flow(log_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_t* log_title = lv_label_create(log_panel);
    lv_label_set_text(log_title, "工作日志");
    lv_obj_set_style_text_color(log_title, lv_color_hex(0x247047), 0);
    log_label_ = lv_label_create(log_panel);
    lv_obj_set_width(log_label_, LV_PCT(100));
    lv_obj_set_flex_grow(log_label_, 1);
    lv_label_set_long_mode(log_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(log_label_, "暂无日志");
    lv_obj_set_style_text_color(log_label_, lv_color_hex(0x244032), 0);

    sync_status_label_ = lv_label_create(dashboard_root_);
    lv_obj_set_width(sync_status_label_, LV_PCT(100));
    lv_obj_set_style_text_align(sync_status_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(sync_status_label_, lv_color_hex(0x6B7484), 0);
    lv_label_set_text(sync_status_label_, "等待电脑服务");

    SyncViewToStateLocked();
}

void DashboardDisplay::SetStatus(const char* status) {
    SpiLcdDisplay::SetStatus(status);
    SyncViewToState();
}

void DashboardDisplay::UpdateStatusBar(bool update_all) {
    SpiLcdDisplay::UpdateStatusBar(update_all);
    SyncViewToState();
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
    lv_label_set_text(sync_status_label_, sync_status.c_str());
}

void DashboardDisplay::SetSyncStatus(const std::string& sync_status) {
    DisplayLockGuard lock(this);
    if (lock && sync_status_label_ != nullptr) {
        lv_label_set_text(sync_status_label_, sync_status.c_str());
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

void DashboardDisplay::RenderSnapshotLocked(const dashboard::Snapshot& snapshot) {
    lv_label_set_text(dashboard_date_label_, snapshot.date.c_str());
    const std::string schedules = BuildScheduleText(snapshot);
    const std::string logs = BuildLogText(snapshot);
    lv_label_set_text(schedule_label_, schedules.c_str());
    lv_label_set_text(log_label_, logs.c_str());
}

std::string DashboardDisplay::BuildScheduleText(const dashboard::Snapshot& snapshot) {
    if (snapshot.schedules.empty()) {
        return "暂无日程";
    }
    std::ostringstream text;
    for (size_t i = 0; i < snapshot.schedules.size(); ++i) {
        if (i != 0) {
            text << '\n';
        }
        const auto& entry = snapshot.schedules[i];
        text << (entry.time.empty() ? "--:--" : entry.time) << "  " << entry.title;
    }
    return text.str();
}

std::string DashboardDisplay::BuildLogText(const dashboard::Snapshot& snapshot) {
    if (snapshot.work_logs.empty()) {
        return "暂无日志";
    }
    std::ostringstream text;
    for (size_t i = 0; i < snapshot.work_logs.size(); ++i) {
        if (i != 0) {
            text << '\n';
        }
        text << "• " << snapshot.work_logs[i].title;
    }
    return text.str();
}
