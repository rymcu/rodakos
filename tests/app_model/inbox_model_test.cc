#include "test_framework.h"

#include "phone_os/inbox_model.h"

#include <cJSON.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#ifndef RODAK_AIOT_CONTRACT_DIR
#error "RODAK_AIOT_CONTRACT_DIR must point at contracts/rodak-aiot/v1"
#endif

namespace {
using namespace rodakos;
using CJsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

CJsonPtr Parse(const char* text) { return CJsonPtr(cJSON_Parse(text), cJSON_Delete); }

constexpr const char* kSnapshot = R"({
  "schemaVersion": 1, "inboxRevision": 12, "unreadCount": 2,
  "items": [
    {"noticeId": "n-1", "kind": "todo", "title": "给阳台的花浇水", "summary": "18:30",
     "body": "每周三和周六浇一次。", "createdAt": 1795000000, "unread": true, "revision": 3,
     "todo": {"taskId": "t-1", "occurrenceId": "o-1", "state": "open", "dueAt": 1795003600}},
    {"noticeId": "n-2", "kind": "notice", "title": "照片整理完成", "createdAt": 1794990000,
     "unread": true, "revision": 1}
  ]
})";

InboxModel BoundModelWithSnapshot() {
    InboxModel model;
    model.SetBound(true);
    InboxSnapshot snapshot;
    std::string error;
    auto root = Parse(kSnapshot);
    RODAK_CHECK(ParseInboxSnapshot(root.get(), snapshot, error));
    model.ApplySnapshot(std::move(snapshot), 1795000100);
    model.SetOnline(true);
    return model;
}
}  // namespace

RODAK_TEST("inbox snapshot parses todos and notices and rejects malformed items") {
    InboxSnapshot snapshot;
    std::string error;
    auto root = Parse(kSnapshot);
    RODAK_CHECK(ParseInboxSnapshot(root.get(), snapshot, error));
    RODAK_CHECK_EQ(snapshot.inbox_revision, 12u);
    RODAK_CHECK_EQ(snapshot.items.size(), size_t{2});
    RODAK_CHECK(snapshot.items[0].kind == InboxKind::kTodo);
    RODAK_CHECK_EQ(snapshot.items[0].occurrence_id, std::string("o-1"));
    RODAK_CHECK(snapshot.items[1].kind == InboxKind::kNotice);

    for (const char* bad : {
             R"({"schemaVersion":2,"inboxRevision":1,"unreadCount":0,"items":[]})",
             R"({"schemaVersion":1,"inboxRevision":1,"unreadCount":0,"items":[{"noticeId":"a","kind":"todo","title":"x","unread":false,"revision":1}]})",
             R"({"schemaVersion":1,"inboxRevision":1,"unreadCount":0,"items":[{"noticeId":"a b","kind":"notice","title":"x","unread":false,"revision":1}]})",
             R"({"schemaVersion":1,"inboxRevision":1,"unreadCount":0,"items":[{"noticeId":"a","kind":"notice","title":"x","unread":false,"revision":1},{"noticeId":"a","kind":"notice","title":"y","unread":false,"revision":1}]})",
         }) {
        auto value = Parse(bad);
        RODAK_CHECK_FALSE(ParseInboxSnapshot(value.get(), snapshot, error));
    }
}

RODAK_TEST("inbox snapshot keeps at most the newest bounded items and truncates long text") {
    std::string text = R"({"schemaVersion":1,"inboxRevision":1,"unreadCount":0,"items":[)";
    for (size_t index = 0; index < kInboxMaxItems + 5; ++index) {
        if (index != 0) text += ",";
        text += R"({"noticeId":"n)" + std::to_string(index) +
                R"(","kind":"notice","title":")" + std::string(400, 'a') +
                R"(","unread":false,"revision":1})";
    }
    text += "]}";
    auto root = Parse(text.c_str());
    InboxSnapshot snapshot;
    std::string error;
    RODAK_CHECK(ParseInboxSnapshot(root.get(), snapshot, error));
    RODAK_CHECK_EQ(snapshot.items.size(), kInboxMaxItems);
    RODAK_CHECK_EQ(snapshot.items[0].title.size(), kInboxMaxTitleBytes);
}

RODAK_TEST("inbox changed signal asks for a refresh unless the revision is already held") {
    InboxModel model = BoundModelWithSnapshot();
    InboxChangedSignal signal;
    RODAK_CHECK(ParseInboxChangedSignal("", signal));
    RODAK_CHECK(model.ApplyChangedSignal(signal));
    RODAK_CHECK(ParseInboxChangedSignal(R"({"schemaVersion":1,"inboxRevision":12,"unreadCount":2})", signal));
    RODAK_CHECK_FALSE(model.ApplyChangedSignal(signal));
    RODAK_CHECK(ParseInboxChangedSignal(R"({"schemaVersion":1,"inboxRevision":13,"unreadCount":5})", signal));
    RODAK_CHECK(model.ApplyChangedSignal(signal));
    RODAK_CHECK_EQ(model.Summary().unread, 5u);
    RODAK_CHECK_FALSE(ParseInboxChangedSignal(R"({"inboxRevision":13})", signal));
}

RODAK_TEST("inbox unbind clears every record and ignores late snapshots") {
    InboxModel model = BoundModelWithSnapshot();
    RODAK_CHECK_EQ(model.Summary().unread, 2u);
    RODAK_CHECK_EQ(model.Summary().open_todos, 1u);
    model.SetBound(false);
    const auto summary = model.Summary();
    RODAK_CHECK_FALSE(summary.bound);
    RODAK_CHECK_EQ(summary.unread, 0u);
    RODAK_CHECK(model.View().items.empty());
    InboxSnapshot snapshot;
    std::string error;
    auto root = Parse(kSnapshot);
    RODAK_CHECK(ParseInboxSnapshot(root.get(), snapshot, error));
    model.ApplySnapshot(std::move(snapshot), 0);
    RODAK_CHECK(model.View().items.empty());
}

RODAK_TEST("inbox mark read lowers the unread count at once") {
    InboxModel model = BoundModelWithSnapshot();
    RODAK_CHECK(model.MarkReadLocally("n-2"));
    RODAK_CHECK_EQ(model.Summary().unread, 1u);
    RODAK_CHECK_FALSE(model.MarkReadLocally("n-2"));
}

RODAK_TEST("inbox complete submits once and records the server result") {
    InboxModel model = BoundModelWithSnapshot();
    RODAK_CHECK_FALSE(model.BeginAction("n-2", InboxActionKind::kComplete, 0, "op-a"));
    RODAK_CHECK(model.BeginAction("n-1", InboxActionKind::kComplete, 0, "op-a"));
    RODAK_CHECK(model.Find("n-1")->op_state == InboxOpState::kSubmitting);
    // No second request while the first is in flight.
    RODAK_CHECK_FALSE(model.BeginAction("n-1", InboxActionKind::kComplete, 0, "op-b"));

    const std::string body = BuildInboxActionBody(InboxActionKind::kComplete, *model.Find("n-1"),
                                                  "op-a", 0);
    auto sent = Parse(body.c_str());
    RODAK_CHECK_EQ(std::string(cJSON_GetObjectItem(sent.get(), "action")->valuestring), std::string("complete"));
    RODAK_CHECK_EQ(std::string(cJSON_GetObjectItem(sent.get(), "taskId")->valuestring), std::string("t-1"));
    RODAK_CHECK_EQ(cJSON_GetObjectItem(sent.get(), "expectedRevision")->valueint, 3);

    auto answer = Parse(R"({"outcome":"applied","inboxRevision":13,"item":{"noticeId":"n-1","kind":"todo","title":"给阳台的花浇水","unread":false,"revision":4,"todo":{"taskId":"t-1","occurrenceId":"o-1","state":"done"}}})");
    InboxActionResult result;
    std::string error;
    RODAK_CHECK(ParseInboxActionResult(answer.get(), result, error));
    model.FinishAction("n-1", result);
    RODAK_CHECK(model.Find("n-1")->todo_state == InboxTodoState::kDone);
    RODAK_CHECK(model.Find("n-1")->op_state == InboxOpState::kNone);
    RODAK_CHECK_EQ(model.Summary().open_todos, 0u);
    RODAK_CHECK_FALSE(model.BeginAction("n-1", InboxActionKind::kSnooze, 10, "op-c"));
}

RODAK_TEST("inbox lost answer keeps the opId and a retry must reuse it") {
    InboxModel model = BoundModelWithSnapshot();
    RODAK_CHECK(model.BeginAction("n-1", InboxActionKind::kSnooze, 30, "op-a"));
    model.FailAction("n-1");
    const InboxItem* item = model.Find("n-1");
    RODAK_CHECK(item->op_state == InboxOpState::kPendingReconcile);
    RODAK_CHECK_EQ(item->op_id, std::string("op-a"));
    RODAK_CHECK_FALSE(model.BeginAction("n-1", InboxActionKind::kSnooze, 30, "op-new"));
    RODAK_CHECK_FALSE(model.BeginAction("n-1", InboxActionKind::kComplete, 0, "op-a"));
    // A refresh does not drop the pending state.
    InboxSnapshot snapshot;
    std::string error;
    auto root = Parse(kSnapshot);
    RODAK_CHECK(ParseInboxSnapshot(root.get(), snapshot, error));
    model.ApplySnapshot(std::move(snapshot), 1795000200);
    RODAK_CHECK(model.Find("n-1")->op_state == InboxOpState::kPendingReconcile);
    RODAK_CHECK(model.BeginAction("n-1", InboxActionKind::kSnooze, 30, "op-a"));
    RODAK_CHECK(model.Find("n-1")->op_state == InboxOpState::kSubmitting);
    RODAK_CHECK_FALSE(model.BeginAction("n-1", InboxActionKind::kSnooze, 15, "op-z"));
}

RODAK_TEST("inbox conflict answer converges on the server state") {
    InboxModel model = BoundModelWithSnapshot();
    RODAK_CHECK(model.BeginAction("n-1", InboxActionKind::kComplete, 0, "op-a"));
    auto answer = Parse(R"({"outcome":"conflict","inboxRevision":14,"item":{"noticeId":"n-1","kind":"todo","title":"给阳台的花浇水","unread":false,"revision":5,"todo":{"taskId":"t-1","occurrenceId":"o-2","state":"snoozed","snoozedUntil":1795007200}}})");
    InboxActionResult result;
    std::string error;
    RODAK_CHECK(ParseInboxActionResult(answer.get(), result, error));
    RODAK_CHECK(result.outcome == InboxActionOutcome::kConflict);
    model.FinishAction("n-1", result);
    RODAK_CHECK(model.Find("n-1")->todo_state == InboxTodoState::kSnoozed);
    RODAK_CHECK_EQ(model.Find("n-1")->occurrence_id, std::string("o-2"));
    RODAK_CHECK_EQ(model.inbox_revision(), 14u);
    auto bad = Parse(R"({"outcome":"maybe","inboxRevision":1})");
    RODAK_CHECK_FALSE(ParseInboxActionResult(bad.get(), result, error));
}

namespace {
CJsonPtr Fixture(const std::string& relative) {
    std::ifstream input(std::string(RODAK_AIOT_CONTRACT_DIR) + "/examples/" + relative, std::ios::binary);
    std::ostringstream content;
    content << input.rdbuf();
    return Parse(content.str().c_str());
}
}  // namespace

RODAK_TEST("inbox contract fixtures pass through the firmware parsers") {
    InboxSnapshot snapshot;
    InboxActionResult result;
    std::string error;
    auto root = Fixture("valid/inbox-snapshot.two-items.json");
    RODAK_CHECK(ParseInboxSnapshot(root.get(), snapshot, error));
    RODAK_CHECK_EQ(snapshot.items.size(), size_t{2});
    root = Fixture("valid/inbox-snapshot.empty.json");
    RODAK_CHECK(ParseInboxSnapshot(root.get(), snapshot, error));
    root = Fixture("invalid/inbox-snapshot.version-two.json");
    RODAK_CHECK_FALSE(ParseInboxSnapshot(root.get(), snapshot, error));
    for (const char* fixture : {"valid/inbox-action-result.applied.json",
                                "valid/inbox-action-result.conflict.json"}) {
        root = Fixture(fixture);
        RODAK_CHECK(ParseInboxActionResult(root.get(), result, error));
    }
    root = Fixture("invalid/inbox-action-result.unknown-outcome.json");
    RODAK_CHECK_FALSE(ParseInboxActionResult(root.get(), result, error));

    // The device's own action bodies equal the fixtures; the fixtures use device-style opIds.
    InboxModel model;
    model.SetBound(true);
    root = Fixture("valid/inbox-snapshot.two-items.json");
    RODAK_CHECK(ParseInboxSnapshot(root.get(), snapshot, error));
    model.ApplySnapshot(std::move(snapshot), 0);
    RODAK_CHECK(model.Find("n-7c1e") != nullptr);
    struct Case {
        InboxActionKind kind;
        const char* notice;
        const char* op;
        uint32_t minutes;
        const char* fixture;
    };
    for (const Case& item : {Case{InboxActionKind::kComplete, "n-7c1e", "op-5a1b2c3d4e5f6071-1", 0,
                                  "valid/inbox-action-request.complete.json"},
                             Case{InboxActionKind::kSnooze, "n-7c1e", "op-5a1b2c3d4e5f6071-2", 30,
                                  "valid/inbox-action-request.snooze.json"},
                             Case{InboxActionKind::kMarkRead, "n-7c1f", "op-5a1b2c3d4e5f6071-3", 0,
                                  "valid/inbox-action-request.read.json"}}) {
        CJsonPtr built = Parse(BuildInboxActionBody(item.kind, *model.Find(item.notice), item.op,
                                                    item.minutes).c_str());
        CJsonPtr expected = Fixture(item.fixture);
        RODAK_CHECK(built != nullptr && expected != nullptr);
        RODAK_CHECK(cJSON_Compare(built.get(), expected.get(), true));
    }
}
