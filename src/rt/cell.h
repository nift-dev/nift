#pragma once
// Internal captured-variable cells (CP12C): creation and checked value access.
// A cell is not a script value; it exists only in compiler binding registers,
// FunctionObject upvalues and the cycle graph.
#include "rt/heap.h"
#include "rt/vm_ops.h"

#include <cstdint>
#include <string>

namespace nift::rt {

Value make_cell(Value value, std::int64_t binding_type);
// Retain a lexical root binding and an evaluated array index. Returns null
// for an invalid root/index. Allocation failure propagates normally.
Value make_indexed_cell(Value root, std::size_t index);
bool is_indexed_cell(const Value& cell) noexcept;
bool has_indexed_location(const Value& cell);
CellObject* cell_of(const Value& cell);
// Follow lexical forwarding only; never resolve a selector or its cache.
const CellObject* cell_plain_capture(const Value& cell);
// A plain Value copy (D-040 COW semantics for the value).
Value cell_load(const Value& cell);
// A source slot (-3) forwards to the cell it names. B5 removed the old
// loop selector (-2). Indexed cells use the remaining negative encodings
// and store the root binding in their ordinary owned Value edge.
constexpr std::int64_t kSourceSlot = -3;
bool cell_read(const Value& cell, Value& value, std::string& missing);
Value copy_cell_location(const Value& cell);
bool cell_cached_read(const Value& cell, Value& value, std::string& missing);
bool cell_cached_put(Value& cell, Value value, std::string& missing);
Value cell_cached_owner(const Value& cell);
Value cell_slot(const Value& cell);
Value cell_anchor(const Value& cell);
void cell_rebind(Value& cell, Value value);
std::int64_t cell_binding_type(const CellObject& cell);
// Replaces the cell's value after the caller has performed type/cycle checks.
void cell_put(Value& cell, Value value);
// True when `value` is a cell (defensive boundary checks).
inline bool is_cell(const Value& value) noexcept { return value.is(Kind::Cell); }

} // namespace nift::rt
