#include "rt/bad_construction.h"
// The reviewer's bypass_public.cpp cases: the construction API on an existing
// receiver and the sort commits outside the sort controller (storage-write
// must fire on each line, and not on the annotated or protocol lines).
void bypass(Value& receiver, const Value& item) {
    array_push(receiver, item);
    array_build_storage(receiver).push_back(item);
    object_set(receiver, Atom{}, item);
    mutation::sort_storage(receiver).push_back(item);
    mutation::Protocol::sort_items(receiver).clear();
    Value fresh = make_array(1);
    array_push(fresh, item);  // rt-lint: storage-ok fresh, the value this function just made
    mutation::push(receiver, item, env);  // through the protocol: not flagged
}
