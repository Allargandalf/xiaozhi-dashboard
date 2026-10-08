#ifndef LICHUANG_DASHBOARD_BUTTON_POLICY_H
#define LICHUANG_DASHBOARD_BUTTON_POLICY_H

namespace dashboard {

enum class ButtonGesture {
    kSingleClick,
    kDoubleClick,
};

enum class ButtonRuntimeState {
    kStarting,
    kIdle,
    kConversation,
    kOtherBusy,
};

enum class ButtonAction {
    kNone,
    kEnterWifiConfig,
    kCyclePage,
    kCyclePageAndEndConversation,
    kShowAssistantAndToggleVoice,
    kShowAssistantAndStartListening,
    kShowDashboardAndEndConversation,
};

constexpr ButtonAction ResolveButtonAction(ButtonGesture gesture, ButtonRuntimeState state,
                                           bool press_to_talk_enabled) {
    if (gesture == ButtonGesture::kSingleClick) {
        switch (state) {
            case ButtonRuntimeState::kStarting:
                return ButtonAction::kEnterWifiConfig;
            case ButtonRuntimeState::kIdle:
                return ButtonAction::kCyclePage;
            case ButtonRuntimeState::kConversation:
                return ButtonAction::kCyclePageAndEndConversation;
            case ButtonRuntimeState::kOtherBusy:
                return ButtonAction::kNone;
        }
    }

    switch (state) {
        case ButtonRuntimeState::kIdle:
            return press_to_talk_enabled ? ButtonAction::kShowAssistantAndStartListening
                                         : ButtonAction::kShowAssistantAndToggleVoice;
        case ButtonRuntimeState::kConversation:
            return ButtonAction::kShowDashboardAndEndConversation;
        case ButtonRuntimeState::kStarting:
        case ButtonRuntimeState::kOtherBusy:
            return ButtonAction::kNone;
    }
    return ButtonAction::kNone;
}

}  // namespace dashboard

#endif  // LICHUANG_DASHBOARD_BUTTON_POLICY_H
