// Tooling checkpoint (component 2): an in-place key copied out of storage that never held one. Must NOT compile.
#include "rt/aggregate.h"
#include <cstdlib>
using namespace nift::rt;
void bypass(nift::rt::Value& published) { const auto& forged = *static_cast<const nift::rt::build::InPlaceKey*>(std::malloc(1)); nift::rt::build::InPlaceKey copy(forged); nift::rt::Construction::array_push(copy, published, nift::rt::Value()); }
