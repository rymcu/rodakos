#pragma once

#include "apps/home/home_layout_model.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace rodakos {

enum class HomeLayoutLoadStatus {
    kMissing,
    kLoaded,
    kCorrupt,
    kTooLarge,
    kUnsupportedVersion,
    kStorageError,
};

struct HomeLayoutLoadResult {
    HomeLayoutLoadStatus status = HomeLayoutLoadStatus::kStorageError;
    HomeLayout layout;
    uint32_t source_version = 0;
    bool reconciled = false;
    bool write_allowed = false;
};

enum class HomeLayoutSaveStatus {
    kSaved,
    kNotLoaded,
    kReadOnly,
    kResetNotAllowed,
    kConflict,
    kCompareError,
    kStaleRevision,
    kRevisionOverflow,
    kInvalidModel,
    kTooLarge,
    kEncodeError,
    kWriteError,
    kWriteUncertain,
    kCommitError,
};

struct HomeLayoutSaveResult {
    HomeLayoutSaveStatus status = HomeLayoutSaveStatus::kNotLoaded;
    std::optional<HomeLayout> layout;
};

bool HomeLayoutSaveLocksEditing(HomeLayoutSaveStatus status);

class HomeLayoutStore {
public:
    HomeLayoutLoadResult Load(
        const std::vector<std::string>& visible_app_ids,
        const std::vector<HomeAppIdMigration>& migrations = {});
    HomeLayoutSaveResult Save(const HomeLayout& candidate);
    HomeLayoutSaveResult Reset(const HomeLayout& replacement);

    HomeLayoutStore() = default;
    HomeLayoutStore(const HomeLayoutStore&) = delete;
    HomeLayoutStore& operator=(const HomeLayoutStore&) = delete;

    bool loaded() const { return loaded_; }
    bool write_allowed() const { return write_allowed_; }

private:
    enum class SourceState {
        kMissing,
        kValue,
        kTypeMismatch,
        kTooLarge,
        kError,
    };

    enum class SourceCheckResult {
        kUnchanged,
        kChanged,
        kError,
    };

    SourceCheckResult CheckSource() const;
    HomeLayoutSaveResult Persist(const HomeLayout& candidate, uint32_t next_revision);
    void FreezeWrites();

    bool loaded_ = false;
    bool write_allowed_ = false;
    bool reset_allowed_ = false;
    SourceState source_state_ = SourceState::kError;
    std::string source_encoded_;
    uint32_t current_revision_ = 0;
};

}  // namespace rodakos
