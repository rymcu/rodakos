#include "apps/home/home_layout_codec.h"

#include <cJSON.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

namespace rodakos {
namespace {

struct JsonDeleter {
    void operator()(cJSON* value) const { cJSON_Delete(value); }
};

using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

bool HasDuplicateKeys(const cJSON* object) {
    if (!cJSON_IsObject(object)) {
        return false;
    }
    std::unordered_set<std::string> keys;
    for (const cJSON* child = object->child; child != nullptr; child = child->next) {
        if (child->string == nullptr || !keys.insert(child->string).second) {
            return true;
        }
    }
    return false;
}

bool HasOnlyKeys(const cJSON* object, std::initializer_list<std::string_view> allowed) {
    if (!cJSON_IsObject(object) || HasDuplicateKeys(object)) {
        return false;
    }
    for (const cJSON* child = object->child; child != nullptr; child = child->next) {
        if (child->string == nullptr) {
            return false;
        }
        const std::string_view key(child->string);
        bool found = false;
        for (const std::string_view candidate : allowed) {
            if (key == candidate) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

bool ContainsDecodedNul(std::string_view json) {
    if (json.find('\0') != std::string_view::npos) {
        return true;
    }

    bool in_string = false;
    for (size_t index = 0; index < json.size(); ++index) {
        const char ch = json[index];
        if (!in_string) {
            if (ch == '"') {
                in_string = true;
            }
            continue;
        }
        if (ch == '"') {
            in_string = false;
            continue;
        }
        if (ch != '\\' || index + 1 >= json.size()) {
            continue;
        }

        const char escape = json[++index];
        if (escape != 'u') {
            continue;
        }
        if (index + 4 < json.size() && json[index + 1] == '0' && json[index + 2] == '0' &&
            json[index + 3] == '0' && json[index + 4] == '0') {
            return true;
        }
        index = std::min(index + size_t{4}, json.size() - 1);
    }
    return false;
}

bool AddSize(size_t& total, size_t amount) {
    if (amount > kHomeLayoutMaxJsonBytes - total) {
        return false;
    }
    total += amount;
    return true;
}

size_t DecimalDigits(uint32_t value) {
    size_t digits = 1;
    while (value >= 10) {
        value /= 10;
        ++digits;
    }
    return digits;
}

bool AddEncodedStringSize(size_t& total, std::string_view value) {
    if (!AddSize(total, 2)) {
        return false;
    }
    for (const unsigned char ch : value) {
        size_t encoded_bytes = 1;
        if (ch == '"' || ch == '\\' || ch == '\b' || ch == '\f' || ch == '\n' || ch == '\r' ||
            ch == '\t') {
            encoded_bytes = 2;
        } else if (ch < 0x20) {
            encoded_bytes = 6;
        }
        if (!AddSize(total, encoded_bytes)) {
            return false;
        }
    }
    return true;
}

bool FitsEncodedLimit(const HomeLayout& layout) {
    size_t size = 0;
    if (!AddSize(size, std::string_view("{\"v\":").size()) ||
        !AddSize(size, DecimalDigits(layout.version)) ||
        !AddSize(size, std::string_view(",\"rev\":").size()) ||
        !AddSize(size, DecimalDigits(layout.revision)) ||
        !AddSize(size, std::string_view(",\"items\":[").size())) {
        return false;
    }

    for (size_t item_index = 0; item_index < layout.items.size(); ++item_index) {
        const auto& item = layout.items[item_index];
        if (item_index != 0 && !AddSize(size, 1)) {
            return false;
        }
        if (item.type == HomeLayoutItemType::kApp) {
            if (!AddSize(size, std::string_view("{\"type\":\"app\",\"id\":").size()) ||
                !AddEncodedStringSize(size, item.id) || !AddSize(size, 1)) {
                return false;
            }
            continue;
        }

        if (!AddSize(size, std::string_view("{\"type\":\"folder\",\"id\":").size()) ||
            !AddEncodedStringSize(size, item.id) ||
            !AddSize(size, std::string_view(",\"name\":").size()) ||
            !AddEncodedStringSize(size, item.name) ||
            !AddSize(size, std::string_view(",\"apps\":[").size())) {
            return false;
        }
        for (size_t app_index = 0; app_index < item.apps.size(); ++app_index) {
            if ((app_index != 0 && !AddSize(size, 1)) ||
                !AddEncodedStringSize(size, item.apps[app_index])) {
                return false;
            }
        }
        if (!AddSize(size, 2)) {
            return false;
        }
    }
    return AddSize(size, 2);
}

bool ReadUint32(const cJSON* object, const char* key, uint32_t& output) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) ||
        value->valuedouble < 0 || std::floor(value->valuedouble) != value->valuedouble ||
        value->valuedouble > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
        return false;
    }
    output = static_cast<uint32_t>(value->valuedouble);
    return true;
}

bool ReadString(const cJSON* object, const char* key, std::string& output) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(value) || value->valuestring == nullptr) {
        return false;
    }
    output = value->valuestring;
    return IsValidHomeUtf8(output);
}

bool AddOwnedItem(cJSON* parent, cJSON* child) {
    if (child == nullptr) {
        return false;
    }
    if (!cJSON_AddItemToArray(parent, child)) {
        cJSON_Delete(child);
        return false;
    }
    return true;
}

bool AddString(cJSON* object, const char* key, const std::string& value) {
    return cJSON_AddStringToObject(object, key, value.c_str()) != nullptr;
}

}  // namespace

HomeLayoutDecodeResult DecodeHomeLayout(std::string_view json) {
    HomeLayoutDecodeResult result;
    if (json.size() > kHomeLayoutMaxJsonBytes) {
        result.status = HomeLayoutDecodeStatus::kTooLarge;
        return result;
    }
    if (json.empty()) {
        result.status = HomeLayoutDecodeStatus::kMalformed;
        return result;
    }
    std::string terminated(json);
    JsonPtr root(cJSON_ParseWithLengthOpts(terminated.c_str(), terminated.size() + 1,
                                           nullptr, true));
    if (root == nullptr || !cJSON_IsObject(root.get()) || HasDuplicateKeys(root.get())) {
        result.status = HomeLayoutDecodeStatus::kMalformed;
        return result;
    }

    uint32_t version = 0;
    if (!ReadUint32(root.get(), "v", version)) {
        result.status = HomeLayoutDecodeStatus::kMalformed;
        return result;
    }
    result.source_version = version;
    if (version != kHomeLayoutSchemaVersion) {
        result.status = HomeLayoutDecodeStatus::kUnsupportedVersion;
        return result;
    }
    if (ContainsDecodedNul(json)) {
        result.status = HomeLayoutDecodeStatus::kMalformed;
        return result;
    }
    if (!HasOnlyKeys(root.get(), {"v", "rev", "items"})) {
        result.status = HomeLayoutDecodeStatus::kMalformed;
        return result;
    }

    uint32_t revision = 0;
    const cJSON* items = cJSON_GetObjectItemCaseSensitive(root.get(), "items");
    if (!ReadUint32(root.get(), "rev", revision) || !cJSON_IsArray(items)) {
        result.status = HomeLayoutDecodeStatus::kMalformed;
        return result;
    }

    HomeLayout layout;
    layout.revision = revision;
    const cJSON* json_item = nullptr;
    cJSON_ArrayForEach(json_item, items) {
        if (!cJSON_IsObject(json_item) || HasDuplicateKeys(json_item)) {
            result.status = HomeLayoutDecodeStatus::kMalformed;
            return result;
        }

        std::string type;
        std::string id;
        if (!ReadString(json_item, "type", type) || !ReadString(json_item, "id", id) || id.empty()) {
            result.status = HomeLayoutDecodeStatus::kMalformed;
            return result;
        }

        if (type == "app") {
            if (!HasOnlyKeys(json_item, {"type", "id"})) {
                result.status = HomeLayoutDecodeStatus::kMalformed;
                return result;
            }
            layout.items.push_back(HomeLayoutItem::App(std::move(id)));
            continue;
        }
        if (type != "folder") {
            result.status = HomeLayoutDecodeStatus::kMalformed;
            return result;
        }
        if (!HasOnlyKeys(json_item, {"type", "id", "name", "apps"})) {
            result.status = HomeLayoutDecodeStatus::kMalformed;
            return result;
        }

        std::string name;
        const cJSON* apps = cJSON_GetObjectItemCaseSensitive(json_item, "apps");
        if (!IsValidHomeFolderId(id) || !ReadString(json_item, "name", name) ||
            !IsValidHomeFolderName(name) || !cJSON_IsArray(apps)) {
            result.status = HomeLayoutDecodeStatus::kMalformed;
            return result;
        }

        std::vector<std::string> app_ids;
        const cJSON* app_id = nullptr;
        cJSON_ArrayForEach(app_id, apps) {
            if (!cJSON_IsString(app_id) || app_id->valuestring == nullptr) {
                result.status = HomeLayoutDecodeStatus::kMalformed;
                return result;
            }
            std::string value = app_id->valuestring;
            if (value.empty() || !IsValidHomeUtf8(value)) {
                result.status = HomeLayoutDecodeStatus::kMalformed;
                return result;
            }
            app_ids.push_back(std::move(value));
        }
        layout.items.push_back(HomeLayoutItem::Folder(std::move(id), std::move(name),
                                                      std::move(app_ids)));
    }

    result.status = HomeLayoutDecodeStatus::kOk;
    result.layout = std::move(layout);
    return result;
}

HomeLayoutEncodeResult EncodeHomeLayout(const HomeLayout& layout) {
    HomeLayoutEncodeResult result;
    if (!FitsEncodedLimit(layout)) {
        result.status = HomeLayoutEncodeStatus::kTooLarge;
        return result;
    }
    if (ValidateHomeLayout(layout) != HomeLayoutValidationStatus::kOk) {
        result.status = HomeLayoutEncodeStatus::kInvalidModel;
        return result;
    }

    JsonPtr root(cJSON_CreateObject());
    if (root == nullptr || cJSON_AddNumberToObject(root.get(), "v", layout.version) == nullptr ||
        cJSON_AddNumberToObject(root.get(), "rev", layout.revision) == nullptr) {
        result.status = HomeLayoutEncodeStatus::kOutOfMemory;
        return result;
    }

    cJSON* items = cJSON_AddArrayToObject(root.get(), "items");
    if (items == nullptr) {
        result.status = HomeLayoutEncodeStatus::kOutOfMemory;
        return result;
    }

    for (const auto& item : layout.items) {
        cJSON* json_item = cJSON_CreateObject();
        if (json_item == nullptr ||
            !AddString(json_item, "type",
                       item.type == HomeLayoutItemType::kApp ? "app" : "folder") ||
            !AddString(json_item, "id", item.id)) {
            cJSON_Delete(json_item);
            result.status = HomeLayoutEncodeStatus::kOutOfMemory;
            return result;
        }

        if (item.type == HomeLayoutItemType::kFolder) {
            if (!AddString(json_item, "name", item.name)) {
                cJSON_Delete(json_item);
                result.status = HomeLayoutEncodeStatus::kOutOfMemory;
                return result;
            }
            cJSON* apps = cJSON_AddArrayToObject(json_item, "apps");
            if (apps == nullptr) {
                cJSON_Delete(json_item);
                result.status = HomeLayoutEncodeStatus::kOutOfMemory;
                return result;
            }
            for (const auto& app_id : item.apps) {
                if (!AddOwnedItem(apps, cJSON_CreateString(app_id.c_str()))) {
                    cJSON_Delete(json_item);
                    result.status = HomeLayoutEncodeStatus::kOutOfMemory;
                    return result;
                }
            }
        }

        if (!AddOwnedItem(items, json_item)) {
            result.status = HomeLayoutEncodeStatus::kOutOfMemory;
            return result;
        }
    }

    char* encoded = cJSON_PrintUnformatted(root.get());
    if (encoded == nullptr) {
        result.status = HomeLayoutEncodeStatus::kOutOfMemory;
        return result;
    }
    result.json = encoded;
    cJSON_free(encoded);

    if (result.json.size() > kHomeLayoutMaxJsonBytes) {
        result.json.clear();
        result.status = HomeLayoutEncodeStatus::kTooLarge;
        return result;
    }
    result.status = HomeLayoutEncodeStatus::kOk;
    return result;
}

}  // namespace rodakos
