#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

enum class SettingsStringReadStatus {
    kOk,
    kNotFound,
    kTypeMismatch,
    kTooLarge,
    kError,
};

enum class SettingsStringWriteStatus {
    kOk,
    kRemoveFailed,
    kError,
};

namespace rodakos_home_ui_test {

struct FakeSettingsState {
    std::map<std::string, std::string> committed_values;
    std::optional<SettingsStringReadStatus> read_override;
    SettingsStringWriteStatus write_status = SettingsStringWriteStatus::kOk;
    bool commit_result = true;
    int read_calls = 0;
    int write_calls = 0;
    int commit_calls = 0;
    std::vector<std::string> operations;
};

inline FakeSettingsState& SettingsState() {
    static FakeSettingsState state;
    return state;
}

inline std::mutex& SettingsMutex() {
    static std::mutex mutex;
    return mutex;
}

inline std::string StorageKey(const std::string& name_space, const std::string& key) {
    return name_space + "\n" + key;
}

inline void ResetSettings() {
    std::lock_guard<std::mutex> lock(SettingsMutex());
    SettingsState() = {};
}

inline void SetCommittedSetting(const std::string& name_space,
                                const std::string& key,
                                std::string value) {
    std::lock_guard<std::mutex> lock(SettingsMutex());
    SettingsState().committed_values[StorageKey(name_space, key)] = std::move(value);
}

inline std::string GetCommittedSetting(const std::string& name_space,
                                       const std::string& key) {
    std::lock_guard<std::mutex> lock(SettingsMutex());
    const auto it = SettingsState().committed_values.find(StorageKey(name_space, key));
    return it == SettingsState().committed_values.end() ? std::string() : it->second;
}

}  // namespace rodakos_home_ui_test

class Settings {
public:
    explicit Settings(const std::string& name_space = {}, bool read_write = false)
        : namespace_(name_space), read_write_(read_write) {}

    std::string GetString(const std::string& key, const std::string& default_value = {}) {
        std::string value;
        return ReadString(key, value, static_cast<size_t>(-1)) ==
                       SettingsStringReadStatus::kOk
                   ? value
                   : default_value;
    }

    SettingsStringReadStatus ReadString(const std::string& key,
                                        std::string& value,
                                        size_t max_value_bytes) {
        std::lock_guard<std::mutex> lock(rodakos_home_ui_test::SettingsMutex());
        auto& state = rodakos_home_ui_test::SettingsState();
        ++state.read_calls;
        state.operations.push_back("read:" + namespace_ + ":" + key);
        value.clear();
        if (state.read_override.has_value()) {
            return *state.read_override;
        }
        const auto it = state.committed_values.find(
            rodakos_home_ui_test::StorageKey(namespace_, key));
        if (it == state.committed_values.end()) {
            return SettingsStringReadStatus::kNotFound;
        }
        if (it->second.size() > max_value_bytes) {
            return SettingsStringReadStatus::kTooLarge;
        }
        value = it->second;
        return SettingsStringReadStatus::kOk;
    }

    bool SetString(const std::string& key, const std::string& value) {
        return WriteString(key, value) == SettingsStringWriteStatus::kOk;
    }

    SettingsStringWriteStatus WriteString(const std::string& key,
                                          const std::string& value) {
        std::lock_guard<std::mutex> lock(rodakos_home_ui_test::SettingsMutex());
        auto& state = rodakos_home_ui_test::SettingsState();
        ++state.write_calls;
        state.operations.push_back("write:" + namespace_ + ":" + key);
        if (!read_write_) {
            return SettingsStringWriteStatus::kError;
        }
        if (state.write_status != SettingsStringWriteStatus::kOk) {
            return state.write_status;
        }
        pending_values_[key] = value;
        return SettingsStringWriteStatus::kOk;
    }

    bool Commit() {
        std::lock_guard<std::mutex> lock(rodakos_home_ui_test::SettingsMutex());
        auto& state = rodakos_home_ui_test::SettingsState();
        ++state.commit_calls;
        state.operations.push_back("commit:" + namespace_);
        if (!read_write_ || !state.commit_result) {
            return false;
        }
        for (const auto& [key, value] : pending_values_) {
            state.committed_values[rodakos_home_ui_test::StorageKey(namespace_, key)] = value;
        }
        pending_values_.clear();
        return true;
    }

    int32_t GetInt(const std::string&, int32_t default_value = 0) {
        return default_value;
    }
    bool SetInt(const std::string&, int32_t) { return false; }
    bool GetBool(const std::string&, bool default_value = false) {
        return default_value;
    }
    bool SetBool(const std::string&, bool) { return false; }

private:
    std::string namespace_;
    bool read_write_ = false;
    std::map<std::string, std::string> pending_values_;
};
