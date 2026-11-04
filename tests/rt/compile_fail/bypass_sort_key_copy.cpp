// Tooling checkpoint (component 4), RECHECK2 N3: copying a key out of storage
// that never held one (malloc'd memory, rc2/forge/g07, or an inactive union
// member, g08) into a real SortKey object. SortKey has no copy or move
// constructor, so this must NOT compile (the diagnostic names the deleted
// SortKey copy constructor).
#include "rt/mutation.h"
#include <cstdlib>
#include <vector>
using namespace nift::rt;
void bypass(Value& published) {
    const mutation::SortKey& forged = *static_cast<const mutation::SortKey*>(std::malloc(1));
    mutation::SortKey copy(forged);
    mutation::Protocol::sort_items(copy, published).clear();
}
