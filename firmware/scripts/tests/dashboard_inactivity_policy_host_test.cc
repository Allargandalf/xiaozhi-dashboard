#include "dashboard_inactivity_policy.h"

#include <cassert>

int main() {
    using dashboard::InactivityPolicy;
    using dashboard::InactivityState;

    InactivityPolicy policy(30'000);

    // Silent listening expires once and asks the application to close the conversation.
    assert(!policy.Observe(InactivityState::kListeningSilent, 1'000).return_to_dashboard);
    assert(!policy.Observe(InactivityState::kListeningSilent, 30'999).return_to_dashboard);
    auto action = policy.Observe(InactivityState::kListeningSilent, 31'000);
    assert(action.return_to_dashboard);
    assert(action.end_conversation);
    assert(!policy.Observe(InactivityState::kListeningSilent, 60'000).return_to_dashboard);

    // Real speech invalidates the old deadline. Silence gets a complete new window.
    policy.Observe(InactivityState::kListeningVoice, 61'000);
    policy.Observe(InactivityState::kListeningSilent, 62'000);
    assert(!policy.Observe(InactivityState::kListeningSilent, 91'999).return_to_dashboard);
    action = policy.Observe(InactivityState::kListeningSilent, 92'000);
    assert(action.return_to_dashboard);
    assert(action.end_conversation);

    // TTS/network/config/update activity also invalidates a previously armed deadline.
    policy.Observe(InactivityState::kListeningSilent, 100'000);
    policy.Observe(InactivityState::kBusy, 120'000);
    policy.Observe(InactivityState::kListeningSilent, 121'000);
    assert(!policy.Observe(InactivityState::kListeningSilent, 130'000).return_to_dashboard);
    assert(policy.Observe(InactivityState::kListeningSilent, 151'000).end_conversation);

    // Idle returns the UI to the dashboard without asking to close a conversation.
    policy.Observe(InactivityState::kIdle, 200'000);
    action = policy.Observe(InactivityState::kIdle, 230'000);
    assert(action.return_to_dashboard);
    assert(!action.end_conversation);

    // An explicit assistant-view request starts a fresh idle window, even after expiry.
    policy.Restart(InactivityState::kIdle, 240'000);
    assert(!policy.Observe(InactivityState::kIdle, 269'999).return_to_dashboard);
    assert(policy.Observe(InactivityState::kIdle, 270'000).return_to_dashboard);

    // A clock reset cannot make an old timer fire; it establishes a fresh deadline.
    policy.Restart(InactivityState::kListeningSilent, 500'000);
    assert(!policy.Observe(InactivityState::kListeningSilent, 100).return_to_dashboard);
    assert(!policy.Observe(InactivityState::kListeningSilent, 30'099).return_to_dashboard);
    assert(policy.Observe(InactivityState::kListeningSilent, 30'100).end_conversation);

    return 0;
}
