// Tooling checkpoint (component 2): an in-place key made by byte-copying. Must NOT compile.
#include "rt/aggregate.h"
#include <cstdlib>
using namespace nift::rt;
void bypass(nift::rt::Value& published) { nift::rt::Construction::array_push(__builtin_bit_cast(nift::rt::build::InPlaceKey, static_cast<unsigned char>(0)), published, nift::rt::Value()); }
