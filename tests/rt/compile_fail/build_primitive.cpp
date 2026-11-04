// Tooling checkpoint (component 2): the construction primitive behind the builders. Must NOT compile.
#include "rt/aggregate.h"
#include <cstdlib>
using namespace nift::rt;
void bypass(nift::rt::Value& published) { nift::rt::Construction::array_push(published, nift::rt::Value()); }
