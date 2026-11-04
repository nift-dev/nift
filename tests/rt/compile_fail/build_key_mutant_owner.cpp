// Tooling review F3 (x/k6_objmerge.cpp): natives::obj_merge owns the key only in
// its R-B5 mutant build; in a production build it cannot construct one. Must NOT compile.
#include "rt/natives/support.h"
namespace nift::rt::natives {
Status obj_merge(const Context&, const Value* args, std::uint32_t, Value& out, std::string&) {
    Value receiver = args[0];
    const build::InPlaceKey key;
    Construction::object_set(key, receiver, atom("x"), Value::integer(1));
    out = receiver; return Status::Ok;
}
}
