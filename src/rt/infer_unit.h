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
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nift::rt { struct SessionCallableShape; }

namespace nift::rt::infer_detail {

struct SessionLocationInference;
class Inference {
public:
    Inference(const Ast& ast, NodeId root, const UnitPlan& plan, const GlobalPlan* globals,
              const SessionCompileContext* session = nullptr,
              const std::vector<InvocationBinding>* invocation = nullptr);

    // Monotone unions over a finite (depth-capped) lattice. Worklist over the call graph (review D1): a unit
    // is re-walked only when its bindings, a parameter or a callee's return grew; its last walk sees its final
    // inputs. Out of budget: "not converged" (never "know nothing"), and the compiler refuses.
    UnitKinds run(std::size_t max_walks);
    // B4b 22 (tests): after convergence, walk every unit once more and record each node whose
    // kinds that walk changes (`stale()`). A converged result is a fixed point, so the list is
    // empty unless a unit kept facts an input change should have re-walked it for (the
    // invalidation invariant, checked directly: b4b_types_unit.cpp `fixpoint-*`).
    void verify_fixed_point() { verify_ = true; }
    const std::vector<NodeId>& stale() const { return stale_; }
    // B4b 27: analysis state the extra walk grew (a unit queued again, a walk that grew its own
    // inputs, new lambda units): not a fixed point even when no node's facts changed.
    const std::vector<std::string>& state_changed() const { return state_changed_; }
    std::size_t units_count() const { return plan_.functions.size() + 1 + lambdas_.size(); }

private:
    void enqueue(std::size_t unit);
    TyRef session_expression_seed(NodeId id);
    TyRef session_type(const TyRef&, const std::shared_ptr<const SessionCallableShape>&);
    std::size_t session_lambda_reference(NodeId);
    void seed_session();
    void session_begin_locations();
    void session_location_slot(std::uintptr_t, std::size_t);
    bool session_sync_locations();
    bool session_locations_converged() const;
    void export_session();
    TyRef session_call(const Node& n);
    TyRef session_bottom_arguments(const Node&);
    TyRef session_collection_call(const Node&, const std::vector<TyRef>&, int);
    TyRef session_unknown_call(const Node&, const TyRef&);
    TyRef session_historical_call(const Node&, const TyRef&);
    void session_call_edge(std::size_t target, std::size_t arity, std::size_t arguments);

    std::size_t current_unit() const;
    // B4b 23: an expression or statement walked (the analysis-work count `nodes`).
    const Node& visit(NodeId id) {
        ++result_.work.nodes;
        return ast_.at(id);
    }

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
    TyRef* find_binding(std::string_view name);
    // B4b 26 (the general dependency invariant, b4b-element-types.md section 18): every binding
    // slot (a local, a parameter, a capture, a global, a struct field) records every unit whose
    // walk read it, whatever the unit's lexical relation to the slot's declaration (the declaring
    // function, a lambda, main, a method, a module function); when the slot grows, in any unit's
    // walk, every reader but the unit being walked (`self_changed_`) is queued again.
    TyRef* note_read(TyRef* var);              // `var` read by the current unit (find, struct_field)
    bool slot_changed(const TyRef* var);       // `var` grew: its readers walk again
    std::vector<std::size_t> slot_read_;  // F1: per binding slot, the last walk that read it
    std::size_t walk_serial_ = 0;

    // ---- CP12b structs (rt/structs.h) ----
    // The struct whose method is being walked, or -1.
    int current_struct() const;
    bool own_this(NodeId receiver) const;
    TyRef mixed_presentation(const Node& call);  // B4
    bool mixed_presentation_call(const Node& call);  // B4 N3: the enclosing struct's `this` (not a binding named so)
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
    TyRef collection_call(const Node&, NodeId, const TyRef&, const std::vector<TyRef>&, int);
    // D-063: a method of a map/set/stack value (stores widen the binding's element type).
    TyRef keyed_result(const Node&, const TyRef&, const std::vector<TyRef>&, std::string_view);
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
    void interpolation(const Node& n);  // CP25-R: a string's `$[...]` expressions (infer_walk.cpp)

    TyRef call(const Node& n);
    TyRef thread_call(const Node& n);  // CP22: `thread(f, args…)`, or null when the call is not one

    void block(NodeId id);

    void statements(const Node& b);

    void add_element(NodeId receiver, const TyRef& value, bool key = false);  // key: a map key (D-063)
    TyRef set_members(const TyRef& set) const;
    TyRef map_keys(const TyRef& map) const;
    // D-NEXT-refsem: collections are shared references, so what one alias
    // stores every alias may read. Contents are therefore tracked per
    // allocation site (Ty::sites; every alias of an object carries its site),
    // never per binding: sound under any aliasing, like the per-type struct
    // field slots. Growth re-walks every unit.
    // `name`: a map value stored or read under a string-literal key (a
    // record-like map keeps one type per such key), or null.
    TyRef contents(const TyRef& collection, bool keys, const std::string* name = nullptr) const;
    void add_contents(const TyRef& collection, const TyRef& value, bool keys, const std::string* name = nullptr);
    const std::string* literal_key(const Node& call, std::string& storage) const;
    TyRef constructed(const Node& call, const TyRef& result) const;
    bool tagged() const { return plan_.collections || !plan_.structs.empty(); }
    void propagate_session_alias(std::size_t slot);

    void statement(NodeId id);
    void for_statement(const Node&);

    void walk_main();

    void walk_function(std::size_t f);
    void walk_unit(std::size_t unit);

    const Ast& ast_;
    NodeId root_;
    const UnitPlan& plan_;
    GlobalSlots globals_;
    const SessionCompileContext* session_ = nullptr;
    const std::vector<InvocationBinding>* invocation_ = nullptr;
    std::shared_ptr<SessionLocationInference> session_locations_;
    std::unordered_map<std::string_view, std::size_t> external_;
    UnitKinds result_;
    bool verify_ = false;
    std::vector<NodeId> stale_;
    std::vector<std::string> state_changed_;
    void check_fixed_point();
    std::vector<std::vector<TyRef>> params_;
    std::vector<TyRef> returns_;
    std::vector<TyRef> vars_;
    std::unordered_map<std::size_t,NodeId> binding_decls_;
    std::unordered_set<std::size_t> const_arrays_;
    std::unordered_map<std::size_t, std::pair<std::size_t, NodeId>> session_for_aliases_;
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
    // D-NEXT-refsem: per allocation site, the map keys, the map values / set
    // members / stack elements; `contents_unknown_`: something was stored into
    // a collection of unknown origin, so any collection may hold anything.
    struct SiteContents {
        TyRef keys, values;                                    // values: under any other key
        std::map<std::string, TyRef, std::less<>> named;        // map values under string-literal keys
    };
    std::map<std::uint32_t, SiteContents> contents_;
    bool contents_unknown_ = false;

    // ---- B4b (b4b-element-types.md): arrays and objects per allocation site ----
    // A value's type carries its sites; the stores of every alias go to the
    // sites (agg_), and every read is materialized from them (record()).
    TyRef materialize(const TyRef& t);
    TyRef materialize_now(const TyRef& t, std::vector<std::uint32_t>& read);
    TyRef pruned(const TyRef& value);  // F1: a stored value as it reads
    void prune_parts(std::shared_ptr<Ty>& t, bool& changed, std::vector<std::uint32_t>& read);
    TyRef prune_part(const TyRef& part, std::vector<std::uint32_t>& read, int depth);  // F1: opened: anything
    struct Materialized {
        TyRef in, out;
        std::vector<std::uint32_t> read;  // the classes the result depends on
        std::size_t unit;                 // the last unit entered in their readers lists
    };
    std::unordered_map<const Ty*, Materialized> materialized_;  // F1: valid while site_version_ holds
    std::uint64_t site_version_ = 0, memo_version_ = 0;
    TyRef sited(NodeId id, const TyRef& t);             // record(): materialized, or of unknown origin
    TyRef fresh(NodeId site, const TyRef& t);           // a construction: allocation site `site`
    std::uint32_t new_site();                           // a site without a node (an invocation seed)
    bool has_site(const TyRef& t) const;
    std::uint32_t derived(std::uint32_t site);          // the site of the untracked parts inside `site`
    void site_store(const TyRef& container, const TyRef& value, const std::string* key);
    void dynamic_mutation(const TyRef& recv, std::string_view name, const std::vector<TyRef>& values);
    void escape_callables(const std::vector<TyRef>& values);  // C4: called where the inference does not see
    void untracked_call();                                    // C4: a call whose targets are unknown
    void site_grew();                                   // every unit re-walks (unknown contents, C4, C5)
    void site_changed(std::uint32_t rep);               // H2: the readers of one site class re-walk
    std::uint32_t rep(std::uint32_t site);              // H2: a site's class (union-find)
    void unite_sites(std::uint32_t a, std::uint32_t b);
    std::vector<std::uint32_t> classes(const std::vector<std::uint32_t>& sites, bool& open);
    TyRef canonical(const TyRef& t);                    // H2: a value's sites as at most 16 classes
    std::vector<std::uint32_t> with_classes(const std::vector<std::uint32_t>& raw,
                                            const std::vector<std::uint32_t>& classes) const;
    TyRef session_effects(const TyRef& result, const Node& call);  // C5: a call of an earlier statement's code
    void expose(const TyRef& t, int depth = 0);                     // C5: reachable by earlier code
    void expose_sites(const std::vector<std::uint32_t>& sites);
    void session_open();
    void session_store(std::string_view name, const TyRef* var, const TyRef& value);
    void thread_target(const TyRef& f);                 // H1: thread/async entry of possibly unknown targets
    bool exact_callee(const TyRef& f) const;            // H1: every target known
    std::unordered_set<std::uint32_t> exposed_, opened_, collapsed_;
    std::unordered_map<std::uint32_t, std::uint32_t> parent_;
    // F1: a class's readers in the order they first read it; membership in
    // constant time (a collapsed class is read by every unit of the shape).
    // B4b 28 (review of 4c9632b8, M-1): `notified` is the position in dequeued_ of the unit
    // being walked when the list was last notified (Inference::notify): every reader in the
    // list then was queued, so a later notification need only look at the units dequeued
    // since (each one's position in the list in constant time), never the whole list again.
    struct UnitSet {
        static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
        std::vector<std::size_t> list;
        std::vector<std::uint32_t> pos;  // per unit, its index in `list` + 1 (built once the list is long)
        std::size_t notified = 0;
        void insert(std::size_t u) {
            if (list.size() < 16) {
                for (std::size_t v : list)
                    if (v == u) return;
                list.push_back(u);
                if (list.size() == 16)
                    for (std::size_t k = 0; k < list.size(); ++k) mark(list[k], k);
                return;
            }
            if (u < pos.size() && pos[u]) return;
            mark(u, list.size());
            list.push_back(u);
        }
        std::size_t index(std::size_t u) const {
            if (list.size() < 16) {
                for (std::size_t k = 0; k < list.size(); ++k)
                    if (list[k] == u) return k;
                return kNone;
            }
            return u < pos.size() && pos[u] ? pos[u] - 1 : kNone;
        }
        void mark(std::size_t u, std::size_t at) {
            if (u >= pos.size()) pos.resize(u + 1, 0);
            pos[u] = static_cast<std::uint32_t>(at + 1);
        }
        std::size_t size() const { return list.size(); }
    };
    // B4b 28: queue every reader in `readers` (but `skip`) that is not queued, in list order: the
    // same units in the same order as enqueue() of the whole list, at the cost of the shorter of
    // the list and the walks since its last notification.
    void notify(UnitSet& readers, std::size_t skip = UnitSet::kNone);
    // B4b 28: `unit` (the unit being walked) reads what `readers` lists: a new list starts at
    // the current position (each later member is the unit walked when it joins)
    void add_reader(UnitSet& readers, std::size_t unit) {
        if (readers.list.empty()) readers.notified = dequeued_.empty() ? 0 : dequeued_.size() - 1;
        readers.insert(unit);
    }
    std::vector<std::size_t> dequeued_;  // B4b 28: the units in the order run() took them
    std::unordered_map<std::uint32_t, UnitSet> readers_;
    std::unordered_map<std::size_t, UnitSet> slot_readers_;  // B4b 26: binding slot -> reader units
    bool earlier_called_ = false, closure_exposed_ = false, untracked_seen_ = false;
    std::size_t untracked_escaped_ = 0;  // B4b 28: value_units_ already escaped by untracked_call
    const std::string* store_key(NodeId index, std::string& storage) const;  // a literal string index
    void element_update(NodeId target, const TyRef& value, Op op);          // `xs[i] += v`, `o.k++`
    std::map<std::uint32_t, SiteContents> agg_;
    std::unordered_set<std::uint32_t> agg_sites_, nonpositional_;
    std::unordered_map<std::uint32_t, std::uint32_t> derived_;
    std::unordered_set<NodeId> callee_nodes_;
    std::set<std::size_t> value_units_;
    std::set<std::uint32_t> escaped_units_;
    std::uint32_t next_site_ = 0x80000000u;
    bool agg_unknown_ = false, sites_ready_ = false;
    // B5 (U6, b5-refsem.md section 3): the session heap. In a persistent session every array and
    // object an earlier statement made (a binding's value and its parts at every depth, a seed of
    // earlier code) is one site class of this unit's analysis: its contents start as every kind any
    // earlier value's contents may hold (SessionCompileContext::heap and the bindings' recorded
    // structures, seed_session), and every store through any earlier value grows them, so no read
    // of an earlier value keeps a statement-old assumption. Exposed (earlier code reaches it).
    std::uint32_t heap_site_ = kOpenSite, heap_objects_site_ = kOpenSite;  // arrays' and objects' pools
    TyRef heap_sited(const TyRef& t);  // an earlier value: in the pool(s) of its container kind(s)
    TyRef session_export(const TyRef& t, int depth);  // a recorded structure: materialized, no sites
    // B5 (U6): the site classes `t` reaches at every depth (class -> depth; `shared`: a part
    // whose holders the analysis cannot bound, or one class at two depths).
    void session_reach(const TyRef& t, int depth, std::map<std::uint32_t, int>& at, bool& shared);
};

} // namespace nift::rt::infer_detail
