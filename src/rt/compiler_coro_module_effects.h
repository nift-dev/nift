#pragma once
// What code in a linked coroutine unit may do when it runs, for the module
// admission rules (rt/compiler_coro_module.h): each function's effects,
// call-graph closures, and what a call through a value may reach.
// Conservative: a flag may be set for code that never does it, never the reverse.
#include "rt/ast.h"
#include "rt/infer.h"
#include "rt/resolver.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Test-only deliberate defects (`-DNIFT_CORO_MODULE_MUTANT=n`,
// tests/runtime2/coro_module/mutants.sh). Normal builds contain none.
#ifndef NIFT_CORO_MODULE_MUTANT
#define NIFT_CORO_MODULE_MUTANT 0
#endif

namespace nift::rt {

struct LinkedUnit;

namespace coro_module {

inline constexpr int kMutant = NIFT_CORO_MODULE_MUTANT;

// What code may do when it runs.
struct Effects {
    std::vector<std::size_t> refs;  // functions it may call or pass on, by name
    bool spawn = false;        // may start a task: thread(), an async call, an async lambda
    bool state = false;        // may read or write a module's top-level binding
    bool by_value = false;     // calls through a value (the target is not known by name)
    void merge(const Effects& o) {
        spawn = spawn || o.spawn;
        state = state || o.state;
        by_value = by_value || o.by_value;
    }
};

// Task: code that runs. Spawner: a task's arguments, evaluated by the code
// that starts it; only the values passed into the task (function names,
// lambda bodies) can run in it. Init: a module's top-level statements
// (function and lambda bodies run only when called).
enum class Mode { Task, Spawner, Init };

class Analysis {
public:
    struct Item { NodeId id; Mode mode; };
    Analysis(const Ast& ast, const UnitPlan& plan, const UnitKinds& kinds, const LinkedUnit& link);

    Effects walk(NodeId start, Mode mode) const;
    // One node's own effects; its children go on `work`.
    void visit(NodeId id, Mode mode, Effects& e, std::vector<Item>& work) const;
    // Everything the code may do, through every function it may reach.
    Effects closure(const Effects& start) const;
    // Functions code with these refs may reach by name.
    std::vector<bool> reached(const std::vector<std::size_t>& start) const;

    std::size_t module_of(std::uint32_t offset) const;
    bool function(std::string_view t, std::size_t& f) const;
    bool is_async(std::size_t f) const;
    bool thread_builtin(std::string_view t) const;
    bool constructor(std::string_view t) const { return ctors_.count(t) != 0; }
    std::size_t functions() const { return fn_effects_.size(); }
    bool value_spawn() const { return value_spawn_; }      // some function value or lambda may start a task
    bool value_state() const { return value_state_; }      // some function value or lambda may touch module state
    bool module_handles() const { return module_handles_; }  // a module binding may hold a handle
    // A named async function used as a value (stored, passed, aliased): a
    // call through a value may then call it (review 2 B1). Any such use, and
    // one of an async function defined in an imported module.
    bool value_async() const { return value_async_; }
    bool value_async_module() const { return value_async_module_; }
    // `id` is a read of a named async function as a value: the value is the
    // task's code wherever it is called (a task root, like an async lambda).
    bool async_value(NodeId id, std::size_t& f) const;

    // An argument that may be a function: a lambda, a function's name, or
    // a value whose kinds include a function.
    bool function_argument(NodeId arg) const;

private:
    void call(const Node& n, Mode mode, Effects& e, std::vector<Item>& work) const;
    const Effects& closure_of(std::size_t f) const;
    void values();
    void handle(NodeId value);
    std::string_view name(NodeId id) const { return ast_.text(ast_.at(id)); }

    const Ast& ast_;
    const UnitPlan& plan_;
    const UnitKinds& kinds_;
    const LinkedUnit& link_;
    std::unordered_set<std::string_view> state_names_;  // top-level bindings of imported modules
    std::unordered_map<std::string_view, std::vector<std::size_t>> methods_;
    std::unordered_map<std::string_view, std::size_t> ctors_;
    std::vector<Effects> fn_effects_;  // each function's own body
    mutable std::vector<Effects> closure_;
    mutable std::vector<bool> closed_;
    bool value_spawn_ = false;
    bool value_state_ = false;
    bool module_handles_ = false;
    bool value_async_ = false;
    bool value_async_module_ = false;
    std::unordered_set<NodeId> async_values_;  // reads of named async functions as values
};

}  // namespace coro_module
}  // namespace nift::rt
