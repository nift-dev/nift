// Tooling checkpoint (component 4), RECHECK2 N3 (rc2/forge/g06): a SortKey made
// by byte-copying (__builtin_bit_cast; std::bit_cast in C++20) was well-defined
// while SortKey was trivially copyable. With its user-provided destructor it is
// not, so the cast must NOT compile (the diagnostic names "trivially copyable").
#include "rt/mutation.h"
#include <vector>
using namespace nift::rt;
void bypass(Value& published) {
    mutation::Protocol::sort_items(__builtin_bit_cast(mutation::SortKey, static_cast<unsigned char>(0)), published).clear();
}
