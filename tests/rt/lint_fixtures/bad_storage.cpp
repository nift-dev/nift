#include "rt/bad_storage.h"
// Known bypasses of the mutation protocol (storage-write must fire on each).
void bypass(Value& a, Value v) {
    a.heap_as<ArrayObject>()->items.push_back(v);
    array_mut(a)[0] = v;
    stack_push(a, v);
    mutation::stack_push(a, v, env);  // through the protocol: not flagged
}
