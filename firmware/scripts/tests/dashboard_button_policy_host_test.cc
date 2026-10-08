#include "dashboard_button_policy.h"

#include <cassert>

int main() {
    using dashboard::ButtonAction;
    using dashboard::ButtonGesture;
    using dashboard::ButtonRuntimeState;
    using dashboard::ResolveButtonAction;

    // A recognized double click is a voice action only; it never leaks a page-cycle action.
    assert(ResolveButtonAction(ButtonGesture::kSingleClick, ButtonRuntimeState::kIdle, false) ==
           ButtonAction::kCyclePage);
    assert(ResolveButtonAction(ButtonGesture::kDoubleClick, ButtonRuntimeState::kIdle, false) ==
           ButtonAction::kShowAssistantAndToggleVoice);
    assert(ResolveButtonAction(ButtonGesture::kDoubleClick, ButtonRuntimeState::kIdle, true) ==
           ButtonAction::kShowAssistantAndStartListening);

    // Page changes remain visible by closing an active conversation after selecting the page.
    assert(ResolveButtonAction(ButtonGesture::kSingleClick, ButtonRuntimeState::kConversation,
                               false) == ButtonAction::kCyclePageAndEndConversation);
    assert(ResolveButtonAction(ButtonGesture::kDoubleClick, ButtonRuntimeState::kConversation,
                               false) == ButtonAction::kShowDashboardAndEndConversation);

    // Preserve the failed-connect onboarding shortcut on a single click only.
    assert(ResolveButtonAction(ButtonGesture::kSingleClick, ButtonRuntimeState::kStarting, false) ==
           ButtonAction::kEnterWifiConfig);
    assert(ResolveButtonAction(ButtonGesture::kSingleClick, ButtonRuntimeState::kStarting, true) ==
           ButtonAction::kEnterWifiConfig);
    assert(ResolveButtonAction(ButtonGesture::kDoubleClick, ButtonRuntimeState::kStarting, false) ==
           ButtonAction::kNone);

    // Configuration, upgrading, activation, notification, and fatal states ignore B gestures.
    assert(ResolveButtonAction(ButtonGesture::kSingleClick, ButtonRuntimeState::kOtherBusy,
                               false) == ButtonAction::kNone);
    assert(ResolveButtonAction(ButtonGesture::kDoubleClick, ButtonRuntimeState::kOtherBusy, true) ==
           ButtonAction::kNone);
    return 0;
}
