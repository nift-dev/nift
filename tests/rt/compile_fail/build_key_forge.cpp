// Tooling checkpoint (component 2): an in-place key made outside its owners. Must NOT compile.
#include "rt/aggregate.h"
#include <cstdlib>
using namespace nift::rt;
void bypass(nift::rt::Value& published) { const nift::rt::build::InPlaceKey key; nift::rt::Construction::array_push(key, published, nift::rt::Value()); }
