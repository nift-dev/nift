#pragma once
// VM values for counted handles (CP17/D-035): boxes a handle-table reference
// in a refcounted heap value, so the VM's own reference counting decides when
// the last script reference is gone. Requires the HandleBoxObject patch to
// rt/value.h (tests/runtime2/cp17/patches/vm-handle-box.patch).
#include "rt/handles/table.h"

namespace nift::rt::handles {

// Boxes `handle`, adopting one table reference (the creator's reference from
// a counted create(), or one from retain()). Box counted handles only; a
// non-handle or an already boxed value is returned unchanged.
Value box_counted_handle(HandleTable& table, const Value& handle);
// A boxed value for another isolate: retains a new table reference (refcounts
// of heap values are per isolate; boxes never cross isolates).
Value share_into_isolate(HandleTable& table, const Value& boxed);

} // namespace nift::rt::handles
