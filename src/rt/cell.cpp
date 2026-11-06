#include "rt/cell.h"
#include "rt/mutation.h"

#include "rt/aggregate.h"
#include "rt/heap_account.h"
#include "rt/string.h"


namespace nift::rt {

Value make_cell(Value value, std::int64_t binding_type) {
    auto* cell = new CellObject();
    cell->value = std::move(value);
    cell->binding_type = binding_type;
    heap_retally(cell);
    return Value::adopt(cell);
}

CellObject* cell_of(const Value& cell) { return cell.is(Kind::Cell) ? cell.heap_as<CellObject>() : nullptr; }

const CellObject* cell_plain_capture(const Value& cell) {
    const CellObject* current = cell_of(cell);
    for (unsigned depth = 0; current && depth < 64; ++depth) {
        if (current->binding_type >= 0) return current;
#if defined(NIFT_SESSION_ADMISSION_MUTANT) && NIFT_SESSION_ADMISSION_MUTANT == 3
        return nullptr;
#endif
        if (current->binding_type != kSourceSlot) return nullptr;
        current = cell_of(current->value);
    }
    return nullptr;
}

std::int64_t cell_binding_type(const CellObject& cell) {
    const CellObject* current = &cell;
    while (current->binding_type == kSourceSlot) current = cell_of(current->value);
    return current->binding_type;
}

// B5 4 (B2 review L2 resolved by deletion): the selector cells (a loop
// variable's lazy location descriptor, kind -2) and their cached path store
// `replace_path` (a residual take, a default mutation::Env, a deeper level
// committed before its parent was validated) are gone with the live loop path:
// every loop iterates its entry snapshot (D5), so no compiled code creates a
// location descriptor. The cell opcodes keep their names; a "cached" access
// is the plain cell access and a captured loop cell is shared, not copied.
Value cell_slot(const Value& cell) { return cell; }

Value cell_anchor(const Value& cell) {
    Value anchor = cell;
    const CellObject* current = cell_of(anchor);
    while (current && current->binding_type == kSourceSlot) {
        anchor = current->value;
        current = cell_of(anchor);
    }
    return anchor;
}

bool cell_read(const Value& cell, Value& value, std::string& missing) {
    (void)missing;
    const CellObject* current = cell_of(cell_anchor(cell));
    if (!current) return false;
    value = current->value;
    return true;
}

Value copy_cell_location(const Value& cell) { return cell; }

bool cell_cached_read(const Value& cell, Value& value, std::string& missing) {
    return cell_read(cell, value, missing);
}

bool cell_cached_put(Value& cell, Value value, std::string& missing) {
    (void)missing;
    if (!cell_of(cell)) return false;
    cell_put(cell, std::move(value));
    return true;
}

Value cell_cached_owner(const Value& cell) { return cell_anchor(cell); }

Value cell_load(const Value& cell) {
    Value result;
    std::string missing;
    cell_read(cell, result, missing);
    return result;
}

void cell_put(Value& cell, Value value) {
    Value anchor = cell_anchor(cell);
    CellObject* object = cell_of(anchor);
    if (!object) return;
    object->value = std::move(value);
    if (may_contain_identity(object->value)) mark_may_hold_identity(object);
}

void cell_rebind(Value& cell, Value value) {
    CellObject* object = cell_of(cell);
    if (!object) return;
    if (object->binding_type == kSourceSlot) {
        CellObject* child = cell_of(object->value);
        if (child->binding_type == kSourceSlot) cell_rebind(object->value, std::move(value));
        else object->value = make_cell(std::move(value), child->binding_type);
        return;
    }
    cell_put(cell, std::move(value));
}

} // namespace nift::rt
