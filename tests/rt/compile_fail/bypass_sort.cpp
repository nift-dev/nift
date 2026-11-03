// B2 compile-fail test (review M1, the reviewer's bypass_public.cpp): the
// protocol's sort commits hand out a receiver's raw storage, so they are
// private to the sort controller. Must NOT compile.
#include "rt/mutation.h"
void bypass(nift::rt::Value& receiver) { nift::rt::mutation::Protocol::sort_items(receiver).clear(); }
