#include "rt/session_alias.h"
// rt-lint: cold-file compatibility entry; storage effects now share the inference callable-ID lifetime
namespace nift::rt {
UnitKinds infer_session_aliases(const Ast& ast, NodeId root, const UnitPlan& plan, std::size_t walks,
                               const GlobalPlan* globals, SessionCompileContext* session) {
    return infer_unit_kinds(ast,root,plan,walks,globals,session);
}
}
