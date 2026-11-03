// B2 compile-fail test (owner B): the protocol's commit (stage 3 without
// stages 2) is private. Must NOT compile ("private" / "within this context").
#include "rt/mutation.h"
void bypass(nift::rt::Value& array) {
    // The edit is passed as {} so the rejection is the commit's own access,
    // not the (also private) ArrayEdit enumeration's.
    nift::rt::mutation::Protocol::commit_array(array, {}, 0, 0, nift::rt::Value());
}
