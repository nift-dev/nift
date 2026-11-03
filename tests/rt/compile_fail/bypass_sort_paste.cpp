// B2 compile-fail test (re-review C1): the reviewer's rr/lint/sort_bypass.cpp,
// which passed rt-lint (an include path with `//`, a token-pasted name). The
// accessor without a SortKey does not exist. Must NOT compile.
#include "rt//sort_controller.h"
#define CAT(a, b) a##b
namespace nift::rt {
void s1(Value& published) { mutation::CAT(Sort, Controller)::items(published).clear(); }
}
