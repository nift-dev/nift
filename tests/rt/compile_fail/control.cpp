// B2 compile-fail control: the protocol's entry points compile (so the
// failures above are access failures, not a broken harness).
#include "rt/mutation.h"
nift::rt::mutation::Outcome ok(nift::rt::Value& array, const nift::rt::Value& item) {
    return nift::rt::mutation::set(array, 0, item, std::string_view("a"));
}
// Tooling review F10: the rvalue publish compiles (so build_publish_lvalue.cpp
// fails on the lvalue, not on a missing publish).
#include "rt/aggregate.h"
nift::rt::Value ok_publish() {
    nift::rt::build::Array a(1);
    return static_cast<nift::rt::build::Array&&>(a).publish();
}
