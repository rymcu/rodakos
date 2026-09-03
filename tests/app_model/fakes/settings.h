#pragma once

#include <cstddef>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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

enum class SettingsBoolReadStatus {
    kOk,
    kNotFound,
    kTypeMismatch,
    kError,
};

namespace rodakos_test {

struct FakeSettingsState {
    std::map<std::string, std::string> committed_values;
    std::map<std::string, bool> committed_bool_values;
    std::optional<SettingsStringReadStatus> read_override;
    std::optional<SettingsBoolReadStatus> bool_read_override;
    SettingsStringWriteStatus write_status = SettingsStringWriteStatus::kOk;
    bool bool_set_result = true;
    bool commit_result = true;
    int read_calls = 0;
    int set_calls = 0;
    int commit_calls = 0;
    int destructor_commit_calls = 0;
    int read_delay_ms = 0;
    std::vector<std::string> operations;
};

inline FakeSettingsState& SettingsState() {
    static FakeSettingsState state;
    return state;
}

inline std::mutex& SettingsStateMutex() {
    static std::mutex mutex;
    return mutex;
}

inline void ResetSettingsState() {
    std::lock_guard<std::mutex> lock(SettingsStateMutex());
    SettingsState() = FakeSettingsState{};
}

inline std::string SettingsStorageKey(const std::string& name_space, const std::string& key) {
    return name_space + "\n" + key;
}

inline void SetCommittedSetting(const std::string& name_space,
                                const std::string& key,
                                std::string value) {
    std::lock_guard<std::mutex> lock(SettingsStateMutex());
    const std::string storage_key = SettingsStorageKey(name_space, key);
    SettingsState().committed_values[storage_key] = std::move(value);
    SettingsState().committed_bool_values.erase(storage_key);
}

inline std::string GetCommittedSetting(const std::string& name_space, const std::string& key) {
    std::lock_guard<std::mutex> lock(SettingsStateMutex());
    const auto it = SettingsState().committed_values.find(SettingsStorageKey(name_space, key));
    return it == SettingsState().committed_values.end() ? std::string() : it->second;
}

inline void SetCommittedBool(const std::string& name_space,
                             const std::string& key,
                             bool value) {
    std::lock_guard<std::mutex> lock(SettingsStateMutex());
    const std::string storage_key = SettingsStorageKey(name_space, key);
    SettingsState().committed_bool_values[storage_key] = value;
    SettingsState().committed_values.erase(storage_key);
}

inline std::optional<bool> GetCommittedBool(const std::string& name_space,
                                            const std::string& key) {
    std::lock_guard<std::mutex> lock(SettingsStateMutex());
    const auto it = SettingsState().committed_bool_values.find(
        SettingsStorageKey(name_space, key));
    return it == SettingsState().committed_bool_values.end()
               ? std::optional<bool>{}
               : std::optional<bool>{it->second};
}

}  // namespace rodakos_test

class Settings {
public:
    explicit Settings(const std::string& name_space = "", bool read_write = false)
        : namespace_(name_space), read_write_(read_write) {}

    ~Settings() {
        if (read_write_ && dirty_ && !commit_attempted_) {
            {
                std::lock_guard<std::mutex> lock(rodakos_test::SettingsStateMutex());
                ++rodakos_test::SettingsState().destructor_commit_calls;
            }
            Commit();
        }
    }

    std::string GetString(const std::string& key, const std::string& default_value = "") {
        std::string value;
        return ReadString(key, value, static_cast<size_t>(-1)) == SettingsStringReadStatus::kOk
                   ? value
                   : default_value;
    }

    SettingsStringReadStatus ReadString(const std::string& key,
                                        std::string& value,
                                        size_t max_value_bytes) {
        SettingsStringReadStatus result = SettingsStringReadStatus::kNotFound;
        int delay_ms = 0;
        {
            std::lock_guard<std::mutex> lock(rodakos_test::SettingsStateMutex());
            auto& state = rodakos_test::SettingsState();
            ++state.read_calls;
            state.operations.push_back("read:" + namespace_ + ":" + key);
            value.clear();
            delay_ms = state.read_delay_ms;
            if (state.read_override.has_value()) {
                result = *state.read_override;
            } else {
                const auto it = state.committed_values.find(
                    rodakos_test::SettingsStorageKey(namespace_, key));
                if (it == state.committed_values.end()) {
                    result = SettingsStringReadStatus::kNotFound;
                } else if (it->second.size() > max_value_bytes) {
                    result = SettingsStringReadStatus::kTooLarge;
                } else {
                    value = it->second;
                    result = SettingsStringReadStatus::kOk;
                }
            }
        }
        if (delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
        return result;
    }

    bool SetString(const std::string& key, const std::string& value) {
        return WriteString(key, value) == SettingsStringWriteStatus::kOk;
    }

    SettingsStringWriteStatus WriteString(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(rodakos_test::SettingsStateMutex());
        auto& state = rodakos_test::SettingsState();
        ++state.set_calls;
        state.operations.push_back("set:" + namespace_ + ":" + key);
        if (!read_write_ || state.write_status == SettingsStringWriteStatus::kError) {
            return SettingsStringWriteStatus::kError;
        }
        state.committed_values[rodakos_test::SettingsStorageKey(namespace_, key)] = value;
        if (state.write_status == SettingsStringWriteStatus::kOk) {
            dirty_ = true;
            commit_attempted_ = false;
        }
        return state.write_status;
    }

    bool GetBool(const std::string& key, bool default_value = false) {
        bool value = default_value;
        return ReadBool(key, value) == SettingsBoolReadStatus::kOk ? value : default_value;
    }

    SettingsBoolReadStatus ReadBool(const std::string& key, bool& value) {
        std::lock_guard<std::mutex> lock(rodakos_test::SettingsStateMutex());
        auto& state = rodakos_test::SettingsState();
        ++state.read_calls;
        state.operations.push_back("read-bool:" + namespace_ + ":" + key);
        if (state.bool_read_override.has_value()) {
            return *state.bool_read_override;
        }

        const std::string storage_key = rodakos_test::SettingsStorageKey(namespace_, key);
        const auto bool_it = state.committed_bool_values.find(storage_key);
        if (bool_it != state.committed_bool_values.end()) {
            value = bool_it->second;
            return SettingsBoolReadStatus::kOk;
        }
        if (state.committed_values.find(storage_key) != state.committed_values.end()) {
            return SettingsBoolReadStatus::kTypeMismatch;
        }
        return SettingsBoolReadStatus::kNotFound;
    }

    bool SetBool(const std::string& key, bool value) {
        std::lock_guard<std::mutex> lock(rodakos_test::SettingsStateMutex());
        auto& state = rodakos_test::SettingsState();
        ++state.set_calls;
        state.operations.push_back("set-bool:" + namespace_ + ":" + key);
        if (!read_write_ || !state.bool_set_result) {
            return false;
        }
        const std::string storage_key = rodakos_test::SettingsStorageKey(namespace_, key);
        state.committed_bool_values[storage_key] = value;
        state.committed_values.erase(storage_key);
        dirty_ = true;
        commit_attempted_ = false;
        return true;
    }

    bool Commit() {
        std::lock_guard<std::mutex> lock(rodakos_test::SettingsStateMutex());
        auto& state = rodakos_test::SettingsState();
        ++state.commit_calls;
        state.operations.push_back("commit:" + namespace_);
        commit_attempted_ = true;
        if (!read_write_ || !state.commit_result) {
            return false;
        }
        dirty_ = false;
        return true;
    }

private:
    std::string namespace_;
    bool read_write_ = false;
    bool dirty_ = false;
    bool commit_attempted_ = false;
};
