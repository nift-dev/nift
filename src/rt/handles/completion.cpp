#include "rt/handles/completion.h"

#include "rt/handles/table.h"

namespace nift::rt::handles {

CompletionTicket& CompletionTicket::operator=(CompletionTicket&& other) noexcept {
    if (this != &other) {
        CompletionTicket old(std::move(*this));  // an unreported ticket being overwritten settles as unresolved
        table_ = other.table_;
        record_ = other.record_;
        other.table_ = nullptr;
    }
    return *this;
}

CompletionTicket::~CompletionTicket() {
    if (table_) {
        complete(CleanupOutcome::lost("internal.handle_cleanup_unresolved",
                                         "cleanup outcome was never reported"));
    }
}

bool CompletionTicket::complete(CleanupOutcome outcome) {
    HandleTable* table = table_;
    if (!table) return false;
    table_ = nullptr;
    if (outcome.result == CleanupOutcome::Result::Pending) {
        outcome = CleanupOutcome::lost("internal.handle_cleanup_unresolved", "cleanup reported no final outcome");
    }
    return table->settle(record_, std::move(outcome));
}

} // namespace nift::rt::handles
