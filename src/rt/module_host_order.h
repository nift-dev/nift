#pragma once
// Slot 2 (owner 2026-11-11 05:41, D-068 #2): the host-import guard, Option A
// (docs/runtime-2/checkpoints/dead-code-admission.md 5.5). R1 (owner,
// 2026-10-21): the VM reads every module source before it runs; legacy reads
// each at its import. A linked unit with a host op is still admitted when no
// host op can run before the last module read:
//   E  every top-level statement of the entry before its last import is an
//      import (or a comment): a strict leading import block;
//   M  the code that runs while the modules initialise (the main proto up to
//      its last module instruction, so every module of the graph,
//      transitively) has no host op and no call: only operations classified
//      safe below, natives classified Pure (and print), and no call of a
//      built-in that a binding of its name may shadow. `import`/`export` are
//      not calls: the linker splices and blanks them (module_link.cpp).
// Anything not classified is refused (unknown effects: conservative).
#include "rt/ast.h"
#include "rt/module_link.h"
#include "rt/natives/builtin.h"
#include "rt/proto.h"

#include <cstdint>
#include <string>

namespace nift::rt {

// The permanent effect classification of every built-in (D-068 #2):
// tests/runtime2/host_set_complete.py fails when an id is unclassified.
enum class BuiltinEffect : std::uint8_t { Unclassified, Pure, Host, Shared };
BuiltinEffect builtin_effect(natives::BuiltinId id);

// True when the linked program may run although it contains a host op.
bool host_order_admits(const Program& program, const LinkedUnit& link, const Ast& entry, NodeId root);

// The R1 refusal of a linked unit (shared by run_unit and the refusal
// inventory): its first host op (rt/module_host_op.h), unless admitted above.
bool host_order_refusal(const Program& program, const LinkedUnit& link, const Ast& entry, NodeId root,
                        std::string& reason, std::uint32_t& offset, bool* positioned = nullptr);

} // namespace nift::rt
