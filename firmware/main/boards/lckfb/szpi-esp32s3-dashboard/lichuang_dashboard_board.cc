#include "application.h"
#include "button.h"
#include "codecs/box_audio_codec.h"
#include "config.h"
#include "dashboard_button_policy.h"
#include "dashboard_client.h"
#include "dashboard_display.h"
#include "dashboard_inactivity_policy.h"
#include "dashboard_usb.h"
#include "dashboard_usb_protocol.h"
#include "esp32_camera.h"
#include "i2c_device.h"
#include "mcp_server.h"
#include "press_to_talk_mcp_tool.h"
#include "wifi_board.h"

#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <driver/uart.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_touch_ft5x06.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>

#include <array>
#include <atomic>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
const char* TAG = "LichuangDashboard";

class Pca9557 : public I2cDevice {
public:
    Pca9557(i2c_master_bus_handle_t i2c_bus, uint8_t address) : I2cDevice(i2c_bus, address) {
        WriteReg(0x01, 0x03);
        WriteReg(0x03, 0xF8);
    }

    void SetOutputState(uint8_t bit, uint8_t level) {
        uint8_t data = ReadReg(0x01);
        data = (data & ~(1 << bit)) | (level << bit);
        WriteReg(0x01, data);
    }
};

class CustomAudioCodec : public BoxAudioCodec {
public:
    CustomAudioCodec(i2c_master_bus_handle_t i2c_bus, Pca9557* pca9557)
        : BoxAudioCodec(i2c_bus, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                        AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
                        AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, GPIO_NUM_NC,
                        AUDIO_CODEC_ES8311_ADDR, AUDIO_CODEC_ES7210_ADDR, AUDIO_INPUT_REFERENCE,
                        28.0f, 2, 0.0f),
          pca9557_(pca9557) {}

    void EnableOutput(bool enable) override {
        BoxAudioCodec::EnableOutput(enable);
        pca9557_->SetOutputState(1, enable ? 1 : 0);
    }

private:
    Pca9557* pca9557_;
};
}  // namespace

class LichuangDashboardBoard : public WifiBoard {
public:
    LichuangDashboardBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeDisplay();
        InitializeTouch();
        InitializeButtons();
        InitializeCamera();

        dashboard_client_ = new DashboardClient(display_);
        dashboard_client_->Initialize();
        InitializeTools();
        InitializeSerialConfig();
        InitializeInactivityTimer();
        GetBacklight()->RestoreBrightness();
    }

    ~LichuangDashboardBoard() override {
        board_alive_->store(false);
        if (inactivity_timer_ != nullptr) {
            esp_timer_stop(inactivity_timer_);
            esp_timer_delete(inactivity_timer_);
        }
    }

    AudioCodec* GetAudioCodec() override {
        static CustomAudioCodec audio_codec(i2c_bus_, pca9557_);
        return &audio_codec;
    }

    Display* GetDisplay() override { return display_; }

    Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    Camera* GetCamera() override { return camera_; }

private:
    void InitializeI2c() {
        i2c_master_bus_config_t config = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags =
                {
                    .enable_internal_pullup = 1,
                },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&config, &i2c_bus_));
        pca9557_ = new Pca9557(i2c_bus_, 0x19);
    }

    void InitializeSpi() {
        spi_bus_config_t config = {};
        config.mosi_io_num = GPIO_NUM_40;
        config.miso_io_num = GPIO_NUM_NC;
        config.sclk_io_num = GPIO_NUM_41;
        config.quadwp_io_num = GPIO_NUM_NC;
        config.quadhd_io_num = GPIO_NUM_NC;
        config.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &config, SPI_DMA_CH_AUTO));
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = GPIO_NUM_NC;
        io_config.dc_gpio_num = GPIO_NUM_39;
        io_config.spi_mode = 2;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        pca9557_->SetOutputState(0, 0);
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

        display_ = new DashboardDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                        DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                        DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeTouch() {
        esp_lcd_touch_config_t touch_config = {
            .x_max = DISPLAY_HEIGHT,
            .y_max = DISPLAY_WIDTH,
            .rst_gpio_num = GPIO_NUM_NC,
            .int_gpio_num = GPIO_NUM_NC,
            .levels =
                {
                    .reset = 0,
                    .interrupt = 0,
                },
            .flags =
                {
                    .swap_xy = 1,
                    .mirror_x = 1,
                    .mirror_y = 0,
                },
        };
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS,
            .control_phase_bytes = 1,
            .dc_bit_offset = 0,
            .lcd_cmd_bits = 8,
            .flags =
                {
                    .disable_control_phase = 1,
                },
        };
        io_config.scl_speed_hz = 400000;
        esp_lcd_panel_io_handle_t io_handle = nullptr;
        if (esp_lcd_new_panel_io_i2c(i2c_bus_, &io_config, &io_handle) != ESP_OK) {
            ESP_LOGW(TAG, "Touch controller IO unavailable; continuing without touch");
            return;
        }
        esp_lcd_touch_handle_t touch = nullptr;
        if (esp_lcd_touch_new_i2c_ft5x06(io_handle, &touch_config, &touch) != ESP_OK ||
            touch == nullptr) {
            ESP_LOGW(TAG, "FT5x06 not found; continuing without touch");
            esp_lcd_panel_io_del(io_handle);
            return;
        }
        const lvgl_port_touch_cfg_t port_config = {
            .disp = lv_display_get_default(),
            .handle = touch,
        };
        if (port_config.disp != nullptr) {
            lvgl_port_add_touch(&port_config);
        }
    }

    void InitializeButtons() {
        boot_button_.OnClick(
            [this]() { ScheduleButtonGesture(dashboard::ButtonGesture::kSingleClick); });
        boot_button_.OnDoubleClick(
            [this]() { ScheduleButtonGesture(dashboard::ButtonGesture::kDoubleClick); });
    }

    static bool IsConversationState(DeviceState state) {
        return state == kDeviceStateConnecting || state == kDeviceStateListening ||
               state == kDeviceStateSpeaking;
    }

    static dashboard::ButtonRuntimeState GetButtonRuntimeState(DeviceState state) {
        if (state == kDeviceStateStarting) {
            return dashboard::ButtonRuntimeState::kStarting;
        }
        if (state == kDeviceStateIdle) {
            return dashboard::ButtonRuntimeState::kIdle;
        }
        if (IsConversationState(state)) {
            return dashboard::ButtonRuntimeState::kConversation;
        }
        return dashboard::ButtonRuntimeState::kOtherBusy;
    }

    void ScheduleButtonGesture(dashboard::ButtonGesture gesture) {
        auto alive = board_alive_;
        Application::GetInstance().Schedule([this, alive, gesture]() {
            if (!alive->load()) {
                return;
            }
            HandleButtonGesture(gesture);
        });
    }

    void HandleButtonGesture(dashboard::ButtonGesture gesture) {
        auto& app = Application::GetInstance();
        const bool press_to_talk_enabled =
            press_to_talk_tool_ != nullptr && press_to_talk_tool_->IsPressToTalkEnabled();
        const auto action = dashboard::ResolveButtonAction(
            gesture, GetButtonRuntimeState(app.GetDeviceState()), press_to_talk_enabled);
        switch (action) {
            case dashboard::ButtonAction::kEnterWifiConfig:
                EnterWifiConfigMode();
                return;
            case dashboard::ButtonAction::kCyclePage:
                display_->CycleDashboardPage();
                dashboard_client_->RequestRefresh();
                inactivity_policy_.Restart(GetInactivityState(app), NowMs());
                return;
            case dashboard::ButtonAction::kCyclePageAndEndConversation:
                display_->CycleDashboardPage();
                dashboard_client_->RequestRefresh();
                app.EndConversation();
                return;
            case dashboard::ButtonAction::kShowAssistantAndToggleVoice:
                display_->SetPreferredView("assistant");
                inactivity_policy_.Restart(GetInactivityState(app), NowMs());
                app.ToggleChatState();
                return;
            case dashboard::ButtonAction::kShowAssistantAndStartListening:
                display_->SetPreferredView("assistant");
                inactivity_policy_.Restart(GetInactivityState(app), NowMs());
                app.StartListening();
                return;
            case dashboard::ButtonAction::kShowDashboardAndEndConversation:
                display_->SetPreferredView("dashboard");
                dashboard_client_->RequestRefresh();
                app.EndConversation();
                return;
            case dashboard::ButtonAction::kNone:
                return;
        }
    }

    void InitializeCamera() {
        pca9557_->SetOutputState(2, 0);
        camera_config_t config = {};
        config.ledc_channel = LEDC_CHANNEL_2;
        config.ledc_timer = LEDC_TIMER_2;
        config.pin_d0 = CAMERA_PIN_D0;
        config.pin_d1 = CAMERA_PIN_D1;
        config.pin_d2 = CAMERA_PIN_D2;
        config.pin_d3 = CAMERA_PIN_D3;
        config.pin_d4 = CAMERA_PIN_D4;
        config.pin_d5 = CAMERA_PIN_D5;
        config.pin_d6 = CAMERA_PIN_D6;
        config.pin_d7 = CAMERA_PIN_D7;
        config.pin_xclk = CAMERA_PIN_XCLK;
        config.pin_pclk = CAMERA_PIN_PCLK;
        config.pin_vsync = CAMERA_PIN_VSYNC;
        config.pin_href = CAMERA_PIN_HREF;
        config.pin_sccb_sda = -1;
        config.pin_sccb_scl = CAMERA_PIN_SIOC;
        config.sccb_i2c_port = I2C_NUM_1;
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.xclk_freq_hz = XCLK_FREQ_HZ;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        camera_ = new Esp32Camera(config);
    }

    void InitializeTools() {
        auto& server = McpServer::GetInstance();
        server.AddTool(
            "self.system.reconfigure_wifi",
            "End this conversation and enter WiFi configuration mode. Ask the user to confirm.",
            PropertyList(), [this](const PropertyList&) -> ToolResult {
                EnterWifiConfigMode();
                return true;
            });

        Property view("view", kPropertyTypeString);
        view.SetMaxLength(16);
        server.AddTool(
            "self.dashboard.set_view",
            "切换设备页面。把用户说的“语音助手/对话”映射为 assistant，“看板/仪表盘”映射为 "
            "dashboard（保留上次看板页），“概览”映射为 overview，“日程”映射为 schedule，"
            "“工作记录/日志”映射为 log。选择 assistant 以外的页面会立即结束当前语音会话。",
            PropertyList({view}), [this](const PropertyList& properties) -> ToolResult {
                const std::string requested = properties["view"].value<std::string>();
                if (!dashboard::IsValidView(requested)) {
                    return std::unexpected(
                        "view must be assistant, dashboard, overview, schedule, or log");
                }
                auto& app = Application::GetInstance();
                const std::string current = display_->SetPreferredView(requested);
                const auto state = app.GetDeviceState();
                const bool conversation_active = IsConversationState(state);
                const bool dashboard_requested = requested != "assistant";
                const char* status = "applied";
                if (dashboard_requested) {
                    dashboard_client_->RequestRefresh();
                    if (conversation_active) {
                        // McpServer sends this tool result from scheduled main-loop work. The
                        // end-conversation event is deliberately handled after scheduled work,
                        // so the result is sent before its transport is closed.
                        app.EndConversation();
                        status = "ending_conversation";
                    } else if (state != kDeviceStateIdle) {
                        status = "waiting_for_idle";
                    } else if (current == "assistant") {
                        status = "waiting_for_display";
                    }
                } else {
                    inactivity_policy_.Restart(GetInactivityState(app), NowMs());
                }
                cJSON* result = cJSON_CreateObject();
                cJSON_AddStringToObject(result, "requested", requested.c_str());
                cJSON_AddStringToObject(result, "current", current.c_str());
                cJSON_AddStringToObject(result, "status", status);
                return result;
            });

        Property entry_type("type", kPropertyTypeString);
        entry_type.SetMaxLength(8);
        Property date("date", kPropertyTypeString);
        date.SetMaxLength(10);
        Property time("time", kPropertyTypeString, std::string(""));
        time.SetMaxLength(5);
        Property title("title", kPropertyTypeString);
        title.SetMaxLength(96);
        Property content("content", kPropertyTypeString, std::string(""));
        content.SetMaxLength(256);
        server.AddTool(
            "self.dashboard.add_entry",
            "Queue a schedule or work-log entry for the local computer dashboard service. "
            "time is optional for an all-day schedule. A queued response is not a saved "
            "confirmation. title is limited to 96 UTF-8 bytes and content to 256 UTF-8 bytes.",
            PropertyList({entry_type, date, time, title, content}),
            [this](const PropertyList& properties) -> ToolResult {
                auto queued =
                    dashboard_client_->QueueEntry(properties["type"].value<std::string>(),
                                                  properties["date"].value<std::string>(),
                                                  properties["time"].value<std::string>(),
                                                  properties["title"].value<std::string>(),
                                                  properties["content"].value<std::string>());
                if (!queued.queued) {
                    return std::unexpected(queued.error);
                }
                cJSON* result = cJSON_CreateObject();
                cJSON_AddStringToObject(result, "status", "queued");
                cJSON_AddBoolToObject(result, "saved", false);
                cJSON_AddStringToObject(result, "request_id", queued.request_id.c_str());
                return result;
            });

        press_to_talk_tool_ = new PressToTalkMcpTool();
        press_to_talk_tool_->Initialize();
    }

    static uint64_t NowMs() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }

    static dashboard::InactivityState GetInactivityState(Application& app) {
        const auto state = app.GetDeviceState();
        if (state == kDeviceStateIdle) {
            return dashboard::InactivityState::kIdle;
        }
        if (state == kDeviceStateListening) {
            if (!app.GetAudioService().IsPlaybackIdle()) {
                return dashboard::InactivityState::kBusy;
            }
            return app.IsVoiceDetected() ? dashboard::InactivityState::kListeningVoice
                                         : dashboard::InactivityState::kListeningSilent;
        }
        return dashboard::InactivityState::kBusy;
    }

    void InitializeInactivityTimer() {
        const esp_timer_create_args_t timer_args = {
            .callback =
                [](void* context) {
                    auto* board = static_cast<LichuangDashboardBoard*>(context);
                    bool expected = false;
                    if (!board->inactivity_poll_pending_.compare_exchange_strong(expected, true)) {
                        return;
                    }
                    auto alive = board->board_alive_;
                    Application::GetInstance().Schedule([board, alive]() {
                        if (!alive->load()) {
                            return;
                        }
                        board->inactivity_poll_pending_.store(false);
                        board->PollInactivity();
                    });
                },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "dashboard_idle",
            .skip_unhandled_events = true,
        };
        esp_err_t status = esp_timer_create(&timer_args, &inactivity_timer_);
        if (status == ESP_OK) {
            status = esp_timer_start_periodic(inactivity_timer_, 500 * 1000);
        }
        if (status != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start dashboard inactivity timer: %s",
                     esp_err_to_name(status));
            if (inactivity_timer_ != nullptr) {
                esp_timer_delete(inactivity_timer_);
                inactivity_timer_ = nullptr;
            }
        }
    }

    void PollInactivity() {
        auto& app = Application::GetInstance();
        const auto activity = GetInactivityState(app);
        const auto voice_generation = app.GetAudioService().VoiceActivityGeneration();
        if (voice_generation != last_voice_generation_) {
            last_voice_generation_ = voice_generation;
            inactivity_policy_.Restart(activity, NowMs());
        }
        const auto action = inactivity_policy_.Observe(activity, NowMs());
        if (!action.return_to_dashboard) {
            return;
        }

        display_->SetPreferredView("dashboard");
        dashboard_client_->RequestRefresh();
        if (action.end_conversation) {
            // The application rechecks VAD when it handles this event. Speech that begins after
            // this poll therefore invalidates the timeout instead of being interrupted.
            app.EndConversationIfSilent(voice_generation);
        }
    }

    void InitializeSerialConfig() {
        if (!uart_is_driver_installed(UART_NUM_0)) {
            esp_err_t status = uart_driver_install(UART_NUM_0, 4096, 0, 0, nullptr, 0);
            if (status != ESP_OK) {
                ESP_LOGW(TAG, "Dashboard serial config unavailable: %s", esp_err_to_name(status));
                return;
            }
        }
        if (xTaskCreate(SerialConfigTaskEntry, "dashboard_uart", 4096, this, 1,
                        &serial_config_task_) != pdPASS) {
            ESP_LOGW(TAG, "Failed to start dashboard serial config task");
        }
    }

    static void SerialConfigTaskEntry(void* context) {
        static_cast<LichuangDashboardBoard*>(context)->SerialConfigTask();
    }

    void SerialConfigTask() {
        dashboard::SerialLineBuffer buffer;
        std::array<uint8_t, 128> bytes = {};
        while (true) {
            const int read =
                uart_read_bytes(UART_NUM_0, bytes.data(), bytes.size(), pdMS_TO_TICKS(100));
            if (read <= 0) {
                continue;
            }
            for (int i = 0; i < read; ++i) {
                std::string line;
                if (buffer.Push(bytes[i], line) && !DashboardUsb::GetInstance().HandleLine(line)) {
                    HandleSerialConfigLine(line.c_str());
                }
            }
        }
    }

    void HandleSerialConfigLine(const char* line) {
        std::istringstream input(line);
        std::vector<std::string> fields;
        std::string field;
        while (input >> field) {
            fields.push_back(std::move(field));
        }
        if (fields.empty() || fields[0] != "dashboard_config") {
            return;
        }

        std::string error;
        bool success = false;
        if (fields.size() == 2 && fields[1] == "show") {
            success = true;
        } else if (fields.size() == 2 && fields[1] == "clear") {
            success = dashboard_client_->ClearConfig(error);
        } else if (fields.size() == 4 && fields[1] == "set") {
            const std::string token = fields[3] == "-" ? "" : fields[3];
            success = dashboard_client_->SaveConfig(fields[2], token, error);
        } else {
            error = "usage";
        }

        if (success) {
            const std::string summary = dashboard_client_->GetConfigSummary();
            std::printf("DASHBOARD_CONFIG %s\n", summary.c_str());
        } else {
            std::printf("DASHBOARD_CONFIG {\"ok\":false,\"error\":\"%s\"}\n", error.c_str());
        }
        std::fflush(stdout);
    }

    std::shared_ptr<std::atomic<bool>> board_alive_ = std::make_shared<std::atomic<bool>>(true);
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    Button boot_button_;
    DashboardDisplay* display_ = nullptr;
    Pca9557* pca9557_ = nullptr;
    Esp32Camera* camera_ = nullptr;
    DashboardClient* dashboard_client_ = nullptr;
    PressToTalkMcpTool* press_to_talk_tool_ = nullptr;
    TaskHandle_t serial_config_task_ = nullptr;
    dashboard::InactivityPolicy inactivity_policy_;
    uint32_t last_voice_generation_ = 0;
    esp_timer_handle_t inactivity_timer_ = nullptr;
    std::atomic<bool> inactivity_poll_pending_{false};
};

DECLARE_BOARD(LichuangDashboardBoard);
