#include "rt/session.h"
#include "rt/session_text.h"
#include "rt/session_shape.h"
#include "rt/session_inventory.h"
#include "rt/cell.h"
#include "rt/aggregate.h"
#include "rt/compiler_eval.h"
#include "rt/script_runner.h"
#include "rt/script_runner_stats.h"
#include "rt/heap.h"
#include "rt/infer.h"
#include "rt/parser.h"
#include "rt/unit_check.h"
#include <algorithm>
#include <stdexcept>
#include <chrono>

// rt-lint: cold-file session compilation, code linking and publication
namespace nift::rt {
VmSession::VmSession(VmOptions options) {
    auto main = std::make_unique<Proto>();
    main->code.push_back(Instr{OpCode::Halt, 0, 0, 0, 0});
    main->offsets.push_back(0); main->notes.push_back(0);
    bootstrap_.protos.push_back(std::move(main));
    options.source_map = nullptr;  // each owned session Proto supplies its immutable source map
    options.session_globals = &globals_;
    options.return_value = &last_value_;
    vm_ = std::make_unique<Vm>(bootstrap_, options);
}
VmSession::~VmSession() { close(); }
void VmSession::close() {
    if (closed_) return;
    if (active_) { close_pending_ = true; return; }
    close_pending_ = false;
    // Destroy executable values before the immutable code they refer to.
    vm_.reset(); last_value_ = Value(); globals_.clear(); namespace_.symbols.clear(); namespace_.structures.clear();
    linked_.clear(); units_.clear(); vacant_.clear(); closed_ = true;
}
std::size_t VmSession::retained_code_units() const {
    return std::count_if(units_.begin(), units_.end(), [](const auto& unit) { return !unit.owner.expired(); });
}
void VmSession::retire_code() {
    units_.erase(std::remove_if(units_.begin(), units_.end(), [&](const auto& unit) {
        if (!unit.owner.expired()) return false;
        vm_->retire_session_slots(unit.slots);
        for (const auto slot : unit.slots) { linked_[slot] = nullptr; vacant_.push_back(slot); }
        return true;
    }), units_.end());
}
bool VmSession::contains(const std::string& name) const {
    return namespace_.structures.count(name) || std::any_of(namespace_.symbols.begin(), namespace_.symbols.end(),
                       [&](const auto& symbol) { return symbol.defined && symbol.name == name; });
}
Value VmSession::value(const std::string& name) const {
    for (const auto& symbol : namespace_.symbols)
        if (symbol.defined && symbol.name == name) return cell_load(globals_.at(symbol.slot));
    return Value();
}
std::size_t VmSession::binding_count() const {
    return namespace_.structures.size() + std::count_if(namespace_.symbols.begin(), namespace_.symbols.end(),
                         [](const auto& symbol) { return symbol.defined; });
}
void VmSession::refresh(SessionCompileContext& pending) {
    auto& symbols = pending.symbols;
    for (auto& symbol : symbols) {
        symbol.defined = is_cell(globals_[symbol.slot]);
        if (!symbol.defined) continue;  // reserve source-referenced slots, without publishing a live binding
        const auto* cell = cell_of(globals_[symbol.slot]);
        const auto& v = cell->value;
        // These are the published compiler's whole-unit bounds. Widening a
        // later assignment beyond them refuses before execution; an observed
        // current value never silently narrows old code's assumptions.
        symbol.kinds = symbol.contract;
        if (!symbol.struct_name.empty() && !symbol.struct_definition) {
            const auto schema = pending.structures.find(symbol.struct_name);
            if (schema != pending.structures.end()) symbol.struct_definition = schema->second;
            else {
                const auto imported = pending.schema_templates.find(symbol.struct_name);
                if (imported != pending.schema_templates.end()) symbol.struct_definition = imported->second;
            }
        }
        symbol.callable_shape = session_callable_shape(v);
        symbol.untracked_callables = symbol.callable_shape && !symbol.callable_shape->complete;
        symbol.callable = v.is(Kind::Function);
        if (symbol.callable) {
            const auto* proto = static_cast<const Proto*>(v.template heap_as<FunctionObject>()->code);
            symbol.arity = proto->nparams;
            symbol.returns = proto->session_returns;
            symbol.definition = proto->session_definition;
            symbol.identity = v.template heap_as<FunctionObject>()->session_identity;
            symbol.params = proto->session_params; symbol.param_types = proto->session_param_types;
        }
    }
}
std::vector<std::string> VmSession::namespace_names() const {
    std::vector<std::string> names;
    for (const auto& symbol : namespace_.symbols) if (symbol.defined) names.push_back(symbol.name);
    for (const auto& schema : namespace_.structures) names.push_back(schema.first);
    return names;
}
std::vector<std::string> VmSession::callable_names() const {
    std::vector<std::string> names;
    for (const auto& symbol : namespace_.symbols) if (symbol.defined && symbol.callable) names.push_back(symbol.name);
    for (const auto& schema : namespace_.structures) names.push_back(schema.first);
    return names;
}
SessionResult VmSession::execute(const std::string& source, std::string path, std::string& output, bool inspect_tail) {
    return execute_safe(source, path, output, false, inspect_tail);
}
SessionResult VmSession::evaluate(const std::string& source, std::string path, std::string& output) {
    return execute_safe(source, path, output, true, false);
}
SessionResult VmSession::execute_safe(const std::string& source, const std::string& path, std::string& output,
                                      bool expression, bool inspect_tail) {
    if (active_) {
        SessionResult result;
        result.execution.reason = StopReason::Internal;
        result.execution.message = "session is already executing";
        detail::Stats stats; stats.path = path; stats.outcome = "internal-error";
        stats.reason = result.execution.message; detail::report(stats);
        return result;  // never reset, retire or destroy a running VM
    }
    const auto before = executions_;
    active_ = true;
    try {
        auto result = execute_unit(source, path, output, expression, inspect_tail);
        active_ = false;
        if (close_pending_) close();
        return result;
    }
    catch (const std::exception& error) {
        active_ = false;
        SessionResult result;
        result.vm_executions = executions_ != before;
        result.admitted = result.vm_executions != 0;
        result.execution.reason = StopReason::Internal;
        result.execution.message = std::string("NIFT_VM internal failure: ") + error.what();
        detail::Stats stats; stats.path = path; stats.outcome = "internal-error";
        stats.reason = result.execution.message; stats.executions = static_cast<int>(result.vm_executions);
        close(); detail::report(stats); return result;
    } catch (...) {
        active_ = false; close(); throw;
    }
}
SessionResult VmSession::execute_unit(const std::string& source, std::string path, std::string& output,
                                      bool expression, bool inspect_tail) {
    if (closed_) throw std::logic_error("session is closed");
    retire_code();
    const auto begin = std::chrono::steady_clock::now();
    SessionResult result;
    const auto text = session_text(source, path, namespace_);
    const auto source_map = std::make_shared<const SourceMap>(SourceMap::pieces(text.pieces));
    Ast ast(text.text);
    ParseResult parsed;
    if (expression) {
        const auto prefix = parse_script_range(ast, 0, text.unit_begin);
        const auto value = parse_eval_expression(ast, text.unit_begin, static_cast<std::uint32_t>(text.text.size()));
        normalize_eval_callbacks(ast, value);
        if (ast.at(value).kind == NodeKind::Call && ast.at(ast.at(value).a).kind == NodeKind::Member) {
            const auto name = ast.text(ast.at(ast.at(ast.at(value).a).b));
            result.presentation = name == "stringify" || name == "prettify" || name == "highlight";
        }
        parsed.root = make_eval_program(ast, value); parsed.ok = prefix.ok;
        if (prefix.ok && ast.at(prefix.root).count) {
            std::vector<NodeId> statements;
            for (std::uint32_t k = 0; k < ast.at(prefix.root).count; ++k)
                statements.push_back(ast.list(ast.at(prefix.root))[k]);
            const auto& body = ast.at(parsed.root);
            for (std::uint32_t k = 0; k < body.count; ++k) statements.push_back(ast.list(body)[k]);
            ast.set_list(parsed.root, statements);
        }
    } else parsed = parse_script(ast);
    detail::Stats stats;
    stats.path = path; stats.source_bytes = source.size();
    SessionCompileContext pending = namespace_;
    pending.inspect_tail = inspect_tail;
    pending.declarations.clear();
    for (const auto& symbol : pending.symbols)
        pending.declarations.emplace(symbol.name, ast.add(NodeKind::Ident, 0, 0));
    prepare_session_namespace(pending, text);
    pending.imported_functions.clear();
    for (const auto& imported : text.imports) pending.imported_functions.insert(imported.first);
    prepare_session_templates(ast, parsed.root, parsed.ok, text, namespace_, pending);
    if (parsed.ok) normalize_session_callbacks(ast, parsed.root, pending);
    CompileLimits limits; limits.session = &pending;
    const auto parse_done = std::chrono::steady_clock::now();
    stats.parse_us = std::chrono::duration<double, std::micro>(parse_done - begin).count();
    const auto mode = vm_mode_from_environment();
    if (mode == VmMode::ClassifyAll)
        stats.inventory_json = session_inventory_json(ast, parsed.root, parsed.ok, pending, *source_map, expression);
    CompileResult compiled;
    if (parsed.ok && first_opaque_node(ast, parsed.root) == kNoNode)
        compiled = expression ? compile_eval_program(ast, parsed.root, limits) : compile_unit(ast, parsed.root, limits);
    else compiled.reason = parsed.ok ? "session:opaque" : "session:parse";
    stats.compile_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - parse_done).count();
    if (!compiled.program) {
#if defined(NIFT_SESSION_MUTANT) && NIFT_SESSION_MUTANT == 1
        namespace_ = pending;  // M1: leak partial compiler publication on refusal
#endif
        ++refusals_; result.refusal = compiled.reason; result.offset = compiled.offset;
        const auto at = source_map->at(compiled.offset);
        stats.outcome = "require-error"; stats.reason = result.refusal; stats.path = at.path;
        stats.line = at.line; stats.column = at.column;
        detail::report(stats); return result;
    }
    if (mode == VmMode::Classify || mode == VmMode::ClassifyAll) {
        stats.outcome = "compiled"; detail::report(stats); return result;
    }
    auto owned = std::shared_ptr<Program>(std::move(compiled.program));
    auto& program = *owned;
    session_definitions(ast, parsed.root, text, path, pending, program, executions_);
    session_structures(ast, parsed.root, text, path, pending);
    auto linked = linked_;
    auto vacant = vacant_;
    std::vector<std::uint16_t> slots(program.protos.size());
    for (std::size_t k = 1; k < program.protos.size(); ++k) {
        if (!vacant.empty()) { slots[k] = vacant.back(); vacant.pop_back(); }
        else {
            if (linked.size() >= 65535) { ++refusals_; result.refusal = "limit:session-protos"; return result; }
            slots[k] = static_cast<std::uint16_t>(linked.size()); linked.push_back(nullptr);
        }
        linked[slots[k]] = program.protos[k].get();
    }
    const auto targets = activation_targets(program);
    for (auto& proto : program.protos) {
        proto->session_source = source_map;
        proto->session_owner = owned;
        for (auto& instruction : proto->code)
            if (instruction.op == OpCode::Call || instruction.op == OpCode::LoadFunction ||
                instruction.op == OpCode::Closure) instruction.b = slots.at(instruction.b);
    }
    // Allocate all publication containers before admitting execution. Refusal
    // above has neither published bindings nor touched the live cell graph.
    units_.reserve(units_.size() + 1);
    CodeUnit record{owned, std::vector<std::uint16_t>(slots.begin() + 1, slots.end())};
    globals_.resize(pending.slots);
    units_.push_back(std::move(record));
    linked_.swap(linked); vacant_.swap(vacant);
    last_value_ = Value();
    result.admitted = true; result.vm_executions = 1; ++executions_;
#if defined(NIFT_SESSION_MUTANT) && NIFT_SESSION_MUTANT == 4
    std::vector<Value> before;
    for (const auto& cell : globals_) before.push_back(is_cell(cell) ? cell_load(cell) : Value());
#endif
    activate_code(targets, owned);
    retire_code();  // old code may have lost its final owner during activation
    const auto execute_begin = std::chrono::steady_clock::now();
    result.execution = vm_->run_session_unit(*owned, linked_, output);
    if (result.execution.reason != StopReason::None) result.execution.statement_text.clear();
    stats.execute_us = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - execute_begin).count();
#if defined(NIFT_SESSION_MUTANT) && NIFT_SESSION_MUTANT == 4
    if (result.execution.reason == StopReason::Error)
        for (std::size_t k = 0; k < before.size(); ++k)
            if (is_cell(globals_[k])) cell_put(globals_[k], before[k]);  // M4: roll back runtime effects
#endif
    refresh(pending);
    namespace_ = std::move(pending);
    namespace_.imported_functions.clear();
    namespace_.declarations.clear();
    namespace_.lambda_templates.clear(); namespace_.capture_seeds.clear();
    namespace_.callable_templates.clear(); namespace_.capture_callable_seeds.clear();
    namespace_.capture_contracts.clear(); namespace_.template_definitions.clear(); namespace_.const_captures.clear();
    namespace_.source_owner.reset(); namespace_.schema_templates.clear();
    namespace_.named_templates.clear(); namespace_.phased_functions.clear();
#if defined(NIFT_SESSION_MUTANT) && NIFT_SESSION_MUTANT == 2
    if (executions_ == 1) for (const auto& symbol : namespace_.symbols)
        if (symbol.name == "x") cell_put(globals_[symbol.slot], Value::integer(0));  // M2: lose retained state
#endif
    result.value = last_value_;
    stats.executions = 1;
    stats.outcome = result.execution.reason == StopReason::None ? "vm" :
        result.execution.reason == StopReason::Error ? "vm-error" : "internal-error";
    stats.reason = result.execution.message;
    stats.code = result.execution.code; stats.catchable = result.execution.catchable;
    const auto at = result.execution.reason == StopReason::None ? SourceOrigin{"", 1, 1} :
        result.execution.error_origin ? SourceOrigin{result.execution.origin_source,
        result.execution.origin_line, result.execution.origin_column} :
        !result.execution.report_origin.path.empty() ? result.execution.report_origin :
        source_map->at(result.execution.offset);
    stats.line = at.line; stats.column = at.column;
    stats.error_origin = result.execution.error_origin;
    stats.protos = program.protos.size();
    for (const auto& proto : program.protos) stats.instructions += proto->code.size();
    detail::report(stats);
    if (result.execution.reason == StopReason::Internal) close();
    return result;
}
} // namespace nift::rt
