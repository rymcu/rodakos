#pragma once
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>

enum class SettingsStringReadStatus { kOk, kNotFound, kTypeMismatch, kTooLarge, kError };
enum class SettingsStringWriteStatus { kOk, kRemoveFailed, kError };
enum class SettingsBoolReadStatus { kOk, kNotFound, kTypeMismatch, kError };
namespace wake_host {
struct WriteFault {
    SettingsStringWriteStatus status = SettingsStringWriteStatus::kOk;
    bool changes_storage = true;
    bool commit_ok = true;
};
struct Storage {
    std::map<std::string, std::string> strings;
    std::map<std::string, bool> booleans;
    std::deque<WriteFault> writes;
    std::optional<SettingsStringReadStatus> read_error;
    unsigned write_calls = 0;
    unsigned commit_calls = 0;
};
inline Storage& Store() { static Storage value; return value; }
inline std::mutex& StoreMutex() { static std::mutex value; return value; }
inline void ResetStore() { std::lock_guard<std::mutex> lock(StoreMutex()); Store() = {}; }
inline void Fault(WriteFault fault) { std::lock_guard<std::mutex> lock(StoreMutex()); Store().writes.push_back(fault); }
inline std::string Key(const std::string& ns, const std::string& key) { return ns + ":" + key; }
}
class Settings {
public:
    explicit Settings(std::string ns, bool writable = false) : ns_(std::move(ns)), writable_(writable) {}
    SettingsStringReadStatus ReadString(const std::string& key, std::string& value, size_t limit) {
        std::lock_guard<std::mutex> lock(wake_host::StoreMutex());
        value.clear();
        auto& store = wake_host::Store();
        if (store.read_error) return *store.read_error;
        const auto item = store.strings.find(wake_host::Key(ns_, key));
        if (item == store.strings.end()) return SettingsStringReadStatus::kNotFound;
        if (item->second.size() > limit) return SettingsStringReadStatus::kTooLarge;
        value = item->second;
        return SettingsStringReadStatus::kOk;
    }
    std::string GetString(const std::string& key, const std::string& fallback = "") {
        std::string value;
        return ReadString(key, value, static_cast<size_t>(-1)) == SettingsStringReadStatus::kOk ? value : fallback;
    }
    SettingsStringWriteStatus WriteString(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(wake_host::StoreMutex());
        if (!writable_) return SettingsStringWriteStatus::kError;
        auto& store = wake_host::Store();
        ++store.write_calls;
        wake_host::WriteFault fault;
        if (!store.writes.empty()) { fault = store.writes.front(); store.writes.pop_front(); }
        commit_ok_ = fault.commit_ok;
        if (fault.changes_storage) store.strings[wake_host::Key(ns_, key)] = value;
        return fault.status;
    }
    bool SetString(const std::string& key, const std::string& value) {
        return WriteString(key, value) == SettingsStringWriteStatus::kOk;
    }
    SettingsBoolReadStatus ReadBool(const std::string& key, bool& value) {
        std::lock_guard<std::mutex> lock(wake_host::StoreMutex());
        const auto item = wake_host::Store().booleans.find(wake_host::Key(ns_, key));
        if (item == wake_host::Store().booleans.end()) return SettingsBoolReadStatus::kNotFound;
        value = item->second;
        return SettingsBoolReadStatus::kOk;
    }
    bool SetBool(const std::string& key, bool value) {
        std::lock_guard<std::mutex> lock(wake_host::StoreMutex());
        if (!writable_) return false;
        wake_host::Store().booleans[wake_host::Key(ns_, key)] = value;
        return true;
    }
    bool Commit() {
        std::lock_guard<std::mutex> lock(wake_host::StoreMutex());
        ++wake_host::Store().commit_calls;
        return writable_ && commit_ok_;
    }
private:
    std::string ns_;
    bool writable_ = false;
    bool commit_ok_ = true;
};
