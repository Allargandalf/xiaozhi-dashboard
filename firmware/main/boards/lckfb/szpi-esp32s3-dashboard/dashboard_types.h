#ifndef LICHUANG_DASHBOARD_TYPES_H
#define LICHUANG_DASHBOARD_TYPES_H

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace dashboard {

constexpr size_t kMaxVisibleEntriesPerSection = 4;
constexpr size_t kMaxResponseBytes = 16 * 1024;

struct Entry {
    int id = 0;
    std::string type;
    std::string date;
    std::string time;
    std::string title;
    std::string content;
};

struct Snapshot {
    std::string date;
    std::vector<Entry> schedules;
    std::vector<Entry> work_logs;
};

inline bool IsLeapYear(int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

inline bool IsValidDate(const std::string& value) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
        return false;
    }
    for (size_t i = 0; i < value.size(); ++i) {
        if (i != 4 && i != 7 && !std::isdigit(static_cast<unsigned char>(value[i]))) {
            return false;
        }
    }
    const int year = std::stoi(value.substr(0, 4));
    const int month = std::stoi(value.substr(5, 2));
    const int day = std::stoi(value.substr(8, 2));
    if (year < 2000 || year > 2199 || month < 1 || month > 12) {
        return false;
    }
    static constexpr int kDaysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int max_day = kDaysPerMonth[month - 1];
    if (month == 2 && IsLeapYear(year)) {
        ++max_day;
    }
    return day >= 1 && day <= max_day;
}

inline bool IsValidTime(const std::string& value) {
    if (value.empty()) {
        return true;
    }
    if (value.size() != 5 || value[2] != ':' ||
        !std::isdigit(static_cast<unsigned char>(value[0])) ||
        !std::isdigit(static_cast<unsigned char>(value[1])) ||
        !std::isdigit(static_cast<unsigned char>(value[3])) ||
        !std::isdigit(static_cast<unsigned char>(value[4]))) {
        return false;
    }
    const int hour = (value[0] - '0') * 10 + value[1] - '0';
    const int minute = (value[3] - '0') * 10 + value[4] - '0';
    return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

inline bool IsValidType(const std::string& value) { return value == "schedule" || value == "log"; }

inline bool IsValidView(const std::string& value) {
    return value == "assistant" || value == "dashboard";
}

inline size_t DecodeUtf8CodePoint(const std::string& value, size_t offset, uint32_t& code_point) {
    const auto first = static_cast<unsigned char>(value[offset]);
    if (first < 0x80) {
        code_point = first;
        return offset + 1;
    }
    size_t length = 0;
    uint32_t decoded = 0;
    uint32_t minimum = 0;
    if (first >= 0xC2 && first <= 0xDF) {
        length = 2;
        decoded = first & 0x1F;
        minimum = 0x80;
    } else if (first >= 0xE0 && first <= 0xEF) {
        length = 3;
        decoded = first & 0x0F;
        minimum = 0x800;
    } else if (first >= 0xF0 && first <= 0xF4) {
        length = 4;
        decoded = first & 0x07;
        minimum = 0x10000;
    } else {
        code_point = first;
        return offset + 1;
    }
    if (offset + length > value.size()) {
        code_point = first;
        return offset + 1;
    }
    for (size_t index = 1; index < length; ++index) {
        const auto continuation = static_cast<unsigned char>(value[offset + index]);
        if ((continuation & 0xC0) != 0x80) {
            code_point = first;
            return offset + 1;
        }
        decoded = (decoded << 6) | (continuation & 0x3F);
    }
    if (decoded < minimum || decoded > 0x10FFFF || (decoded >= 0xD800 && decoded <= 0xDFFF)) {
        code_point = first;
        return offset + 1;
    }
    code_point = decoded;
    return offset + length;
}

inline bool IsPythonWhitespace(uint32_t code_point) {
    if ((code_point >= 0x09 && code_point <= 0x0D) || (code_point >= 0x1C && code_point <= 0x20)) {
        return true;
    }
    if (code_point >= 0x2000 && code_point <= 0x200A) {
        return true;
    }
    switch (code_point) {
        case 0x85:
        case 0xA0:
        case 0x1680:
        case 0x2028:
        case 0x2029:
        case 0x202F:
        case 0x205F:
        case 0x3000:
            return true;
        default:
            return false;
    }
}

inline std::string TrimTitle(const std::string& value) {
    size_t first = 0;
    while (first < value.size()) {
        uint32_t code_point = 0;
        const size_t next = DecodeUtf8CodePoint(value, first, code_point);
        if (!IsPythonWhitespace(code_point)) {
            break;
        }
        first = next;
    }
    size_t position = first;
    size_t last_non_whitespace = first;
    while (position < value.size()) {
        uint32_t code_point = 0;
        const size_t next = DecodeUtf8CodePoint(value, position, code_point);
        if (!IsPythonWhitespace(code_point)) {
            last_non_whitespace = next;
        }
        position = next;
    }
    return value.substr(first, last_non_whitespace - first);
}

inline bool IsHttpUrl(const std::string& value) {
    const size_t scheme_length = value.starts_with("http://")    ? 7
                                 : value.starts_with("https://") ? 8
                                                                 : 0;
    if (scheme_length == 0 || value.size() <= scheme_length || value.size() > 192) {
        return false;
    }
    for (unsigned char character : value) {
        if (character <= 0x20 || character == 0x7F) {
            return false;
        }
    }
    if (value.find_first_of("?#", scheme_length) != std::string::npos) {
        return false;
    }
    const size_t authority_end = value.find('/', scheme_length);
    const std::string authority = value.substr(scheme_length, authority_end == std::string::npos
                                                                  ? std::string::npos
                                                                  : authority_end - scheme_length);
    if (authority.empty() || authority.find('@') != std::string::npos) {
        return false;
    }

    std::string host;
    std::string port;
    if (authority.front() == '[') {
        const size_t close = authority.find(']');
        if (close == std::string::npos || close == 1) {
            return false;
        }
        host = authority.substr(1, close - 1);
        if (host.find(':') == std::string::npos) {
            return false;
        }
        for (unsigned char character : host) {
            if (!(std::isxdigit(character) || character == ':' || character == '.')) {
                return false;
            }
        }
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') {
                return false;
            }
            port = authority.substr(close + 2);
        }
    } else {
        const size_t colon = authority.rfind(':');
        if (colon != std::string::npos) {
            if (authority.find(':') != colon) {
                return false;
            }
            host = authority.substr(0, colon);
            port = authority.substr(colon + 1);
        } else {
            host = authority;
        }
        if (host.empty()) {
            return false;
        }
        for (unsigned char character : host) {
            if (!(std::isalnum(character) || character == '.' || character == '-')) {
                return false;
            }
        }
        if (host.front() == '.' || host.back() == '.' || host.find("..") != std::string::npos) {
            return false;
        }
        size_t label_start = 0;
        while (label_start < host.size()) {
            const size_t label_end = host.find('.', label_start);
            const size_t end = label_end == std::string::npos ? host.size() : label_end;
            if (host[label_start] == '-' || host[end - 1] == '-') {
                return false;
            }
            label_start = end + 1;
        }
    }
    if (host.empty()) {
        return false;
    }
    if (!port.empty()) {
        int port_number = 0;
        for (unsigned char character : port) {
            if (!std::isdigit(character)) {
                return false;
            }
            port_number = port_number * 10 + character - '0';
            if (port_number > 65535) {
                return false;
            }
        }
        if (port_number == 0) {
            return false;
        }
    } else if (authority.back() == ':') {
        return false;
    }
    return true;
}

}  // namespace dashboard

#endif  // LICHUANG_DASHBOARD_TYPES_H
