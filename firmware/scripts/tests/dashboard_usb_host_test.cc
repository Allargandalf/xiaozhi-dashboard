#include "dashboard_usb_protocol.h"

#include <cassert>
#include <string>
#include <vector>

int main() {
    assert(dashboard::IsShallowSerialJson(R"({"title":"{ [ \\\" }"})"));
    assert(!dashboard::IsShallowSerialJson(std::string(9, '[') + std::string(9, ']')));
    assert(!dashboard::IsShallowSerialJson("{\"unterminated"));
    dashboard::SerialLineBuffer decoder;
    std::string line;
    std::vector<std::string> frames;
    auto feed = [&](const std::string& bytes) {
        for (unsigned char byte : bytes) {
            if (decoder.Push(byte, line))
                frames.push_back(line);
        }
    };
    feed("dashboard_rpc {\"title\":\"会议");
    assert(frames.empty());
    feed("\"}\r\n");
    assert(frames.size() == 1 && frames[0] == "dashboard_rpc {\"title\":\"会议\"}");
    feed(std::string(dashboard::kMaxSerialFrameBytes + 1, 'x'));
    feed("dashboard_config clear\n");
    assert(frames.size() == 1);  // Overflow cannot turn into a command suffix.
    feed("dashboard_usb hello\n");
    assert(frames.back() == "dashboard_usb hello");
    const size_t count = frames.size();
    feed(std::string("dashboard_config\0 clear\n", 24));
    assert(frames.size() == count);
    feed(std::string(dashboard::kMaxSerialFrameBytes, 'x') + "\n");
    assert(frames.back().size() == dashboard::kMaxSerialFrameBytes);
}
