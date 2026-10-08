#ifndef LICHUANG_DASHBOARD_INACTIVITY_POLICY_H
#define LICHUANG_DASHBOARD_INACTIVITY_POLICY_H

#include <cstdint>

namespace dashboard {

enum class InactivityState {
    kIdle,
    kListeningSilent,
    kListeningVoice,
    kBusy,
};

struct InactivityAction {
    bool return_to_dashboard = false;
    bool end_conversation = false;
};

class InactivityPolicy {
public:
    static constexpr uint64_t kDefaultTimeoutMs = 30'000;

    explicit InactivityPolicy(uint64_t timeout_ms = kDefaultTimeoutMs) : timeout_ms_(timeout_ms) {}

    InactivityAction Observe(InactivityState state, uint64_t now_ms) {
        if (!IsInactive(state)) {
            Disarm(state);
            return {};
        }

        if (!armed_ || state != state_ || now_ms < inactive_since_ms_) {
            Arm(state, now_ms);
            return {};
        }

        if (!fired_ && now_ms - inactive_since_ms_ >= timeout_ms_) {
            fired_ = true;
            return {
                .return_to_dashboard = true,
                .end_conversation = state == InactivityState::kListeningSilent,
            };
        }
        return {};
    }

    // Start a fresh idle window after an explicit request to show the assistant view.
    void Restart(InactivityState state, uint64_t now_ms) {
        if (IsInactive(state)) {
            Arm(state, now_ms);
        } else {
            Disarm(state);
        }
    }

private:
    static bool IsInactive(InactivityState state) {
        return state == InactivityState::kIdle || state == InactivityState::kListeningSilent;
    }

    void Arm(InactivityState state, uint64_t now_ms) {
        state_ = state;
        inactive_since_ms_ = now_ms;
        armed_ = true;
        fired_ = false;
    }

    void Disarm(InactivityState state) {
        state_ = state;
        armed_ = false;
        fired_ = false;
    }

    uint64_t timeout_ms_;
    uint64_t inactive_since_ms_ = 0;
    InactivityState state_ = InactivityState::kBusy;
    bool armed_ = false;
    bool fired_ = false;
};

}  // namespace dashboard

#endif  // LICHUANG_DASHBOARD_INACTIVITY_POLICY_H
