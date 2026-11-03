// B2 compile-fail test (re-review C1): the reviewer's rr/lint/sort_plain.cpp.
// Must NOT compile.
#include "rt/sort_controller.h"
void s2(nift::rt::Value& published) { nift::rt::mutation::SortController::items(published).clear(); }
