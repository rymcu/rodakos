#include "phone_os/inbox_model.h"

#include <cJSON.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <utility>

namespace rodakos {
namespace {

using CJsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

// Cut at a code point boundary so a long server string never ends in half a character.
std::string TruncateUtf8(const char* text, size_t max_bytes) {
    std::string value = text != nullptr ? text : "";
    if (value.size() <= max_bytes) return value;
    size_t end = max_bytes;
    while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xC0) == 0x80) --end;
    value.resize(end);
    return value;
}

bool ReadId(const cJSON* object, const char* name, std::string& out, bool required) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, name);
    if (value == nullptr) {
        out.clear();
        return !required;
    }
    if (!cJSON_IsString(value) || value->valuestring == nullptr) return false;
    const size_t length = std::strlen(value->valuestring);
    if (length == 0 || length > kInboxMaxIdBytes) return false;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char ch = static_cast<unsigned char>(value->valuestring[i]);
        if (ch < 0x21 || ch > 0x7E) return false;
    }
    out = value->valuestring;
    return true;
}

bool ReadText(const cJSON* object, const char* name, size_t max_bytes, std::string& out,
              bool required) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, name);
    if (value == nullptr) {
        out.clear();
        return !required;
    }
    if (!cJSON_IsString(value) || value->valuestring == nullptr) return false;
    out = TruncateUtf8(value->valuestring, max_bytes);
    return !required || !out.empty();
}

bool ReadUint32(const cJSON* object, const char* name, uint32_t& out, bool required) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, name);
    if (value == nullptr) {
        out = 0;
        return !required;
    }
    if (!cJSON_IsNumber(value) || !(value->valuedouble >= 0 && value->valuedouble <= UINT32_MAX)) {
        return false;
    }
    const auto parsed = static_cast<uint32_t>(value->valuedouble);
    if (static_cast<double>(parsed) != value->valuedouble) return false;
    out = parsed;
    return true;
}

bool ReadSeconds(const cJSON* object, const char* name, int64_t& out) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, name);
    out = 0;
    if (value == nullptr) return true;
    // Unix seconds; 2^41 is far beyond any real date but still exact in a double.
    if (!cJSON_IsNumber(value) || !(value->valuedouble >= 0 && value->valuedouble < 2199023255552.0) ||
        std::floor(value->valuedouble) != value->valuedouble) {
        return false;
    }
    out = static_cast<int64_t>(value->valuedouble);
    return true;
}

bool ParseItem(const cJSON* value, InboxItem& item) {
    if (!cJSON_IsObject(value)) return false;
    item = {};
    const cJSON* kind = cJSON_GetObjectItemCaseSensitive(value, "kind");
    if (!cJSON_IsString(kind) || kind->valuestring == nullptr) return false;
    if (std::strcmp(kind->valuestring, "notice") == 0) {
        item.kind = InboxKind::kNotice;
    } else if (std::strcmp(kind->valuestring, "todo") == 0) {
        item.kind = InboxKind::kTodo;
    } else {
        return false;
    }
    const cJSON* unread = cJSON_GetObjectItemCaseSensitive(value, "unread");
    if (!ReadId(value, "noticeId", item.notice_id, true) ||
        !ReadText(value, "title", kInboxMaxTitleBytes, item.title, true) ||
        !ReadText(value, "summary", kInboxMaxSummaryBytes, item.summary, false) ||
        !ReadText(value, "body", kInboxMaxBodyBytes, item.body, false) ||
        !ReadSeconds(value, "createdAt", item.created_at) ||
        !ReadUint32(value, "revision", item.revision, true) || !cJSON_IsBool(unread)) {
        return false;
    }
    item.unread = cJSON_IsTrue(unread);
    if (item.kind == InboxKind::kNotice) {
        return cJSON_GetObjectItemCaseSensitive(value, "todo") == nullptr;
    }
    const cJSON* todo = cJSON_GetObjectItemCaseSensitive(value, "todo");
    if (!cJSON_IsObject(todo)) return false;
    const cJSON* state = cJSON_GetObjectItemCaseSensitive(todo, "state");
    if (!cJSON_IsString(state) || state->valuestring == nullptr) return false;
    if (std::strcmp(state->valuestring, "open") == 0) {
        item.todo_state = InboxTodoState::kOpen;
    } else if (std::strcmp(state->valuestring, "done") == 0) {
        item.todo_state = InboxTodoState::kDone;
    } else if (std::strcmp(state->valuestring, "snoozed") == 0) {
        item.todo_state = InboxTodoState::kSnoozed;
    } else {
        return false;
    }
    return ReadId(todo, "taskId", item.task_id, true) &&
           ReadId(todo, "occurrenceId", item.occurrence_id, true) &&
           ReadSeconds(todo, "dueAt", item.due_at) &&
           ReadSeconds(todo, "snoozedUntil", item.snoozed_until) &&
           (item.todo_state != InboxTodoState::kSnoozed || item.snoozed_until != 0);
}

const char* ActionName(InboxActionKind kind) {
    switch (kind) {
        case InboxActionKind::kMarkRead:
            return "read";
        case InboxActionKind::kComplete:
            return "complete";
        case InboxActionKind::kSnooze:
        default:
            return "snooze";
    }
}

}  // namespace

bool ParseInboxSnapshot(const cJSON* data, InboxSnapshot& snapshot, std::string& error) {
    snapshot = {};
    error.clear();
    if (!cJSON_IsObject(data)) {
        error = "inbox snapshot must be an object";
        return false;
    }
    uint32_t schema_version = 0;
    if (!ReadUint32(data, "schemaVersion", schema_version, true) || schema_version != 1) {
        error = "unsupported inbox schemaVersion";
        return false;
    }
    InboxSnapshot parsed;
    if (!ReadUint32(data, "inboxRevision", parsed.inbox_revision, true) ||
        !ReadUint32(data, "unreadCount", parsed.unread_count, true)) {
        error = "inbox snapshot counters are invalid";
        return false;
    }
    const cJSON* items = cJSON_GetObjectItemCaseSensitive(data, "items");
    if (!cJSON_IsArray(items)) {
        error = "inbox snapshot items must be an array";
        return false;
    }
    parsed.items.reserve(std::min<size_t>(cJSON_GetArraySize(items), kInboxMaxItems));
    const cJSON* entry = nullptr;
    cJSON_ArrayForEach(entry, items) {
        if (parsed.items.size() == kInboxMaxItems) break;
        InboxItem item;
        if (!ParseItem(entry, item)) {
            error = "inbox snapshot has an invalid item";
            return false;
        }
        const bool duplicate = std::any_of(parsed.items.begin(), parsed.items.end(),
            [&](const InboxItem& other) { return other.notice_id == item.notice_id; });
        if (duplicate) {
            error = "inbox snapshot repeats a noticeId";
            return false;
        }
        parsed.items.push_back(std::move(item));
    }
    snapshot = std::move(parsed);
    return true;
}

bool ParseInboxChangedSignal(const std::string& payload, InboxChangedSignal& signal) {
    signal = {};
    if (payload.empty()) return true;
    if (payload.size() > 512) return false;
    CJsonPtr root(cJSON_ParseWithLength(payload.data(), payload.size()), cJSON_Delete);
    if (!cJSON_IsObject(root.get())) return false;
    uint32_t schema_version = 0;
    return ReadUint32(root.get(), "schemaVersion", schema_version, true) && schema_version == 1 &&
           ReadUint32(root.get(), "inboxRevision", signal.inbox_revision, true) &&
           ReadUint32(root.get(), "unreadCount", signal.unread_count, true);
}

bool IsInboxSnoozeChoice(uint32_t minutes) {
    return std::find(std::begin(kInboxSnoozeChoicesMinutes), std::end(kInboxSnoozeChoicesMinutes),
                     minutes) != std::end(kInboxSnoozeChoicesMinutes);
}

std::string BuildInboxActionBody(InboxActionKind kind, const InboxItem& item,
                                 const std::string& op_id, uint32_t snooze_minutes) {
    CJsonPtr root(cJSON_CreateObject(), cJSON_Delete);
    if (root == nullptr) return {};
    cJSON_AddStringToObject(root.get(), "opId", op_id.c_str());
    cJSON_AddStringToObject(root.get(), "action", ActionName(kind));
    cJSON_AddStringToObject(root.get(), "noticeId", item.notice_id.c_str());
    cJSON_AddNumberToObject(root.get(), "expectedRevision", item.revision);
    if (kind == InboxActionKind::kComplete) {
        cJSON_AddStringToObject(root.get(), "taskId", item.task_id.c_str());
    } else if (kind == InboxActionKind::kSnooze) {
        cJSON_AddStringToObject(root.get(), "occurrenceId", item.occurrence_id.c_str());
        cJSON_AddNumberToObject(root.get(), "snoozeMinutes", snooze_minutes);
    }
    char* text = cJSON_PrintUnformatted(root.get());
    if (text == nullptr) return {};
    std::string body(text);
    cJSON_free(text);
    return body;
}

bool ParseInboxActionResult(const cJSON* data, InboxActionResult& result, std::string& error) {
    result = {};
    error.clear();
    if (!cJSON_IsObject(data)) {
        error = "inbox action result must be an object";
        return false;
    }
    const cJSON* outcome = cJSON_GetObjectItemCaseSensitive(data, "outcome");
    if (!cJSON_IsString(outcome) || outcome->valuestring == nullptr) {
        error = "inbox action outcome is missing";
        return false;
    }
    if (std::strcmp(outcome->valuestring, "applied") == 0) {
        result.outcome = InboxActionOutcome::kApplied;
    } else if (std::strcmp(outcome->valuestring, "conflict") == 0) {
        result.outcome = InboxActionOutcome::kConflict;
    } else if (std::strcmp(outcome->valuestring, "rejected") == 0) {
        result.outcome = InboxActionOutcome::kRejected;
    } else {
        error = "inbox action outcome is unknown";
        return false;
    }
    if (!ReadUint32(data, "inboxRevision", result.inbox_revision, true)) {
        error = "inbox action inboxRevision is invalid";
        return false;
    }
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(data, "item");
    if (item != nullptr && !ParseItem(item, result.item)) {
        error = "inbox action item is invalid";
        return false;
    }
    return true;
}

void InboxModel::SetBound(bool bound) {
    if (bound_ == bound) return;
    bound_ = bound;
    if (!bound) {
        // Unbind or rebind: nothing of the previous binding may remain visible.
        synced_ = false;
        syncing_ = false;
        last_sync_at_ = 0;
        inbox_revision_ = 0;
        server_unread_ = 0;
        std::vector<InboxItem>().swap(items_);
    }
    Touch();
}

void InboxModel::SetOnline(bool online) {
    if (online_ == online) return;
    online_ = online;
    Touch();
}

void InboxModel::SetSyncing(bool syncing) {
    if (syncing_ == syncing) return;
    syncing_ = syncing;
    Touch();
}

void InboxModel::ApplySnapshot(InboxSnapshot snapshot, int64_t now_s) {
    if (!bound_) return;
    for (InboxItem& item : snapshot.items) {
        const InboxItem* previous = Find(item.notice_id);
        if (previous == nullptr || previous->op_state == InboxOpState::kNone) continue;
        // A newer revision means the server already reflects some change; a reconcile retry
        // then reports the original outcome for its opId, so the state is still kept.
        item.op_state = previous->op_state;
        item.op_id = previous->op_id;
        item.op_action = previous->op_action;
        item.op_snooze_minutes = previous->op_snooze_minutes;
        if (item.kind == InboxKind::kTodo && item.todo_state == InboxTodoState::kDone &&
            previous->op_action == "complete") {
            item.op_state = InboxOpState::kNone;
            item.op_id.clear();
            item.op_action.clear();
        }
    }
    items_ = std::move(snapshot.items);
    inbox_revision_ = snapshot.inbox_revision;
    server_unread_ = snapshot.unread_count;
    synced_ = true;
    last_sync_at_ = now_s;
    Touch();
}

bool InboxModel::ApplyChangedSignal(const InboxChangedSignal& signal) {
    if (!bound_) return false;
    // An empty or old signal still asks for a refresh; only a revision already held is skipped.
    if (synced_ && signal.inbox_revision != 0 && signal.inbox_revision == inbox_revision_) {
        return false;
    }
    if (signal.inbox_revision != 0 && signal.unread_count != server_unread_) {
        server_unread_ = signal.unread_count;
        Touch();
    }
    return true;
}

bool InboxModel::MarkReadLocally(const std::string& notice_id) {
    InboxItem* item = FindMutable(notice_id);
    if (item == nullptr || !item->unread) return false;
    item->unread = false;
    if (server_unread_ > 0) --server_unread_;
    Touch();
    return true;
}

bool InboxModel::BeginAction(const std::string& notice_id, InboxActionKind kind,
                             uint32_t snooze_minutes, const std::string& op_id) {
    InboxItem* item = FindMutable(notice_id);
    if (item == nullptr || item->kind != InboxKind::kTodo || op_id.empty() ||
        item->todo_state == InboxTodoState::kDone || item->op_state == InboxOpState::kSubmitting ||
        kind == InboxActionKind::kMarkRead ||
        (kind == InboxActionKind::kSnooze && !IsInboxSnoozeChoice(snooze_minutes))) {
        return false;
    }
    const char* action = ActionName(kind);
    // A pending reconcile may only be retried as the same request with the same opId.
    if (item->op_state == InboxOpState::kPendingReconcile &&
        (item->op_id != op_id || item->op_action != action ||
         item->op_snooze_minutes != snooze_minutes)) {
        return false;
    }
    item->op_state = InboxOpState::kSubmitting;
    item->op_id = op_id;
    item->op_action = action;
    item->op_snooze_minutes = kind == InboxActionKind::kSnooze ? snooze_minutes : 0;
    Touch();
    return true;
}

void InboxModel::FinishAction(const std::string& notice_id, const InboxActionResult& result) {
    InboxItem* item = FindMutable(notice_id);
    if (item == nullptr) return;
    item->op_state = InboxOpState::kNone;
    item->op_id.clear();
    item->op_action.clear();
    item->op_snooze_minutes = 0;
    if (!result.item.notice_id.empty() && result.item.notice_id == notice_id) {
        *item = result.item;
    }
    if (result.inbox_revision > inbox_revision_) {
        inbox_revision_ = result.inbox_revision;
    }
    Touch();
}

void InboxModel::FailAction(const std::string& notice_id) {
    InboxItem* item = FindMutable(notice_id);
    if (item == nullptr || item->op_state != InboxOpState::kSubmitting) return;
    item->op_state = InboxOpState::kPendingReconcile;
    Touch();
}

const InboxItem* InboxModel::Find(const std::string& notice_id) const {
    auto found = std::find_if(items_.begin(), items_.end(),
                              [&](const InboxItem& item) { return item.notice_id == notice_id; });
    return found != items_.end() ? &*found : nullptr;
}

InboxItem* InboxModel::FindMutable(const std::string& notice_id) {
    return const_cast<InboxItem*>(Find(notice_id));
}

uint32_t InboxModel::CountUnread() const {
    // The server count covers records beyond the cached list; local reads lower it at once.
    const auto listed = static_cast<uint32_t>(std::count_if(
        items_.begin(), items_.end(), [](const InboxItem& item) { return item.unread; }));
    return std::max(listed, server_unread_);
}

uint32_t InboxModel::CountOpenTodos() const {
    return static_cast<uint32_t>(std::count_if(items_.begin(), items_.end(),
        [](const InboxItem& item) {
            return item.kind == InboxKind::kTodo && item.todo_state != InboxTodoState::kDone;
        }));
}

InboxSummary InboxModel::Summary() const {
    InboxSummary summary;
    summary.bound = bound_;
    summary.synced = synced_;
    if (bound_) {
        summary.unread = CountUnread();
        summary.open_todos = CountOpenTodos();
    }
    summary.revision = revision_;
    return summary;
}

InboxView InboxModel::View() const {
    InboxView view;
    view.bound = bound_;
    view.synced = synced_;
    view.syncing = syncing_;
    view.online = online_;
    view.last_sync_at = last_sync_at_;
    view.inbox_revision = inbox_revision_;
    if (bound_) {
        view.unread = CountUnread();
        view.open_todos = CountOpenTodos();
        view.items = items_;
    }
    view.revision = revision_;
    return view;
}

}  // namespace rodakos
