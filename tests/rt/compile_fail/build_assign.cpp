// Tooling checkpoint (component 2): a builder cannot be assigned (a second builder over a value). Must NOT compile.
#include "rt/aggregate.h"
#include <cstdlib>
using namespace nift::rt;
void bypass() { nift::rt::build::Array a(1), b(1); b = static_cast<nift::rt::build::Array&&>(a); }
