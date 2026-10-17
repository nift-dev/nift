#pragma once
// Static refusal of code whose meaning depends on legacy's prepared
// evaluator (CP11 review round 2: M1, M2, M3, N1).
//
// Legacy runs loop bodies, and the functions called from them, through a
// second evaluator (src/ParserTemplate.cpp prepare_loop_body /
// execute_prepared, src/Ast.cpp parse_expression) whose rules differ from
// the textual evaluator that runs everything else; the full inventory is in
// docs/runtime-2/checkpoints/cp11-vm-poc.md ("Legacy's prepared evaluator").
// Instead of modelling those rules the compiler refuses, before anything
// runs, the shapes whose meaning differs:
//
//   prepared-decl-type   in prepared-reachable code, a declaration whose
//                        initializer text refers to another binding (`b := a`,
//                        `b := (a)`, `b := a * 2`): legacy fixes its type from
//                        whatever binding of that name is visible when the
//                        body is prepared (M1, N1).
//   prepared-fn-unprepared
//                        a function called from prepared-reachable code whose
//                        body legacy cannot prepare (a bare `return`, or an
//                        `if`/`while` condition or `return` expression the
//                        prepared parser rejects): legacy evaluates the
//                        arguments, abandons the preparation and re-runs the
//                        whole call textually, evaluating them again (M2).
//   string-arg-type      a string literal argument to a user function whose
//                        unquoted text legacy types as a double or as a
//                        binding declared somewhere in the unit (`f("2.5")`,
//                        `f("q")` with a binding `q`): legacy types the
//                        parameter from that text (M3).
//
// Prepared-reachable code (over-approximated, which only refuses more): the
// conditions and bodies of every `while` loop, anywhere, and the whole body
// of every function called from prepared-reachable code (fixpoint).
#include "rt/resolver.h"

#include <cstdint>
#include <string>

namespace nift::rt {

struct PreparedRefusal {
    bool refused = false;
    std::string reason;
    std::uint32_t offset = 0;
};

PreparedRefusal check_prepared_contexts(const Ast& ast, NodeId root, const UnitPlan& plan);

} // namespace nift::rt
