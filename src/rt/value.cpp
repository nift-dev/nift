#include "rt/value.h"

#include "rt/heap.h"

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

// Destruction is recursive through child Values; aggregates are released
// child by child, so very deep nesting relies on the C++ stack (legacy
// RuntimeValue has the same property; JSON and literal depth are bounded
// upstream).
void destroy_heap_object(HeapObject* object) {
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

} // namespace nift::rt
