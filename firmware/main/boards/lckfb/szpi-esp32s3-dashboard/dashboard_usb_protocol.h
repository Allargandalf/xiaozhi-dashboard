#ifndef LICHUANG_DASHBOARD_USB_PROTOCOL_H
#define LICHUANG_DASHBOARD_USB_PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace dashboard {
constexpr size_t kMaxSerialFrameBytes = 16 * 1024;

// The RPC schema needs at most six levels. Reject hostile nesting before cJSON
// recurses on the small UART task stack; quoted braces are ordinary text.
inline bool IsShallowSerialJson(std::string_view json) {
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (char character : json) {
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (character == '\\')
                escaped = true;
            else if (character == '"')
                quoted = false;
        } else if (character == '"')
            quoted = true;
        else if (character == '{' || character == '[') {
            if (++depth > 8)
                return false;
        } else if (character == '}' || character == ']') {
            if (--depth < 0)
                return false;
        }
    }
    return depth == 0 && !quoted;
}

// A truncated or oversized frame is discarded through its newline, never
// interpreted as a different command. Storage is on the heap, not UART stack.
class SerialLineBuffer {
public:
    bool Push(uint8_t byte, std::string& completed) {
        if (byte == '\r') {
            return false;
        }
        if (byte == '\n') {
            const bool ready = !discarding_ && !line_.empty();
            if (ready) {
                completed = std::move(line_);
            }
            line_.clear();
            discarding_ = false;
            return ready;
        }
        if (discarding_) {
            return false;
        }
        if (byte < 0x20 || byte == 0x7F || line_.size() >= kMaxSerialFrameBytes) {
            line_.clear();
            discarding_ = true;
            return false;
        }
        line_.push_back(static_cast<char>(byte));
        return false;
    }

private:
    std::string line_;
    bool discarding_ = false;
};
}  // namespace dashboard

#endif
