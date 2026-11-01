#pragma once
// The Phase A forward-compatibility guard's analysis (reference_guard.h),
// internal: the per-binding facts, the syntax walk that collects them
// (reference_guard_walk.cpp) and the freshness rules and verdict
// (reference_guard.cpp).
#include "rt/ast.h"
#include "rt/infer.h"
#include "rt/natives/builtin.h"
#include "rt/resolver.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nift::rt::reference_guard_detail {

constexpr KindSet kShared = kKindArray | kKindObject | kKindUnknown;

// `x = f(..., x, ...)`: the call, the argument node, the callee and position.
struct Idiom {
    NodeId call = kNoNode, arg = kNoNode;
    int fn = -1;
    std::uint32_t k = 0;
};

struct Facts {
    bool mut_top = false, mut_nested = false;
    NodeId mut_at = kNoNode;
    std::vector<NodeId> retained_top, retained_sub, inflows, contents;
    std::vector<Idiom> idioms;
    bool loop = false;      // a for-loop binding: a part of `loop_root`
    NodeId loop_root = kNoNode;
    int param_fn = -1;      // a parameter: function index (-2: a lambda's)
    bool returned = false;  // a parameter returned as it is (an escape, checked at the call sites)
    bool isolated = false;  // an async function's or lambda's parameter: given copies
    std::uint32_t param_k = 0;
    int owner = -1;         // the body declaring it (-1: main or a global; a lambda: lambda_owner)
    bool declared = false;  // `owner` is known (a declaration, parameter or loop binding)
    bool collection_part = false;  // a loop binding over a map, set or stack: an element of an unknown holder
    std::vector<NodeId> mut_sites;  // every in-place mutation rooted here (the lent rule)
};

// A lending: a binding's value (or a part) handed for the duration of a call,
// a native's callback run or a loop to a holder the guard does not track
// (a parameter that does not escape, a native, a loop binding).
struct Lend {
    NodeId decl = kNoNode;
    NodeId region = kNoNode;  // the call or for node: its source span
    NodeId value = kNoNode;   // the lent expression (mutations inside it end before the lending starts)
};

class Guard {
public:
    Guard(const Ast& ast, const UnitPlan& plan, const UnitKinds& kinds, bool session)
        : ast_(ast), plan_(plan), kinds_(kinds), session_(session) {}

    bool run(NodeId root, std::string& reason, std::uint32_t& offset);

    // A session recompile's capture initializer (reference_guard.h).
    void carry(NodeId initializer) { carried_.insert(initializer); }

private:
    KindSet kinds(NodeId id) const { return kinds_.of(id); }
    bool shared(NodeId id) const { return id != kNoNode && (kinds(id) & kShared) != 0; }
    const Node& at(NodeId id) const { return ast_.at(id); }

    // ---- bindings ----
    NodeId decl(NodeId ident, int owner);
    Facts& facts(NodeId d) { return facts_[d]; }

    void note_params();

    // The binding an expression is a part of (or is), and whether it is the
    // binding's whole value. kNoNode with `known` false: an unresolved root.
    struct Root {
        NodeId decl = kNoNode;
        bool whole = false;
        bool known = false;
        bool opaque = false;      // held by something no binding tracks (a struct instance, a collection)
        bool collection = false;  // an element of a map, set or stack (a loop binding over one)
    };
    Root root_of(NodeId id, int owner);

    // ---- freshness ----
    // An expression whose value no other holder can reach: at the top level
    // (deep false) or at every depth (deep true).
    bool fresh(NodeId id, bool deep, int depth = 0, NodeId handed = kNoNode) const;

    bool nested(NodeId id) const { return kinds_.nested_aggregates.count(id) != 0; }

    bool fresh_rule(natives::ResultRule rule, NodeId id, bool deep) const;

    bool fresh_call(NodeId id, bool deep, int depth) const;

    bool returns_fresh(std::size_t f, bool deep, int depth) const;

    // ---- the walk ----
    void retain(NodeId id, int owner);

    void mutate(NodeId receiver, bool whole_receiver, NodeId site, int owner);

    void store_into(NodeId receiver, NodeId value, int owner);

    void walk_list(const Node& n, bool keep, int owner);

    void walk(NodeId id, bool keep, int owner);

    // A `for` loop: the lending of its iterable, its bindings' roots.
    void walk_for(NodeId id, int owner);

    // A user function, constructor or method's parameter k escapes.
    bool escapes(int f, std::uint32_t k) const;

    int user_function(const Node& n, int owner) const;

    void call(NodeId id, bool keep, int owner);

    // `x = f(..., x, ...)`: x passed once, as a bare name, and rebound to the
    // result, so its old value is never observed again (if no other holder).
    void idiom(const Node& assign, NodeId d, int owner);

    // A lambda's own body: a distinct owner per lambda (captures are cells).
    static int lambda_owner(NodeId lambda) { return -4 - static_cast<int>(lambda); }

    // Referenced from a body other than its declaring one (or from two
    // bodies), or in a session: code the binding's own frame does not run
    // may reach it (a callee, a callback, a later statement).
    bool outside(NodeId d) const;

    // The call or loop node `region` may run user code: a user function, a
    // callable value, a native given a callable that is not a lambda literal,
    // or any such call written inside it.
    bool user_code(NodeId region) const;

    static bool within(const Node& site, const Node& region) {
        return site.begin >= region.begin && site.end <= region.end;
    }

    void lend(NodeId value, NodeId region, int owner);

    // Lambda literal arguments of a native: their parameters receive parts
    // of `root` (or of something unknown when root is kNoNode).
    void tie_callbacks(const Node& call, NodeId root, bool known);

    bool callable_value(NodeId arg) const;

    void native_operands(const Node& n, NodeId receiver, NodeId id, int owner);

    // ---- escapes (fixpoint) ----
    bool mutated(const Facts& f) const { return f.mut_top || f.mut_nested || !f.idioms.empty(); }

    bool grow_escapes();

    // ---- the verdict ----
    bool fail(NodeId at_node, const char* route, std::string& reason, std::uint32_t& offset) const;

    bool check(std::string& reason, std::uint32_t& offset);

    // The idiom call's result is x's own (exclusive) value or fresh: every
    // return of the callee is fresh or the idiom parameter itself.
    bool idiom_result(const Facts& f, NodeId value) const;

    bool idiom_argument(NodeId arg) const;

    const Ast& ast_;
    const UnitPlan& plan_;
    const UnitKinds& kinds_;
    bool session_;
    std::unordered_map<NodeId, Facts> facts_;
    std::vector<std::vector<bool>> escape_;
    std::vector<std::vector<NodeId>> returns_;
    std::vector<std::pair<NodeId, int>> calls_;
    std::vector<NodeId> free_reads_, unknown_, unknown_mutations_;
    std::vector<NodeId> opaque_mutations_;  // through an element of a map, set or stack
    std::unordered_set<NodeId> idiom_args_;
    std::unordered_set<NodeId> carried_;
    bool carried(NodeId value) const { return carried_.count(value) != 0; }
    std::unordered_map<NodeId, std::vector<int>> owners_;  // decl -> the bodies referencing it
    std::vector<Lend> lends_;
    std::vector<NodeId> user_calls_;   // calls that may run user code
    std::vector<bool> fn_value_;       // a user function used as a value (unknown callers)
    int async_depth_ = 0;  // inside an async body: outer bindings are the task's copies
    std::unordered_map<std::uint64_t, NodeId> isolated_;  // (owner, outer binding) -> its task copy
};

} // namespace nift::rt::reference_guard_detail
