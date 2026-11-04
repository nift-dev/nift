// Tooling checkpoint (component 2): a builder cannot be made from an existing (published) Value (rc: reconstruct). Must NOT compile.
#include "rt/aggregate.h"
#include <cstdlib>
using namespace nift::rt;
void bypass(nift::rt::Value& published) { nift::rt::build::Array b(published); b.push(nift::rt::Value()); }
