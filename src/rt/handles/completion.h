#pragma once
// Completion API of the CP17 lifecycle (D-035): the one-shot ticket through
// which CP16 reports the OS outcome of a resource's termination. The table
// alone decides and records the lifecycle state; a ticket only reports.
#include "rt/handles/types.h"

#include <cstdint>

namespace nift::rt::handles {

class HandleTable;

// Issued exactly once per resource, when its lifecycle leaves Active (to the
// cleanup hook in CleanupEvent::ticket, or by HandleTable::begin_termination).
// Move-only. complete() settles the lifecycle exactly once; a ticket destroyed
// without completing settles it as Lost (`internal.handle_cleanup_unresolved`)
// so no context waits forever on a dropped report. The ticket must not
// outlive its table (shutdown() waits for every ticket of a closed handle).
class CompletionTicket {
public:
    CompletionTicket() = default;
    CompletionTicket(const CompletionTicket&) = delete;
    CompletionTicket& operator=(const CompletionTicket&) = delete;
    CompletionTicket(CompletionTicket&& other) noexcept : table_(other.table_), record_(other.record_) {
        other.table_ = nullptr;
    }
    CompletionTicket& operator=(CompletionTicket&& other) noexcept;
    ~CompletionTicket();

    // Reports the final outcome (OS facts): completed(), alive_after_kill(),
    // lost() or cleanup_incomplete(). pending() is not final and settles as
    // Lost (`internal.handle_cleanup_unresolved`). Returns true when this call
    // settled the lifecycle; false for an empty or already completed ticket.
    bool complete(CleanupOutcome outcome);
    explicit operator bool() const { return table_ != nullptr; }

private:
    friend class HandleTable;
    CompletionTicket(HandleTable* table, std::uint64_t record) : table_(table), record_(record) {}
    HandleTable* table_ = nullptr;
    std::uint64_t record_ = 0;
};

} // namespace nift::rt::handles
