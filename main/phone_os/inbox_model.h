#pragma once

#include "phone_os/inbox_summary.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct cJSON;

namespace rodakos {

// Message center wire model (docs/rodak-aiot-contract-v1.md section 7.6). The server owns every
// record; the device keeps a bounded copy in RAM only, never in NVS: the 24 KiB NVS partition
// already holds credentials and trust anchors, and an unbind must leave no message text behind.

enum class InboxKind {
    kNotice,
    kTodo,
};

enum class InboxTodoState {
    kOpen,
    kDone,
    kSnoozed,
};

// Device-local progress of a completion or snooze request for one record.
enum class InboxOpState {
    kNone,
    // Sent; waiting for the server's answer.
    kSubmitting,
    // The answer did not arrive. The same opId is sent again on retry, so the server either
    // returns the first result or applies the request once.
    kPendingReconcile,
};

struct InboxItem {
    std::string notice_id;
    InboxKind kind = InboxKind::kNotice;
    std::string title;
    // One-line preview; the full text is in body.
    std::string summary;
    std::string body;
    // Unix seconds, 0 when the server sent none.
    int64_t created_at = 0;
    bool unread = false;
    uint32_t revision = 0;
    // Todo only.
    std::string task_id;
    std::string occurrence_id;
    InboxTodoState todo_state = InboxTodoState::kOpen;
    int64_t due_at = 0;
    int64_t snoozed_until = 0;
    // Device-local.
    InboxOpState op_state = InboxOpState::kNone;
    std::string op_id;
    std::string op_action;
    uint32_t op_snooze_minutes = 0;
};

// Limits that keep a hostile or buggy server from exhausting RAM.
inline constexpr size_t kInboxMaxItems = 40;
inline constexpr size_t kInboxMaxIdBytes = 64;
inline constexpr size_t kInboxMaxTitleBytes = 160;
inline constexpr size_t kInboxMaxSummaryBytes = 240;
inline constexpr size_t kInboxMaxBodyBytes = 2048;
inline constexpr size_t kInboxMaxResponseBytes = 48 * 1024;

struct InboxSnapshot {
    uint32_t inbox_revision = 0;
    uint32_t unread_count = 0;
    std::vector<InboxItem> items;
};

// inbox/changed MQTT payload: a dirty signal without message text.
struct InboxChangedSignal {
    uint32_t inbox_revision = 0;
    uint32_t unread_count = 0;
};

// Parse GET /api/v1/aiot/devices/inbox data. Items beyond kInboxMaxItems are dropped (the server
// sends newest first). A malformed item fails the whole snapshot so a partial list never
// replaces a good one.
bool ParseInboxSnapshot(const cJSON* data, InboxSnapshot& snapshot, std::string& error);
// Parse the inbox/changed payload. An empty payload is valid and carries no revision.
bool ParseInboxChangedSignal(const std::string& payload, InboxChangedSignal& signal);

enum class InboxActionKind {
    kMarkRead,
    kComplete,
    kSnooze,
};

inline constexpr uint32_t kInboxSnoozeChoicesMinutes[] = {10, 30, 60};
bool IsInboxSnoozeChoice(uint32_t minutes);

// POST /api/v1/aiot/devices/inbox/actions body.
std::string BuildInboxActionBody(InboxActionKind kind, const InboxItem& item,
                                 const std::string& op_id, uint32_t snooze_minutes);

enum class InboxActionOutcome {
    // The server applied the request, or had applied it under the same opId earlier.
    kApplied,
    // The record changed elsewhere (revision or occurrence mismatch). The device refreshes and
    // shows the server state; it is not an error for the user.
    kConflict,
    // The server refused the request for good (unknown record, not allowed).
    kRejected,
};

struct InboxActionResult {
    InboxActionOutcome outcome = InboxActionOutcome::kRejected;
    uint32_t inbox_revision = 0;
    // The record as the server now has it; empty notice_id when the server sent none.
    InboxItem item;
};

bool ParseInboxActionResult(const cJSON* data, InboxActionResult& result, std::string& error);

// What the message center UI renders. Built by InboxModel under the service lock and copied out.
struct InboxView {
    bool bound = false;
    bool synced = false;
    bool syncing = false;
    bool online = false;
    // Unix seconds of the last successful snapshot, 0 when none.
    int64_t last_sync_at = 0;
    uint32_t inbox_revision = 0;
    uint32_t unread = 0;
    uint32_t open_todos = 0;
    std::vector<InboxItem> items;
    // Increases whenever any field changes.
    uint32_t revision = 0;
};

// The device-side inbox state machine. Pure and host-testable: the service feeds it network
// results and the UI reads views. Not thread-safe.
class InboxModel {
public:
    void SetBound(bool bound);
    void SetOnline(bool online);
    void SetSyncing(bool syncing);

    // A full snapshot replaces the list. Local operation states survive for records the server
    // still lists, so a pending reconcile is not lost by a refresh.
    void ApplySnapshot(InboxSnapshot snapshot, int64_t now_s);
    // A dirty signal. Returns true when the device should fetch a snapshot.
    bool ApplyChangedSignal(const InboxChangedSignal& signal);

    // Mark read locally right away; the request follows in the background.
    bool MarkReadLocally(const std::string& notice_id);
    // Start a completion or snooze. Returns false when the record is unknown, not a todo, already
    // done or already submitting. op_id must be unique per request; a retry passes the stored one.
    bool BeginAction(const std::string& notice_id, InboxActionKind kind, uint32_t snooze_minutes,
                     const std::string& op_id);
    void FinishAction(const std::string& notice_id, const InboxActionResult& result);
    // The request failed without an answer: keep the opId for a retry.
    void FailAction(const std::string& notice_id);

    const InboxItem* Find(const std::string& notice_id) const;
    InboxSummary Summary() const;
    InboxView View() const;
    uint32_t revision() const { return revision_; }
    uint32_t inbox_revision() const { return inbox_revision_; }
    bool synced() const { return synced_; }

private:
    InboxItem* FindMutable(const std::string& notice_id);
    void Touch() { ++revision_; }
    uint32_t CountUnread() const;
    uint32_t CountOpenTodos() const;

    bool bound_ = false;
    bool synced_ = false;
    bool syncing_ = false;
    bool online_ = false;
    int64_t last_sync_at_ = 0;
    uint32_t inbox_revision_ = 0;
    // Server-reported unread count of the last signal or snapshot; the list may hold fewer.
    uint32_t server_unread_ = 0;
    std::vector<InboxItem> items_;
    uint32_t revision_ = 1;
};

}  // namespace rodakos
