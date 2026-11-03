// B2 compile-fail test (re-check R1, the reviewer's rc/forge/f22): a
// translation unit that includes only rt/mutation.h defines its own
// SortController (it was a friend of Protocol). The sort commits require a
// SortKey and befriend nothing, so the definition gains nothing. Must NOT compile.
#include "rt/mutation.h"
#include <vector>
namespace nift::rt::mutation {
struct SortController {
    static std::vector<Value>& grab(Value& v) { return Protocol::sort_items(v); }
};
}
void hijack(nift::rt::Value& v) { nift::rt::mutation::SortController::grab(v).clear(); }
