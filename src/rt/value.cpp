#include "rt/value.h"

#include "rt/heap.h"

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
    }
    return "unknown";
}

namespace {

// Detaches every child Value of `object` whose last reference this was and
// appends those children to `pending`; the remaining children become Null.
void detach_children(HeapObject* object, std::vector<HeapObject*>& pending) {
    auto take = [&pending](Value& child) {
        if (HeapObject* last = child.release_detached()) pending.push_back(last);
    };
    switch (object->kind) {
        case Kind::Array: for (Value& v : static_cast<ArrayObject*>(object)->items) take(v); return;
        case Kind::Object: for (auto& e : static_cast<ObjectObject*>(object)->entries) take(e.second); return;
        case Kind::Function: for (Value& v : static_cast<FunctionObject*>(object)->upvalues) take(v); return;
        case Kind::Struct: for (auto& f : static_cast<StructObject*>(object)->fields) take(f.second); return;
        case Kind::Error: take(static_cast<ErrorObject*>(object)->cause); return;
        default: return;
    }
}

void delete_object(HeapObject* object) {
    switch (object->kind) {
        case Kind::String: delete static_cast<StringObject*>(object); return;
        case Kind::Decimal: delete static_cast<DecimalObject*>(object); return;
        case Kind::Bytes: delete static_cast<BytesObject*>(object); return;
        case Kind::Array: delete static_cast<ArrayObject*>(object); return;
        case Kind::Object: delete static_cast<ObjectObject*>(object); return;
        case Kind::Function: delete static_cast<FunctionObject*>(object); return;
        case Kind::Struct: delete static_cast<StructObject*>(object); return;
        case Kind::Error: delete static_cast<ErrorObject*>(object); return;
        default: return;  // immediate kinds never reach the heap
    }
}

bool has_children(Kind kind) {
    return kind == Kind::Array || kind == Kind::Object || kind == Kind::Function || kind == Kind::Struct ||
           kind == Kind::Error;
}

} // namespace

// Destruction is iterative: children whose last reference dies are detached
// onto a worklist before their parent is deleted, so the parent's destructor
// only sees Null children and nesting depth never reaches the C++ stack
// (a COW `a = [a]` loop builds depth in O(1) per step). Leaf kinds and
// aggregates without dying children allocate no worklist.
void destroy_heap_object(HeapObject* object) {
    if (!has_children(object->kind)) { delete_object(object); return; }
    std::vector<HeapObject*> pending;
    detach_children(object, pending);
    delete_object(object);
    while (!pending.empty()) {
        HeapObject* next = pending.back();
        pending.pop_back();
        detach_children(next, pending);
        delete_object(next);
    }
}

} // namespace nift::rt
