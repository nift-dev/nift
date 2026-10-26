#include "rt/handles/handle_box.h"

#include "rt/heap_account.h"

namespace nift::rt::handles {

namespace {

void release_box(void* table, HandleKind kind, std::uint64_t id, std::uint32_t generation) noexcept {
    // Runs from a Value destructor (noexcept): an allocation failure in the
    // cleanup's bookkeeping must not escape (std::terminate). The table has
    // already released the cleanup (abandon_cleanup_locked); only that
    // cleanup's report is lost (CP17 x CP18).
    try {
        static_cast<HandleTable*>(table)->drop(Value::handle(kind, id, generation));
    } catch (...) {
        static_cast<HandleTable*>(table)->note_report_lost();
    }
}

} // namespace

Value box_counted_handle(HandleTable& table, const Value& handle) {
    if (!handle.is(Kind::Handle) || handle.boxed_handle()) return handle;
    auto* box = new HandleBoxObject();
    box->id = handle.handle_id();
    box->generation = handle.handle_generation();
    box->handle_kind = handle.handle_kind();
    box->table = &table;
    box->release = release_box;
    heap_retally(box);
    return Value::adopt_handle_box(box);
}

Value share_into_isolate(HandleTable& table, const Value& boxed) {
    const Value plain = boxed.handle_identity();
    if (table.retain(plain) != HandleStatus::Ok) return plain;  // closed: the identity still reports handle_closed
    return box_counted_handle(table, plain);
}

} // namespace nift::rt::handles
