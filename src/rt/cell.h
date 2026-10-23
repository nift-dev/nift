#pragma once
// Internal captured-variable cells (CP12C): creation and checked value access.
// A cell is not a script value; it exists only in compiler binding registers,
// FunctionObject upvalues and the cycle graph.
#include "rt/heap.h"
#include "rt/vm_ops.h"

#include <cstdint>

namespace nift::rt {

Value make_cell(Value value, std::int64_t binding_type);
CellObject* cell_of(const Value& cell);
// A plain Value copy (D-040 COW semantics for the value).
Value cell_load(const Value& cell);
// Replaces the cell's value after the caller has performed type/cycle checks.
void cell_put(Value& cell, Value value);
// True when `value` is a cell (defensive boundary checks).
inline bool is_cell(const Value& value) noexcept { return value.is(Kind::Cell); }

} // namespace nift::rt
