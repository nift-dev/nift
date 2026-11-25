#include "rt/cell.h"
#include "rt/mutation.h"

#include "rt/aggregate.h"
#include "rt/heap_account.h"
#include "rt/string.h"

#include <limits>


namespace nift::rt {

namespace {
constexpr std::int64_t kFirstIndexedCell = -4;
// Keep the full Atom id range disjoint from array index encodings.
constexpr auto kFirstKeyedCell = std::numeric_limits<std::int64_t>::min();
constexpr auto kLastKeyedCell = kFirstKeyedCell + std::numeric_limits<std::uint32_t>::max();

bool keyed_cell(const CellObject& cell) { return cell.binding_type <= kLastKeyedCell; }

const Value* keyed_member(const CellObject& root, Atom key) {
    return root.value.is(Kind::Object) ? object_find(root.value, key) : nullptr;
}

bool indexed_cell(const CellObject* cell) {
    return cell && cell->binding_type <= kFirstIndexedCell;
}

const Value* indexed_element(const CellObject& cell, std::uint64_t index) {
    const Value& root = cell.value;
    if (!root.is(Kind::Array)) return nullptr;
    const auto& items = array_view(root);
    if (index >= items.size()) return nullptr;
    return &items[static_cast<std::size_t>(index)];
}
} // namespace

Value make_cell(Value value, std::int64_t binding_type) {
    auto* cell = new CellObject();
    cell->value = std::move(value);
    cell->binding_type = binding_type;
    heap_retally(cell);
    return Value::adopt(cell);
}

CellObject* cell_of(const Value& cell) { return cell.is(Kind::Cell) ? cell.heap_as<CellObject>() : nullptr; }

Value make_indexed_cell(Value root, std::size_t index) {
    const CellObject* owner = cell_of(cell_anchor(root));
    if (!owner || indexed_cell(owner) ||
        index > static_cast<std::uint64_t>(kFirstIndexedCell - (kLastKeyedCell + 1)) ||
        !indexed_element(*owner, index)) return {};
    return make_cell(std::move(root), kFirstIndexedCell - static_cast<std::int64_t>(index));
}

Value make_keyed_cell(Value root, Atom key) {
    const CellObject* owner = cell_of(cell_anchor(root));
    if (!owner || indexed_cell(owner) || !keyed_member(*owner, key)) return {};
    return make_cell(std::move(root), kFirstKeyedCell + key.id);
}

bool is_indexed_cell(const Value& cell) noexcept { return indexed_cell(cell_of(cell)); }

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
    return indexed_cell(current) ? -1 : current->binding_type;
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

bool has_indexed_location(const Value& cell) {
    const auto* owner = cell_of(cell_anchor(cell));
    return indexed_cell(owner) || (owner && is_indexed_cell(owner->value));
}

Value indexed_alias_payload(const Value& cell, Value selected) {
    const auto* owner = cell_of(cell_anchor(cell));
    if (owner && indexed_cell(owner)) return cell_anchor(cell);
    if (owner && is_indexed_cell(owner->value)) return owner->value;
    return selected;
}

bool cell_read(const Value& cell, Value& value, std::string& missing) {
    const CellObject* current = cell_of(cell_anchor(cell));
    if (!current) return false;
    if (!indexed_cell(current) && is_indexed_cell(current->value)) current = cell_of(current->value);
    if (indexed_cell(current)) {
        const CellObject* root = cell_of(cell_anchor(current->value));
        const bool keyed = keyed_cell(*current);
        const Value* item = nullptr;
        if (root && !indexed_cell(root)) {
            if (keyed) {
                item = keyed_member(*root, Atom{static_cast<std::uint32_t>(current->binding_type - kFirstKeyedCell)});
            } else {
                item = indexed_element(*root, static_cast<std::uint64_t>(kFirstIndexedCell - current->binding_type));
            }
        }
        if (!item) {
            missing = keyed ? "object member" : "array element";
            return false;
        }
        value = *item;
        return true;
    }
    value = current->value;
    return true;
}

Value copy_cell_location(const Value& cell) { return cell; }

bool cell_cached_read(const Value& cell, Value& value, std::string& missing) {
    return cell_read(cell, value, missing);
}

bool cell_cached_put(Value& cell, Value value, std::string& missing) {
    const CellObject* owner = cell_of(cell_anchor(cell));
    if (!owner) return false;
    if (has_indexed_location(cell)) {
        missing = "array element";
        return false;
    }
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
    if (!object || indexed_cell(object)) return;
    object->value = std::move(value);
    if (may_contain_identity(object->value)) mark_may_hold_identity(object);
}

void cell_rebind(Value& cell, Value value) {
    CellObject* object = cell_of(cell);
    if (!object) return;
    if (object->binding_type == kSourceSlot) {
        CellObject* child = cell_of(object->value);
        if (child->binding_type == kSourceSlot) cell_rebind(object->value, std::move(value));
        else object->value = make_cell(std::move(value), indexed_cell(child) ? -1 : child->binding_type);
        return;
    }
    if (indexed_cell(object)) {
        cell = make_cell(std::move(value), -1);
        return;
    }
    cell_put(cell, std::move(value));
}

} // namespace nift::rt
