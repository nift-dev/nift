// Tooling checkpoint (component 2): a builder cannot be copied. Must NOT compile.
#include "rt/aggregate.h"
#include <cstdlib>
using namespace nift::rt;
void bypass() { nift::rt::build::Array a(1); nift::rt::build::Array b(a); }
