// B2 compile-fail test (re-review C1): including rt/sort_controller.h grants
// nothing: SortController::items needs a SortKey, which only the sort functions
// can make (private, user-provided constructor). Must NOT compile.
#include "rt/sort_controller.h"
#include <vector>
using namespace nift::rt;
void bypass(Value& published) { mutation::SortController::items(mutation::SortKey{}, published).clear(); }
