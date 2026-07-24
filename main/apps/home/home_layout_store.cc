#include "apps/home/home_layout_store.h"

#include "apps/home/home_layout_codec.h"
#include "settings.h"

#include <esp_log.h>

#include <limits>
#include <mutex>
#include <string>
#include <utility>

namespace rodakos {
namespace {

constexpr const char* TAG = "HomeLayout";
constexpr const char* kNamespace = "home";
constexpr const char* kLayoutKey = "layout";
std::mutex g_store_mutex;

HomeLayoutLoadResult FallbackResult(HomeLayoutLoadStatus status,
                                    const std::vector<std::string>& visible_app_ids,
                                    bool write_allowed,
                                    uint32_t source_version = 0) {
    HomeLayoutLoadResult result;
    result.status = status;
    result.layout = MakeDefaultHomeLayout(visible_app_ids);
    result.source_version = source_version;
    result.write_allowed = write_allowed;
    return result;
}

HomeLayoutSaveResult SaveFailure(HomeLayoutSaveStatus status) {
    HomeLayoutSaveResult result;
    result.status = status;
    return result;
}

}  // namespace

bool HomeLayoutSaveLocksEditing(HomeLayoutSaveStatus status) {
    switch (status) {
        case HomeLayoutSaveStatus::kNotLoaded:
        case HomeLayoutSaveStatus::kReadOnly:
        case HomeLayoutSaveStatus::kResetNotAllowed:
        case HomeLayoutSaveStatus::kConflict:
        case HomeLayoutSaveStatus::kCompareError:
        case HomeLayoutSaveStatus::kStaleRevision:
        case HomeLayoutSaveStatus::kRevisionOverflow:
        case HomeLayoutSaveStatus::kWriteError:
        case HomeLayoutSaveStatus::kWriteUncertain:
        case HomeLayoutSaveStatus::kCommitError:
            return true;
        case HomeLayoutSaveStatus::kSaved:
        case HomeLayoutSaveStatus::kInvalidModel:
        case HomeLayoutSaveStatus::kTooLarge:
        case HomeLayoutSaveStatus::kEncodeError:
            return false;
    }
    return true;
}

HomeLayoutLoadResult HomeLayoutStore::Load(
    const std::vector<std::string>& visible_app_ids,
    const std::vector<HomeAppIdMigration>& migrations) {
    std::lock_guard<std::mutex> lock(g_store_mutex);
    loaded_ = true;
    write_allowed_ = false;
    reset_allowed_ = false;
    source_state_ = SourceState::kError;
    source_encoded_.clear();
    current_revision_ = 0;

    Settings settings(kNamespace, false);
    std::string encoded;
    const SettingsStringReadStatus read_status =
        settings.ReadString(kLayoutKey, encoded, kHomeLayoutMaxJsonBytes);
    if (read_status == SettingsStringReadStatus::kNotFound) {
        source_state_ = SourceState::kMissing;
        write_allowed_ = true;
        reset_allowed_ = true;
        ESP_LOGI(TAG, "No saved Home layout; using Registry order");
        return FallbackResult(HomeLayoutLoadStatus::kMissing, visible_app_ids, true);
    }
    if (read_status == SettingsStringReadStatus::kTooLarge) {
        source_state_ = SourceState::kTooLarge;
        ESP_LOGE(TAG, "Saved Home layout exceeds %u bytes; preserving it read-only",
                 static_cast<unsigned>(kHomeLayoutMaxJsonBytes));
        return FallbackResult(HomeLayoutLoadStatus::kTooLarge, visible_app_ids, false);
    }
    if (read_status == SettingsStringReadStatus::kTypeMismatch) {
        source_state_ = SourceState::kTypeMismatch;
        ESP_LOGE(TAG, "Saved Home layout has the wrong NVS type; preserving it read-only");
        return FallbackResult(HomeLayoutLoadStatus::kCorrupt, visible_app_ids, false);
    }
    if (read_status != SettingsStringReadStatus::kOk) {
        source_state_ = SourceState::kError;
        ESP_LOGE(TAG, "Cannot read saved Home layout; disabling writes for this session");
        return FallbackResult(HomeLayoutLoadStatus::kStorageError, visible_app_ids, false);
    }

    source_state_ = SourceState::kValue;
    source_encoded_ = encoded;

    HomeLayoutDecodeResult decoded = DecodeHomeLayout(encoded);
    if (decoded.status == HomeLayoutDecodeStatus::kUnsupportedVersion) {
        ESP_LOGW(TAG, "Home layout schema v%u is newer or unsupported; preserving it read-only",
                 static_cast<unsigned>(decoded.source_version));
        return FallbackResult(HomeLayoutLoadStatus::kUnsupportedVersion, visible_app_ids, false,
                              decoded.source_version);
    }
    if (decoded.status == HomeLayoutDecodeStatus::kTooLarge) {
        return FallbackResult(HomeLayoutLoadStatus::kTooLarge, visible_app_ids, false);
    }
    if (decoded.status != HomeLayoutDecodeStatus::kOk) {
        reset_allowed_ = true;
        ESP_LOGE(TAG, "Saved Home layout is malformed; explicit reset is required");
        return FallbackResult(HomeLayoutLoadStatus::kCorrupt, visible_app_ids, false);
    }

    HomeLayoutReconcileResult reconciled =
        ReconcileHomeLayout(decoded.layout, visible_app_ids, migrations);
    current_revision_ = reconciled.layout.revision;
    write_allowed_ = true;
    reset_allowed_ = true;
    ESP_LOGI(TAG, "Loaded Home layout revision %u%s",
             static_cast<unsigned>(reconciled.layout.revision),
             reconciled.changed ? " with in-memory Registry reconciliation" : "");
    HomeLayoutLoadResult result;
    result.status = HomeLayoutLoadStatus::kLoaded;
    result.layout = std::move(reconciled.layout);
    result.source_version = decoded.source_version;
    result.reconciled = reconciled.changed;
    result.write_allowed = true;
    return result;
}

HomeLayoutSaveResult HomeLayoutStore::Save(const HomeLayout& candidate) {
    std::lock_guard<std::mutex> lock(g_store_mutex);
    if (!loaded_) {
        return SaveFailure(HomeLayoutSaveStatus::kNotLoaded);
    }
    if (!write_allowed_) {
        return SaveFailure(HomeLayoutSaveStatus::kReadOnly);
    }
    if (candidate.revision != current_revision_) {
        return SaveFailure(HomeLayoutSaveStatus::kStaleRevision);
    }
    if (candidate.revision == std::numeric_limits<uint32_t>::max()) {
        return SaveFailure(HomeLayoutSaveStatus::kRevisionOverflow);
    }
    const SourceCheckResult source_check = CheckSource();
    if (source_check != SourceCheckResult::kUnchanged) {
        FreezeWrites();
        return SaveFailure(source_check == SourceCheckResult::kError
                               ? HomeLayoutSaveStatus::kCompareError
                               : HomeLayoutSaveStatus::kConflict);
    }

    return Persist(candidate, candidate.revision + 1);
}

HomeLayoutSaveResult HomeLayoutStore::Reset(const HomeLayout& replacement) {
    std::lock_guard<std::mutex> lock(g_store_mutex);
    if (!loaded_) {
        return SaveFailure(HomeLayoutSaveStatus::kNotLoaded);
    }
    if (!reset_allowed_) {
        return SaveFailure(HomeLayoutSaveStatus::kResetNotAllowed);
    }
    if (replacement.revision != 0) {
        return SaveFailure(HomeLayoutSaveStatus::kStaleRevision);
    }
    const SourceCheckResult source_check = CheckSource();
    if (source_check != SourceCheckResult::kUnchanged) {
        FreezeWrites();
        return SaveFailure(source_check == SourceCheckResult::kError
                               ? HomeLayoutSaveStatus::kCompareError
                               : HomeLayoutSaveStatus::kConflict);
    }
    const uint32_t next_revision = write_allowed_
                                       ? current_revision_ == std::numeric_limits<uint32_t>::max()
                                             ? 0
                                             : current_revision_ + 1
                                       : 1;
    return Persist(replacement, next_revision);
}

HomeLayoutStore::SourceCheckResult HomeLayoutStore::CheckSource() const {
    Settings settings(kNamespace, false);
    std::string current;
    const SettingsStringReadStatus status =
        settings.ReadString(kLayoutKey, current, kHomeLayoutMaxJsonBytes);
    if (status == SettingsStringReadStatus::kError) {
        return SourceCheckResult::kError;
    }
    switch (source_state_) {
        case SourceState::kMissing:
            return status == SettingsStringReadStatus::kNotFound
                       ? SourceCheckResult::kUnchanged
                       : SourceCheckResult::kChanged;
        case SourceState::kValue:
            return status == SettingsStringReadStatus::kOk && current == source_encoded_
                       ? SourceCheckResult::kUnchanged
                       : SourceCheckResult::kChanged;
        case SourceState::kTypeMismatch:
            return SourceCheckResult::kError;
        case SourceState::kTooLarge:
            return status == SettingsStringReadStatus::kTooLarge
                       ? SourceCheckResult::kUnchanged
                       : SourceCheckResult::kChanged;
        case SourceState::kError:
            return SourceCheckResult::kError;
    }
    return SourceCheckResult::kError;
}

HomeLayoutSaveResult HomeLayoutStore::Persist(const HomeLayout& candidate,
                                              uint32_t next_revision) {
    HomeLayout next = candidate;
    next.revision = next_revision;

    if (ValidateHomeLayout(next) != HomeLayoutValidationStatus::kOk) {
        return SaveFailure(HomeLayoutSaveStatus::kInvalidModel);
    }

    HomeLayoutEncodeResult encoded = EncodeHomeLayout(next);
    if (encoded.status == HomeLayoutEncodeStatus::kTooLarge) {
        return SaveFailure(HomeLayoutSaveStatus::kTooLarge);
    }
    if (encoded.status == HomeLayoutEncodeStatus::kInvalidModel) {
        return SaveFailure(HomeLayoutSaveStatus::kInvalidModel);
    }
    if (encoded.status != HomeLayoutEncodeStatus::kOk) {
        return SaveFailure(HomeLayoutSaveStatus::kEncodeError);
    }

    Settings settings(kNamespace, true);
    const SettingsStringWriteStatus write_status = settings.WriteString(kLayoutKey, encoded.json);
    if (write_status != SettingsStringWriteStatus::kOk) {
        ESP_LOGE(TAG, "Failed to stage Home layout revision %u",
                 static_cast<unsigned>(next.revision));
        FreezeWrites();
        return SaveFailure(write_status == SettingsStringWriteStatus::kRemoveFailed
                               ? HomeLayoutSaveStatus::kWriteUncertain
                               : HomeLayoutSaveStatus::kWriteError);
    }
    if (!settings.Commit()) {
        ESP_LOGE(TAG, "Failed to commit Home layout revision %u",
                 static_cast<unsigned>(next.revision));
        FreezeWrites();
        return SaveFailure(HomeLayoutSaveStatus::kCommitError);
    }

    source_state_ = SourceState::kValue;
    source_encoded_ = encoded.json;
    current_revision_ = next.revision;
    write_allowed_ = true;
    reset_allowed_ = true;
    ESP_LOGI(TAG, "Saved Home layout revision %u", static_cast<unsigned>(next.revision));
    HomeLayoutSaveResult result;
    result.status = HomeLayoutSaveStatus::kSaved;
    result.layout = std::move(next);
    return result;
}

void HomeLayoutStore::FreezeWrites() {
    write_allowed_ = false;
    reset_allowed_ = false;
}

}  // namespace rodakos
