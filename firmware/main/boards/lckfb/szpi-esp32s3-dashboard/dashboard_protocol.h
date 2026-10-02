#ifndef LICHUANG_DASHBOARD_PROTOCOL_H
#define LICHUANG_DASHBOARD_PROTOCOL_H

#include "dashboard_types.h"

#include <cJSON.h>

#include <cstring>
#include <memory>
#include <string>

namespace dashboard {

inline bool IsPositiveInteger(const cJSON* value) {
    return cJSON_IsNumber(value) && value->valueint > 0 &&
           value->valuedouble == static_cast<double>(value->valueint);
}

inline bool ValidatePostConfirmation(const std::string& body, const Entry& expected,
                                     std::string& error) {
    using JsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
    JsonPtr root(cJSON_ParseWithLength(body.data(), body.size()), cJSON_Delete);
    if (root == nullptr || !cJSON_IsObject(root.get())) {
        error = "root is not an object";
        return false;
    }
    const cJSON* idempotent = cJSON_GetObjectItemCaseSensitive(root.get(), "idempotent");
    const cJSON* entry = cJSON_GetObjectItemCaseSensitive(root.get(), "entry");
    if (!cJSON_IsBool(idempotent) || !cJSON_IsObject(entry)) {
        error = "missing entry or idempotent";
        return false;
    }
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(entry, "id");
    const cJSON* type = cJSON_GetObjectItemCaseSensitive(entry, "type");
    const cJSON* date = cJSON_GetObjectItemCaseSensitive(entry, "date");
    const cJSON* time = cJSON_GetObjectItemCaseSensitive(entry, "time");
    const cJSON* title = cJSON_GetObjectItemCaseSensitive(entry, "title");
    const cJSON* content = cJSON_GetObjectItemCaseSensitive(entry, "content");
    const bool time_matches = (cJSON_IsString(time) && time->valuestring != nullptr &&
                               expected.time == time->valuestring) ||
                              (cJSON_IsNull(time) && expected.time.empty());
    if (!IsPositiveInteger(id) || !cJSON_IsString(type) || type->valuestring == nullptr ||
        !cJSON_IsString(date) || date->valuestring == nullptr || !time_matches ||
        !cJSON_IsString(title) || title->valuestring == nullptr || !cJSON_IsString(content) ||
        content->valuestring == nullptr) {
        error = "entry is missing a required field";
        return false;
    }
    if (expected.type != type->valuestring || expected.date != date->valuestring ||
        expected.title != title->valuestring || expected.content != content->valuestring) {
        error = "entry confirmation does not match the request";
        return false;
    }
    return true;
}

}  // namespace dashboard

#endif  // LICHUANG_DASHBOARD_PROTOCOL_H
