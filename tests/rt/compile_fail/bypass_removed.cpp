// B2 compile-fail test (owner B): the B1 free commit primitives no longer
// exist outside the protocol. Must NOT compile ("not declared").
#include "rt/aggregate.h"
void bypass(nift::rt::Value& map, const nift::rt::Value& key) { nift::rt::map_set(map, key, key); }
