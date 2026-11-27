#pragma once

#include <cstdint>

namespace rodakos {

// What Home and the Lock Screen show about the message center. Counts only, never message text:
// the Lock Screen is a privacy cover, not an authenticated view.
struct InboxSummary {
    // false after unbind or before the first binding; the counts are then zero.
    bool bound = false;
    // A snapshot from the server has been received since boot or since the last binding.
    bool synced = false;
    uint32_t unread = 0;
    uint32_t open_todos = 0;
    // Increases whenever any field above changes, so a poller can skip unchanged summaries.
    uint32_t revision = 0;
};

// Lets Home and the Shell read the summary without depending on the inbox service.
class InboxSummaryProvider {
public:
    virtual ~InboxSummaryProvider() = default;
    virtual InboxSummary GetInboxSummary() = 0;
};

}  // namespace rodakos
