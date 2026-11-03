#include "rt/bad_bypass_forms.h"
#include "rt//sort_controller.h"
#define CAT(a, b) a##b
// The reviewer's rr/lint/vm_bypass.cpp, sort_bypass.cpp and push_value patch:
// storage-write fires on lines 2, 3, 9, 12-15, 17, 18, 20, 22-24 (the pasted
// name on line 21 is caught at its #define, line 3; line 19 uses the storage
// line 18 already flagged).
namespace nift::rt {
using nift::rt::array_push;
void q1(Value& published, const Value& x) {
    // (the using-declaration above, line 9, is a reference too)
    rt::array_push(published, x);
    nift::rt::object_set(published, Atom{}, x);
    auto* f = &array_build_set; f(published, 0, x);
    (array_push)(published, x);
    // rt-lint: storage-ok fresh, a blanket annotation that covers the whole line
    array_build_storage(published).push_back(x); array_push(published, x);
    std::vector<Value>& raw = rt::array_build_storage(published);
    raw.clear();
    rt::struct_add_field(published, Atom{}, x);
    mutation::CAT(Sort, Controller)::items(published).clear();
    if (published.is(Kind::Array)) { rt::array_push(published, x); }
    mutation::SortController::items(published).clear();
    stack_build_push(published, x);
    Value fresh = make_array(1);
    array_push(fresh, x);  // rt-lint: storage-ok fresh, the value this function just made
}
}
