#include "rt/cell.h"

#include "rt/aggregate.h"
#include "rt/heap_account.h"
#include "rt/string.h"

#include <vector>

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
    return current->binding_type == kSelectorCell ? array_items(current->value)[2].as_int() : current->binding_type;
}

Value cell_slot(const Value& cell) {
    const CellObject* object = cell_of(cell);
    return object && object->binding_type == kSelectorCell ? array_items(object->value)[7] : cell;
}

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
    const CellObject* current = cell_of(cell);
    Value anchor = cell;
    std::vector<const CellObject*> path;
    while (current && (current->binding_type == kSelectorCell || current->binding_type == kSourceSlot)) {
        if (current->binding_type == kSelectorCell) {
            path.push_back(current);
            anchor = array_items(current->value)[0];
        } else anchor = current->value;
        current = cell_of(anchor);
    }
    if (!current) return false;
    Value selected = current->value;
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        const auto& descriptor = array_items((*it)->value);
        const Value& key = descriptor[1];
        const Value* found = nullptr;
        if (selected.is(Kind::Array) && key.is(Kind::Int) && key.as_int() >= 0 &&
            static_cast<std::uint64_t>(key.as_int()) < array_size(selected))
            found = &array_items(selected)[static_cast<std::size_t>(key.as_int())];
        else if (selected.is(Kind::Object) && key.is(Kind::String)) {
            Atom atom;
            if (AtomTable::current().find(string_view_of(key), atom)) found = object_get(selected, atom);
        }
        if (!found) { missing = std::string(string_view_of(descriptor[3])); return false; }
        Value next = *found;  // copy before releasing the parent that owns it
        selected = std::move(next);
    }
    value = std::move(selected);
    if (CellObject* owner = cell_of(cell); owner && owner->binding_type == kSelectorCell) {
        array_set(owner->value, 5, Value::boolean(true));
        array_set(owner->value, 6, anchor);
    }
    return true;
}

Value copy_cell_location(const Value& cell) {
    const CellObject* object = cell_of(cell);
    return object && object->binding_type == kSelectorCell ? make_cell(object->value, kSelectorCell) : cell;
}

bool cell_cached_read(const Value& cell, Value& value, std::string& missing) {
    const CellObject* object = cell_of(cell);
    if (!object || object->binding_type != kSelectorCell) return cell_read(cell, value, missing);
    const auto& parts = array_items(object->value);
    if (!parts[5].as_bool()) return cell_read(parts[4], value, missing);
    // The cached pointer belongs to the value owner at the last sync, not to
    // a later replacement of its source slot. Preserve that owner explicitly.
    Value descriptor = object->value;
    array_set(descriptor, 0, parts[6]);
    Value cached = make_cell(std::move(descriptor), kSelectorCell);
    return cell_read(cached, value, missing);
}

namespace {
bool replace_path(Value& parent, const std::vector<Value>& keys, std::size_t at, const Value& value) {
    const Value& key = keys[at];
    const Value* previous = nullptr;
    Atom atom;
    if (parent.is(Kind::Array) && key.is(Kind::Int) && key.as_int() >= 0 &&
        static_cast<std::uint64_t>(key.as_int()) < array_size(parent))
        previous = &array_items(parent)[static_cast<std::size_t>(key.as_int())];
    else if (parent.is(Kind::Object) && key.is(Kind::String) &&
             AtomTable::current().find(string_view_of(key), atom)) previous = object_get(parent, atom);
    if (!previous) return false;
    Value replacement = value;
    if (at + 1 < keys.size()) {
        replacement = *previous;
        if (!replace_path(replacement, keys, at + 1, value)) return false;
    }
    if (parent.is(Kind::Array)) array_set(parent, static_cast<std::size_t>(key.as_int()), replacement);
    else object_set(parent, atom, replacement);
    return true;
}
} // namespace

bool cell_cached_put(Value& cell, Value value, std::string& missing) {
    CellObject* object = cell_of(cell);
    if (!object) return false;
    if (object->binding_type != kSelectorCell) { cell_put(cell, std::move(value)); return true; }
    const auto& parts = array_items(object->value);
    if (!parts[5].as_bool()) {
        Value cache = parts[4];
        cell_put(cache, std::move(value));
        return true;
    }
    std::vector<Value> keys;
    CellObject* current = object;
    while (current->binding_type == kSelectorCell) {
        const auto& descriptor = array_items(current->value);
        keys.insert(keys.begin(), descriptor[1]);
        missing = std::string(string_view_of(descriptor[3]));
        current = cell_of(descriptor[0]);
        if (!current) return false;
    }
    CellObject* owner = cell_of(parts[6]);
    if (!owner) return false;
    Value source = std::move(owner->value);
    const bool ok = replace_path(source, keys, 0, value);
    owner->value = std::move(source);
    if (may_contain_identity(owner->value)) mark_may_hold_identity(owner);
    return ok;
}

Value cell_cached_owner(const Value& cell) {
    const CellObject* object = cell_of(cell);
    if (!object || object->binding_type != kSelectorCell) return cell_anchor(cell);
    const auto& parts = array_items(object->value);
    return parts[parts[5].as_bool() ? 6 : 4];
}

Value cell_take(const Value& cell) {
    Value anchor = cell_anchor(cell);
    CellObject* object = cell_of(anchor);
    return object ? std::move(object->value) : Value();
}

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
    if (object->binding_type == kSelectorCell) {
        Value slot = array_items(object->value)[7];
        cell_rebind(slot, std::move(value));
        object->value = std::move(slot);
        object->binding_type = kSourceSlot;
        return;
    }
    if (object->binding_type == kSourceSlot) {
        CellObject* child = cell_of(object->value);
        if (child->binding_type == kSourceSlot) cell_rebind(object->value, std::move(value));
        else object->value = make_cell(std::move(value), child->binding_type);
        return;
    }
    cell_put(cell, std::move(value));
}

} // namespace nift::rt
