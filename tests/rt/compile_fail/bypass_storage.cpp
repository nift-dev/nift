// B2 compile-fail test (owner B): a storage commit is private to the mutation
// protocol. Must NOT compile ("private" / "within this context").
#include "rt/aggregate.h"
void bypass(nift::rt::Value& array, const nift::rt::Value& item) { nift::rt::Storage::array_set(array, 0, item); }
