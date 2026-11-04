// Tooling checkpoint (component 2): publish() needs an rvalue builder: std::move(builder).publish(). Must NOT compile.
#include "rt/aggregate.h"
#include <cstdlib>
using namespace nift::rt;
nift::rt::Value bypass() { nift::rt::build::Array a(1); return a.publish(); }
