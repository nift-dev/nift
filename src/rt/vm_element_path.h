#pragma once
// Checkpoint 1, the assignment family (combined-b3-b5.md section 94): the run-time steps of an element
// store through a path (`g[i][j] = v`, `o["k"] = v`) that the plain array store (set_index) does not
// cover. A step through an intermediate container (GetIndex with Note::assign_path) and a store into an
// object (SetIndex on an object receiver) report legacy's assignment wording; neither writes anything
// before its checks pass.
#include "rt/opcodes.h"
#include "rt/proto.h"
#include "rt/value.h"

namespace nift::rt {
class CycleGraph;
struct Fault;

// One step of an assignment path: `object[key]`, the container the next step writes into. Legacy's
// texts: an index out of range is "index out of range: <key text>", a negative or non-integral index
// "assignment index does not address an array or object: <key text>", a missing member "object has
// no member: <key>". Kinds the compiler did not prove are an internal failure.
bool path_step(const Note* note, const Value& object, const Value& key, Value& out, Fault& fault);
// `object[key] = value` on an object with a string key, through the mutation protocol (immutable,
// cycle, then the commit); a refused cycle sets *cycle and returns false without raising.
bool set_member(const Note* note, Value& object, const Value& key, const Value& value, Fault& fault,
                const CycleGraph* graph, bool* cycle);

} // namespace nift::rt
