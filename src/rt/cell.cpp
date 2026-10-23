#include "rt/cell.h"

#include "rt/aggregate.h"
#include "rt/heap_account.h"

namespace nift::rt {

Value make_cell(Value value, std::int64_t binding_type) {
    auto* cell = new CellObject();
    cell->value = std::move(value);
    cell->binding_type = binding_type;
    heap_retally(cell);
    return Value::adopt(cell);
}

CellObject* cell_of(const Value& cell) { return cell.is(Kind::Cell) ? cell.heap_as<CellObject>() : nullptr; }

Value cell_load(const Value& cell) {
    const CellObject* object = cell_of(cell);
    return object ? object->value : Value();
}

void cell_put(Value& cell, Value value) {
    CellObject* object = cell_of(cell);
    if (!object) return;
    object->value = std::move(value);
    if (may_contain_identity(object->value)) mark_may_hold_identity(object);
}

} // namespace nift::rt
