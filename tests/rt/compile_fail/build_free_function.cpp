// Tooling checkpoint (component 2): the former free construction function on a published Value. Must NOT compile.
#include "rt/aggregate.h"
#include <cstdlib>
using namespace nift::rt;
void bypass(nift::rt::Value& published) { array_push(published, nift::rt::Value()); }
