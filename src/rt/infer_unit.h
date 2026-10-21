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
#include <utility>
#include <vector>

namespace nift::rt::infer_detail {

class Inference {
public:
    Inference(const Ast& ast, NodeId root, const UnitPlan& plan, const GlobalPlan* globals);

    // Monotone unions over a finite (depth-capped) lattice. Worklist over the call graph (review D1): a unit
    // is re-walked only when its bindings, a parameter or a callee's return grew; its last walk sees its final
    // inputs. Out of budget: "not converged" (never "know nothing"), and the compiler refuses.
    UnitKinds run(std::size_t max_walks);

private:
    void enqueue(std::size_t unit);

    std::size_t current_unit() const;

    using Scope = std::vector<std::pair<std::string_view, std::size_t>>;

    // Unions `value` into `into`; true if it grew (the caller re-queues).
    bool merge(TyRef& into, const TyRef& value);
    void merge_local(TyRef& into, const TyRef& value);
    void merge_return(const TyRef& value);

    void declare(std::string_view name, NodeId at, const TyRef& value);

    TyRef* find(std::string_view name);

    // ---- CP12b structs (rt/structs.h) ----
    // The struct whose method is being walked, or -1.
    int current_struct() const;
    // A field's one slot, shared by every unit (like a global's).
    std::size_t field_slot(const StructFieldInfo& field);
    static std::uint8_t value_kinds(const Value& v);
    void seed_struct_fields();
    // The struct of the receiver `object` of a member access or method call
    // (`this` in a method, or an instance), or -1.
    int receiver_struct(NodeId object);
    TyRef* struct_field(int s, std::string_view name);
    // A call of method `f` (a function index) with `n`'s arguments.
    TyRef method_call(std::size_t f, const Node& n);

    // Constant-folded literal arithmetic (doubles; only the kind is used).
    TyRef constant(NodeId id, double v);

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

    const Ast& ast_;
    NodeId root_;
    const UnitPlan& plan_;
    GlobalSlots globals_;
    UnitKinds result_;
    std::vector<std::vector<TyRef>> params_;
    std::vector<TyRef> returns_;
    std::vector<TyRef> vars_;
    std::vector<std::size_t> persist_index_;
    Constants constants_;
    std::vector<Scope> scopes_;
    std::vector<std::set<std::size_t>> callers_;  // per function: the units reading its return
    std::deque<std::size_t> queue_;
    std::vector<bool> queued_;
    int current_ = -1;
    bool self_changed_ = false;
};

} // namespace nift::rt::infer_detail
