#include "rt/value.h"

#include "rt/heap.h"
#include "rt/heap_account.h"

#include <vector>

namespace nift::rt {

const char* kind_name(Kind kind) {
    switch (kind) {
        case Kind::Null: return "null";
        case Kind::Bool: return "bool";
        case Kind::Int: return "int";
        case Kind::Double: return "double";
        case Kind::Decimal: return "decimal";
        case Kind::String: return "string";
        case Kind::Bytes: return "bytes";
        case Kind::Array: return "array";
        case Kind::Object: return "object";
        case Kind::Function: return "function";
        case Kind::Struct: return "struct";
        case Kind::Handle: return "handle";
        case Kind::Error: return "error";
        case Kind::Cell: return "cell";  // internal only
    }
    return "unknown";
}

namespace {

void delete_object(HeapObject* object) noexcept {
#if defined(NIFT_CLOSURE_MUTANT) && NIFT_CLOSURE_MUTANT == 7
    if (object->kind() == Kind::Cell || object->kind() == Kind::Function) return;  // M7: leak
#endif
    heap_untally(object);
    switch (object->kind()) {
        case Kind::String: delete static_cast<StringObject*>(object); return;
        case Kind::Decimal: delete static_cast<DecimalObject*>(object); return;
        case Kind::Bytes: delete static_cast<BytesObject*>(object); return;
        case Kind::Array: delete static_cast<ArrayObject*>(object); return;
        case Kind::Object: delete static_cast<ObjectObject*>(object); return;
        case Kind::Function: delete static_cast<FunctionObject*>(object); return;
        case Kind::Struct: delete static_cast<StructObject*>(object); return;
        case Kind::Error: delete static_cast<ErrorObject*>(object); return;
        case Kind::Cell: delete static_cast<CellObject*>(object); return;
        default: return;  // immediate kinds never reach the heap
    }
}

bool has_children(Kind kind) noexcept {
    return kind == Kind::Array || kind == Kind::Object || kind == Kind::Function || kind == Kind::Struct ||
           kind == Kind::Error || kind == Kind::Cell;
}

std::size_t child_count(const HeapObject* o) noexcept {
    switch (o->kind()) {
        case Kind::Array: return static_cast<const ArrayObject*>(o)->items.size();
        case Kind::Object: return static_cast<const ObjectObject*>(o)->entries.size();
        case Kind::Function: return static_cast<const FunctionObject*>(o)->upvalues.size();
        case Kind::Struct: return static_cast<const StructObject*>(o)->fields.size();
        case Kind::Error: return 1;
        case Kind::Cell: return 1;
        default: return 0;
    }
}

Value& child_at(HeapObject* o, std::size_t i) noexcept {
    switch (o->kind()) {
        case Kind::Array: return static_cast<ArrayObject*>(o)->items[i];
        case Kind::Object: return static_cast<ObjectObject*>(o)->entries[i].second;
        case Kind::Function: return static_cast<FunctionObject*>(o)->upvalues[i];
        case Kind::Struct: return static_cast<StructObject*>(o)->fields[i].second;
        case Kind::Cell: return static_cast<CellObject*>(o)->value;
        default: return static_cast<ErrorObject*>(o)->cause;
    }
}

// A dying aggregate's header is free once it is untallied and its refcount is
// 0: refcount and the three `counted` bytes hold the child index to resume at
// (56 bits). The tag byte (kind) is kept.
void set_resume(HeapObject* o, std::size_t i) noexcept {
    o->refcount = static_cast<std::uint32_t>(i);
    o->counted[0] = static_cast<std::uint8_t>(i >> 32);
    o->counted[1] = static_cast<std::uint8_t>(i >> 40);
    o->counted[2] = static_cast<std::uint8_t>(i >> 48);
}

std::size_t get_resume(const HeapObject* o) noexcept {
    return std::size_t(o->refcount) | (std::size_t(o->counted[0]) << 32) | (std::size_t(o->counted[1]) << 40) |
           (std::size_t(o->counted[2]) << 48);
}

void delete_untallied(HeapObject* o) noexcept {
    o->counted[0] = o->counted[1] = o->counted[2] = 0;  // untallied before its header was reused
    delete_object(o);
}

// Fallback without allocation or recursion (pointer reversal): descending into
// a dying aggregate child stores the grandparent in the vacated child slot and
// the resume index in the parent's dead header. O(objects) time, O(1) space.
void destroy_in_place(HeapObject* object) noexcept {
    heap_untally(object);
    HeapObject* cur = object;
    HeapObject* up = nullptr;
    std::size_t i = 0;
    for (;;) {
        const std::size_t n = child_count(cur);
        HeapObject* down = nullptr;
        for (; i < n; ++i) {
            Value& slot = child_at(cur, i);
            HeapObject* last = slot.release_detached();
            if (!last) continue;
            if (!has_children(last->kind())) { delete_object(last); continue; }
            heap_untally(last);
            slot.park_pointer(up);
            set_resume(cur, i);
            down = last;
            break;
        }
        if (down) { up = cur; cur = down; i = 0; continue; }
        if (!up) { delete_untallied(cur); return; }
        HeapObject* parent = up;
        const std::size_t at = get_resume(parent);
        up = child_at(parent, at).take_parked();
        delete_untallied(cur);
        cur = parent;
        i = at + 1;
    }
}

// Worklist: dying aggregates go to a fixed in-frame buffer first, then to a
// heap spill vector; if the spill cannot grow (out of memory), that subtree is
// destroyed in place by pointer reversal. Nothing here can throw, recurse or
// need memory (memory investigation C1).
// In-frame buffer size. Tests build with a tiny buffer
// (-DNIFT_RT_DESTROY_LOCAL=1) so that small graphs exercise the spill and the
// pointer-reversal fallback at every allocation index.
#ifndef NIFT_RT_DESTROY_LOCAL
#define NIFT_RT_DESTROY_LOCAL 32
#endif
static_assert(NIFT_RT_DESTROY_LOCAL >= 1, "the destroy worklist needs an in-frame buffer");

struct Worklist {
    static constexpr std::size_t kLocal = NIFT_RT_DESTROY_LOCAL;
    HeapObject* local[kLocal];
    std::size_t nlocal = 0;
    std::vector<HeapObject*> spill;

#if defined(__GNUC__)
    __attribute__((noinline))
#endif
    void take(Value& child) noexcept {
        HeapObject* last = child.release_detached();
        if (!last) return;
        if (!has_children(last->kind())) { delete_object(last); return; }
        if (nlocal < kLocal) { local[nlocal++] = last; return; }
        try {
            spill.push_back(last);
        } catch (...) {
            destroy_in_place(last);
        }
    }

    // Detaches the dying children of `o` (immediates are skipped inline: their
    // destructors are no-ops), then deletes `o`.
    void detach(HeapObject* o) noexcept {
        switch (o->kind()) {
            case Kind::Array:
                for (Value& v : static_cast<ArrayObject*>(o)->items) if (v.is_heap()) take(v);
                break;
            case Kind::Object:
                for (auto& e : static_cast<ObjectObject*>(o)->entries) if (e.second.is_heap()) take(e.second);
                break;
            case Kind::Function:
                for (Value& v : static_cast<FunctionObject*>(o)->upvalues) if (v.is_heap()) take(v);
                break;
            case Kind::Struct:
                for (auto& f : static_cast<StructObject*>(o)->fields) if (f.second.is_heap()) take(f.second);
                break;
            case Kind::Error: take(static_cast<ErrorObject*>(o)->cause); break;
            case Kind::Cell: take(static_cast<CellObject*>(o)->value); break;
            default: break;
        }
        delete_object(o);
    }

    HeapObject* next() noexcept {
        if (!spill.empty()) { HeapObject* n = spill.back(); spill.pop_back(); return n; }
        return nlocal ? local[--nlocal] : nullptr;
    }
};

} // namespace

// Destruction is iterative and needs no memory: children whose last reference
// dies are detached onto the worklist above before their parent is deleted, so
// nesting depth never reaches the C++ stack (a COW `a = [a]` loop builds depth
// in O(1) per step) and an exhausted heap cannot turn a release into an abort.
void destroy_heap_object(HeapObject* object) noexcept {
    if (!has_children(object->kind())) { delete_object(object); return; }
    Worklist w;
    w.detach(object);
    while (HeapObject* next = w.next()) w.detach(next);
}

} // namespace nift::rt
