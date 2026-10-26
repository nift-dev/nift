#pragma once
// The unit kind inference's worklist engine (rt/infer.h), internal (CP12):
// one class, its expressions and calls in infer.cpp, its statements, units
// and CP12b struct members in infer_walk.cpp.
#include "rt/infer.h"
#include "rt/infer_globals.h"
#include "rt/infer_types.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nift::rt { struct SessionCallableShape; }

namespace nift::rt::infer_detail {

class Inference {
public:
    Inference(const Ast& ast, NodeId root, const UnitPlan& plan, const GlobalPlan* globals,
              const SessionCompileContext* session = nullptr);

    // Monotone unions over a finite (depth-capped) lattice. Worklist over the call graph (review D1): a unit
    // is re-walked only when its bindings, a parameter or a callee's return grew; its last walk sees its final
    // inputs. Out of budget: "not converged" (never "know nothing"), and the compiler refuses.
    UnitKinds run(std::size_t max_walks);

private:
    void enqueue(std::size_t unit);
    TyRef session_expression_seed(NodeId id);
    TyRef session_type(const TyRef&, const std::shared_ptr<const SessionCallableShape>&);
    std::size_t session_lambda_reference(NodeId);
    void seed_session();
    void export_session();
    TyRef session_call(const Node& n);

    std::size_t current_unit() const;

    using Scope = std::vector<std::pair<std::string_view, std::size_t>>;

    // CP12C: a lambda is a callable unit after main in the worklist. Its
    // parameter/return slots are separate from the named functions' vectors.
    struct LambdaInfo {
        NodeId node = kNoNode;
        int owner_function = -1;  // enclosing named function for global lookup
        std::vector<TyRef> params;
        TyRef returns;
        TyRef collection_returns;
        std::set<std::size_t> callers;
        std::vector<Scope> scopes;  // lexical scopes at the lambda expression
    };

    // Unions `value` into `into`; true if it grew (the caller re-queues).
    bool merge(TyRef& into, const TyRef& value);
    void merge_local(TyRef& into, const TyRef& value);
    void merge_return(const TyRef& value);
    void merge_lambda_return(std::size_t lambda, const TyRef& value);

    void declare(std::string_view name, NodeId at, const TyRef& value);

    TyRef* find(std::string_view name);

    // ---- CP12b structs (rt/structs.h) ----
    // The struct whose method is being walked, or -1.
    int current_struct() const;
    // A field's one slot, shared by every unit (like a global's).
    std::size_t field_slot(const StructFieldInfo& field);
    static KindSet value_kinds(const Value& v);
    void seed_struct_fields();
    // The struct of the receiver `object` of a member access or method call
    // (`this` in a method, or an instance), or -1.
    int receiver_struct(NodeId object);
    TyRef* struct_field(int s, std::string_view name);
    // A call of method `f` (a function index) with `n`'s arguments.
    TyRef method_call(std::size_t f, const Node& n);
    // CP12C: merge call arguments into every callable unit a callee value may
    // be, enqueue the ones that grew, and return the union of their returns.
    TyRef collection_callables(const TyRef&,const std::vector<TyRef>&,bool&);
    TyRef collection_result(const Node&, const TyRef&, const std::vector<TyRef>&, int);
    TyRef call_callables(const TyRef& callee, const std::vector<TyRef>& values, bool& grew);
    // The unit of a lambda expression, creating it on first sight.
    std::size_t lambda_unit(NodeId node);
    void walk_lambda(std::size_t id);

    // Constant-folded literal arithmetic (doubles; only the kind is used).
    TyRef constant(NodeId id, double v);
    TyRef number_literal(NodeId id);

    TyRef record(NodeId id, TyRef t);

    int function_index(const Node& callee) const;

    TyRef expr(NodeId id);

    TyRef call(const Node& n);

    void block(NodeId id);

    void statements(const Node& b);

    void add_element(const Node& receiver, const TyRef& value);

    void statement(NodeId id);

    void walk_main();

    void walk_function(std::size_t f);
    void walk_unit(std::size_t unit);

    const Ast& ast_;
    NodeId root_;
    const UnitPlan& plan_;
    GlobalSlots globals_;
    const SessionCompileContext* session_ = nullptr;
    std::unordered_map<std::string_view, std::size_t> external_;
    UnitKinds result_;
    std::vector<std::vector<TyRef>> params_;
    std::vector<TyRef> returns_;
    std::vector<TyRef> vars_;
    std::unordered_map<std::size_t,NodeId> binding_decls_;
    std::vector<std::size_t> persist_index_;
    Constants constants_;
    std::vector<Scope> scopes_;
    std::vector<std::set<std::size_t>> callers_;  // per function: the units reading its return
    std::vector<LambdaInfo> lambdas_;
    std::unordered_map<NodeId, std::size_t> lambda_unit_;
    std::unordered_set<NodeId> top_level_decls_;
    std::unordered_map<std::string_view, std::size_t> top_level_slots_;
    int owner_function_ = -1;  // for a lambda unit: its enclosing named function
    std::deque<std::size_t> queue_;
    std::vector<bool> queued_;
    int current_ = -1;
    bool self_changed_ = false;
};

} // namespace nift::rt::infer_detail
