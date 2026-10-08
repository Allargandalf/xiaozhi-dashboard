#include "dashboard_protocol.h"
#include "dashboard_types.h"

#include <cassert>

int main() {
    using namespace dashboard;
    assert(IsValidDate("2026-10-02"));
    assert(IsValidDate("2024-02-29"));
    assert(!IsValidDate("2025-02-29"));
    assert(!IsValidDate("2026-13-01"));
    assert(!IsValidDate("2026-01-00"));
    assert(!IsValidDate("26-10-02"));

    assert(IsValidTime(""));
    assert(IsValidTime("00:00"));
    assert(IsValidTime("23:59"));
    assert(!IsValidTime("24:00"));
    assert(!IsValidTime("9:30"));

    assert(IsValidType("schedule"));
    assert(IsValidType("log"));
    assert(!IsValidType("note"));
    assert(IsValidView("assistant"));
    assert(IsValidView("dashboard"));
    assert(IsValidView("overview"));
    assert(IsValidView("schedule"));
    assert(IsValidView("log"));
    assert(!IsValidView("home"));
    assert(!IsValidView("schedules"));

    assert(TrimTitle("  mentor meeting\t") == "mentor meeting");
    assert(TrimTitle(" \r\n\t").empty());
    assert(TrimTitle("\xC2\xA0"
                     "会议"
                     "\xE3\x80\x80") == "会议");
    assert(TrimTitle("\xC2\xA0"
                     "\xE3\x80\x80")
               .empty());

    assert(IsHttpUrl("http://192.168.1.20:8765"));
    assert(IsHttpUrl("https://dashboard.local/api"));
    assert(IsHttpUrl("http://[::1]:8765"));
    assert(!IsHttpUrl("http://"));
    assert(!IsHttpUrl("http://:8765"));
    assert(!IsHttpUrl("http://host:"));
    assert(!IsHttpUrl("http://host:70000"));
    assert(!IsHttpUrl("http://user:pass@host"));
    assert(!IsHttpUrl("http://host/path?token=secret"));
    assert(!IsHttpUrl("http://host/#fragment"));
    assert(!IsHttpUrl("http://host name"));
    assert(!IsHttpUrl("http://-bad-host"));
    assert(!IsHttpUrl("http://bad..host"));
    assert(!IsHttpUrl("http://[not-ipv6]:8765"));

    Entry expected;
    expected.type = "log";
    expected.date = "2026-10-02";
    expected.title = "foo";
    std::string error;
    assert(ValidatePostConfirmation(
        R"({"entry":{"id":1,"type":"log","date":"2026-10-02","time":null,"title":"foo","content":""},"idempotent":false})",
        expected, error));
    assert(!ValidatePostConfirmation(
        R"({"entry":{"id":0,"type":"log","date":"2026-10-02","time":null,"title":"foo","content":""},"idempotent":false})",
        expected, error));
    expected.time = "15:00";
    assert(!ValidatePostConfirmation(
        R"({"entry":{"id":2,"type":"log","date":"2026-10-02","time":null,"title":"foo","content":""},"idempotent":true})",
        expected, error));
    return 0;
}
