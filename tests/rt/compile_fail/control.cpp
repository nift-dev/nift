// B2 compile-fail control: the protocol's entry points compile (so the
// failures above are access failures, not a broken harness).
#include "rt/mutation.h"
nift::rt::mutation::Outcome ok(nift::rt::Value& array, const nift::rt::Value& item) {
    return nift::rt::mutation::set(array, 0, item, std::string_view("a"));
}
