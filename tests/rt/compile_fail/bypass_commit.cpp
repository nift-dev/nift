// B2 compile-fail test (owner B): the protocol's commit (stage 3 without
// stages 2) is private. Must NOT compile ("private" / "within this context").
#include "rt/mutation.h"
void bypass(nift::rt::Value& array) {
    nift::rt::mutation::Protocol::commit_array(array, nift::rt::mutation::Protocol::ArrayEdit::Clear, 0, 0,
                                               nift::rt::Value());
}
