#include "rt/script_runner.h"
#include "rt/script_runner_stats.h"
#include "rt/script_runner_options.h"
#include "rt/session_invocation.h"

#include "HostTrace.h"
#include "rt/compiler_eval.h"
#include "rt/eval_result.h"
#include "rt/compiler_coro_admit.h"
#include "rt/heap_account.h"
#include "rt/legacy_bridge.h"
#include "rt/module_host_op.h"
#include "rt/module_link.h"
#include "rt/native.h"
#include "rt/natives/builtin.h"
#include "rt/opcodes.h"
#include "rt/parser.h"
#include "rt/refusal_text.h"
#include "rt/source_map.h"
#include "rt/unit_check.h"
#include "rt/vm.h"
#include "rt/vm_coro_run.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>

// rt-lint: cold-file process-level setup and reporting, once per script

namespace nift::rt {

namespace {

using Clock = std::chrono::steady_clock;

double micros(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double, std::micro>(to - from).count();
}

using detail::Stats;
using detail::json_escape;
using detail::report;

void write_stdout(std::string& output, void*) {
    std::cout.write(output.data(), static_cast<std::streamsize>(output.size()));
    output.clear();
}

// First Opaque node in pre-order, or kNoNode.
void locate_into(Stats& stats, const std::string& source, std::uint32_t offset) {
    const Location where = locate(source, offset);
    stats.line = where.line;
    stats.column = where.column;
}

void error_line(const std::filesystem::path& path, const Stats& stats, const std::string& message) {
    std::cout.flush();
    std::cerr << "error: " << path.generic_string() << ':' << stats.line << ':' << stats.column << ": " << message
              << std::endl;
}

// The unit cannot run on the VM. Under NIFT_VM=require that is an error.
bool not_compiled(Stats& stats, VmMode mode, const std::filesystem::path& path, int& exit_code) {
    if (mode == VmMode::Classify || mode == VmMode::ClassifyAll) {
        stats.outcome = "fallback";
        report(stats);
        exit_code = 0;
        return true;
    }
    if (mode != VmMode::Require) {
        stats.outcome = "fallback";
        stats.legacy_executions = 1;  // the caller runs the unit on legacy, which nothing has touched
        report(stats);
        return false;
    }
    // VM required (the default, or NIFT_VM=require): refused before anything
    // ran, with the full user-facing diagnostic (rt/refusal_text.h).
    stats.outcome = "require-error";
    report(stats);
    std::cout.flush();
    const std::string where = stats.positioned
        ? path.generic_string() + ':' + std::to_string(stats.line) + ':' + std::to_string(stats.column)
        : path.generic_string() + " (the VM gave no exact position)";
    std::cerr << vm_refusal_diagnostic(where, stats.reason, path.generic_string()) << std::flush;
    exit_code = 1;
    return true;
}

// An unexpected VM failure (internal error, std::bad_alloc, any exception) is
// reported as such and never disguised as a legacy run (owner directive).
void internal_failure(Stats& stats, const std::filesystem::path& path, const std::string& what, int& exit_code) {
    stats.outcome = "internal-error";
    stats.reason = what;
    report(stats);
    std::cout.flush();
    std::cerr << "error: " << path.generic_string() << ": NIFT_VM internal failure: " << what
              << " (the script was not run on the legacy engine)" << std::endl;
    exit_code = 70;
}

struct EvalResult { nift::RuntimeValue& value; std::string& error; };

bool run_unit(const std::string& source, const std::filesystem::path& source_path, VmMode mode, Stats& stats,
              int& exit_code, EvalResult* eval = nullptr,
              std::vector<InvocationBinding>* invocation = nullptr);

SourceMap source_map_for(const LinkedUnit& link, const std::filesystem::path& path, const std::string& source) {
    return link.linked ? SourceMap::linked(link, path.generic_string(), source)
                       : SourceMap::single(path.generic_string(), source);
}

// The accepted NIFT_VM values (owner ruling): unset, require, 0, 1, classify,
// classify-all. Anything else, the empty value included, is invalid.
bool parse_vm_mode(const char* value, VmMode& mode) {
    if (!value) { mode = VmMode::Require; return true; }  // unset: the VM is required
    const std::string text(value);
    if (text == "require") mode = VmMode::Require;
    else if (text == "0") mode = VmMode::Off;
    else if (text == "1") mode = VmMode::On;
    else if (text == "classify") mode = VmMode::Classify;
    else if (text == "classify-all") mode = VmMode::ClassifyAll;
    else return false;
    return true;
}
} // namespace

VmMode vm_mode_from_environment() {
    // VM-default policy: unset requires the VM (no legacy fallback); only an
    // explicit NIFT_VM=0 selects the legacy interpreter. The CLI rejects an
    // invalid value before any scripting entry runs (vm_mode_configuration_error);
    // anywhere else an invalid value fails closed: the VM is required.
    VmMode mode = VmMode::Require;
    return parse_vm_mode(std::getenv("NIFT_VM"), mode) ? mode : VmMode::Require;
}

std::string vm_mode_configuration_error() {
    const char* value = std::getenv("NIFT_VM");
    VmMode mode = VmMode::Require;
    if (parse_vm_mode(value, mode)) return std::string();
    return std::string("invalid NIFT_VM value '") + value + "'" + (*value ? "" : " (empty)") +
           ": accepted values are unset (the VM is required; the default), require, 0 (the legacy "
           "interpreter), 1 (the VM with legacy fallback), classify, classify-all; nothing was run";
}

bool run_script_on_vm(const std::string& source, const std::filesystem::path& source_path,
                      const std::string& command, const std::vector<std::string>& arguments, int& exit_code) {
    const VmMode mode = vm_mode_from_environment();
    if (mode == VmMode::Off) return false;
    Stats stats;
    stats.source_bytes = source.size();
    stats.path = source_path.generic_string();
    try {
        std::vector<InvocationBinding> invocation = invocation_bindings(command, arguments);
#if defined(NIFT_ARGSCMD_MUTANT) && NIFT_ARGSCMD_MUTANT == 1  // test-only: no script bindings (args_cmd/mutants.sh)
        return run_unit(source, source_path, mode, stats, exit_code, nullptr, nullptr);
#endif
        return run_unit(source, source_path, mode, stats, exit_code, nullptr, &invocation);
    } catch (const std::bad_alloc&) {
        internal_failure(stats, source_path, "out of memory (std::bad_alloc)", exit_code);
    } catch (const std::exception& error) {
        internal_failure(stats, source_path, std::string("exception: ") + error.what(), exit_code);
    } catch (...) {
        internal_failure(stats, source_path, "unknown exception", exit_code);
    }
    return true;
}

bool eval_expression_on_vm(const std::string& source, nift::RuntimeValue& value,
                           std::string& error, int& exit_code) {
    const VmMode mode = vm_mode_from_environment();
    if (mode == VmMode::Off) return false;
    Stats stats;
    stats.source_bytes = source.size();
    stats.path = "<eval>";
    EvalResult result{value, error};
    try {
        return run_unit(source, "<eval>", mode, stats, exit_code, &result);
    } catch (const std::bad_alloc&) {
        internal_failure(stats, "<eval>", "out of memory (std::bad_alloc)", exit_code);
    } catch (const std::exception& exception) {
        internal_failure(stats, "<eval>", std::string("exception: ") + exception.what(), exit_code);
    } catch (...) {
        internal_failure(stats, "<eval>", "unknown exception", exit_code);
    }
    return true;
}

namespace {

bool finish_run(const std::string& source, const std::filesystem::path& source_path,
                const LinkedUnit& link, const VmResult& result, const Value& returned,
                Stats& stats, EvalResult* eval, int& exit_code) {
    std::filesystem::path report_path = source_path;
    const auto locate_linked = [&](std::uint32_t offset) {
        if (!link.linked) return locate_into(stats, source, offset);
        const LinkedUnit::Location at = link.locate(offset);
        report_path = at.module == 0 ? source_path : link.graph.modules[at.module]->path;
        locate_into(stats, at.module == 0 ? source : link.graph.modules[at.module]->source,
                    static_cast<std::uint32_t>(at.offset));
    };
    stats.beyond_legacy_depth = result.beyond_legacy_depth;
    stats.large_numbers = result.large_numbers;
    stats.large_in_loop_path = result.large_in_loop_path;
    stats.error_in_loop_path = result.error_in_loop_path;
    stats.large_lines = result.large_lines;
    stats.derived_lines = result.derived_lines;
    stats.evidence_truncated = result.evidence_truncated;
    stats.heap_peak = result.heap_peak;
    stats.heap_live = result.heap_live;
    stats.heap_bytes = live_heap_bytes();
    if (result.reason == StopReason::None) {
        if (eval && !eval_result_to_legacy(returned, eval->value, eval->error)) {
            stats.outcome = "vm-error";
            stats.reason = eval->error;
            report(stats);
            exit_code = 2;
            return true;
        }
        if (!eval && result.returned && !result.return_text.empty()) std::cout << result.return_text << '\n';
        std::cout.flush();
        stats.outcome = "vm";
        report(stats);
        exit_code = 0;
        return true;
    }
    if (result.error_origin) {
        report_path = result.origin_source;
        stats.line = result.origin_line;
        stats.column = result.origin_column;
    } else if(!result.diagnostic_frames.empty()&&!result.diagnostic_frames.front().site.source.empty()) {
        const auto& display=result.diagnostic_frames.front().site;
        report_path=display.source;stats.line=display.line;stats.column=display.column;
    } else {
        locate_linked(result.offset);
    }
    if (result.reason == StopReason::Error) {
        // A program error, in legacy's format, at the failing expression;
        // while a module initialises, legacy prefixes `import: ` per level.
        std::string message = result.message;
        if (link.linked)
            for (int k = link.depth[link.locate(result.main_offset).module]; k > 0; --k) message = "import: " + message;
        stats.outcome = "vm-error";
        stats.reason = message;
        stats.code = result.code;
        stats.catchable = result.catchable;
        stats.error_origin = result.error_origin;
        report(stats);
        if (eval) eval->error = message;
        else error_line(report_path, stats, message);
        exit_code = eval ? 2 : 1;
        return true;
    }
    internal_failure(stats, source_path, result.message, exit_code);
    return true;
}

bool run_unit(const std::string& source, const std::filesystem::path& source_path, VmMode mode, Stats& stats,
              int& exit_code, EvalResult* eval, std::vector<InvocationBinding>* invocation) {
    if (mode == VmMode::ClassifyAll) add_inventory(source, source_path, stats, eval != nullptr, invocation);
    const auto t0 = Clock::now();
    Ast ast(source);
    ParseResult parsed;
    if (eval) parsed.root = parse_eval_expression(ast, 0, static_cast<std::uint32_t>(source.size()));
    else parsed = parse_script(ast);
    if (eval) normalize_eval_callbacks(ast, parsed.root);
    const auto t1 = Clock::now();
    stats.parse_us = micros(t0, t1);
    if (!parsed.ok) {
        stats.reason = "parse-error";  // legacy reports the identical diagnostic (CP10)
        locate_into(stats, source, parsed.error.offset); stats.positioned = true;
        return not_compiled(stats, mode, source_path, exit_code);
    }
    const NodeId opaque = first_opaque_node(ast, parsed.root);
    if (opaque != kNoNode) {
        stats.reason = std::string("opaque:") + opaque_reason_name(ast.at(opaque).reason);
        locate_into(stats, source, ast.at(opaque).begin); stats.positioned = true;
        return not_compiled(stats, mode, source_path, exit_code);
    }
    // CP12b modules: a unit with imports compiles as its linked text
    // (rt/module_link.h); positions map back to their module (unrecorded: the entry's).
    const LinkedUnit link =
        eval ? LinkedUnit() : link_unit(ast, parsed.root, source, source_path, invocation_names(invocation));
    std::filesystem::path report_path = source_path;
    const auto locate_linked = [&](std::uint32_t offset, bool positioned) {
        stats.positioned = positioned;
        if (!link.linked || !positioned) return locate_into(stats, source, link.linked ? 0 : offset);
        const LinkedUnit::Location at = link.locate(offset);
        report_path = at.module == 0 ? source_path : link.graph.modules[at.module]->path;
        locate_into(stats, at.module == 0 ? source : link.graph.modules[at.module]->source,
                    static_cast<std::uint32_t>(at.offset));
    };
    if (link.linked && !link.ok) {
        stats.reason = link.reason;
        report_path = link.where == 0 ? source_path : link.graph.modules[link.where]->path;
        locate_into(stats, link.where == 0 ? source : link.graph.modules[link.where]->source, link.offset);
        stats.positioned = link.positioned;  // set by the link/graph site that recorded the offset
        return not_compiled(stats, mode, report_path, exit_code);
    }
    Ast linked_ast(link.linked ? std::string_view(link.text) : std::string_view());
    NodeId unit_root = parsed.root;
    if (link.linked) {
        const ParseResult relinked = parse_script(linked_ast);
        if (!relinked.ok || first_opaque_node(linked_ast, relinked.root) != kNoNode) {
            stats.reason = "import-link";  // every module parsed alone: never expected
            locate_into(stats, source, 0); stats.positioned = false;
            return not_compiled(stats, mode, source_path, exit_code);
        }
        unit_root = relinked.root;
    }
    CompileLimits limits;
    limits.module_names = link.module_names; limits.link = &link; cooperative_switches(limits);
    limits.invocation = invocation;  // the script's `cmd`/`args` (rt/session_invocation.h)
    if (invocation) locate_invocation_shadows(*invocation, link.linked ? linked_ast : ast, unit_root,
        [&](std::uint32_t at) { return !link.linked || link.locate(at).module == 0; });
    CompileResult compiled = eval ? compile_eval_program(ast, make_eval_program(ast, parsed.root), limits)
                                  : compile_unit(link.linked ? linked_ast : ast, unit_root, limits);
    const auto t2 = Clock::now();
    stats.compile_us = micros(t1, t2);
    if (invocation) invocation->clear();  // the program holds what it uses; the rest is not live heap
    if (!compiled.program) {
        stats.reason = compiled.reason;
        locate_linked(compiled.offset, compiled.positioned);
        return not_compiled(stats, mode, report_path, exit_code);
    }
    // R1 (owner, 2026-10-21): linking reads all modules before execution.
    // A host operation before/between imports could observe different files.
    // Until imports run in place, scan every proto and refuse linked host built-ins.
#if !defined(NIFT_R1_MUTANT)  // test-only negative control (tests/runtime2/module_host_order_mutant.sh)
    if (link.linked) {
        std::string host_reason;
        std::uint32_t host_offset = 0; bool host_positioned = false;
        if (first_module_host_op(*compiled.program, host_reason, host_offset, &host_positioned)) {
            stats.reason = host_reason;
            locate_linked(host_offset, host_positioned);  // the host op's recorded instruction offset
            return not_compiled(stats, mode, report_path, exit_code);
        }
    }
#endif
    stats.protos = compiled.program->protos.size();
    for (const auto& proto : compiled.program->protos) stats.instructions += proto->code.size();
    if (mode == VmMode::Classify || mode == VmMode::ClassifyAll) {
        stats.outcome = "compiled";
        report(stats);
        exit_code = 0;
        return true;
    }
    const char* dump = std::getenv("NIFT_VM_DUMP");
    if (dump && std::string(dump) == "1") std::cerr << disassemble(*compiled.program);
    VmOptions options;
    detail::limits_from_environment(options);
    options.flush = &write_stdout;  // output streams: nothing is ever replayed (CP12, D-013)
    options.return_in_output = true;  // CP22: a top-level return prints where legacy prints it
    const SourceMap source_map = source_map_for(link, source_path, source);  // owns source positions
    options.source_map = &source_map;
    Value returned;
    if (eval) options.return_value = &returned;
    // From here on the unit runs on the VM and only on the VM: whatever
    // happens, the legacy engine never runs it (engine selection is final).
    stats.executions = 1;
    // CP12b modules: legacy traces each module read (`read-source`) when the
    // import runs; the linked unit read them before running, in the same
    // order (first read, depth first). No traced host op of the VM can run
    // before an import yet; when VM host ops are traced (CP16 wiring), these
    // must move to the import's position.
    if (link.linked)
        for (const std::filesystem::path& read : link.graph.dependencies)
            nift::trace::emit("read-source", read.lexically_normal().generic_string(), "ok");
    Vm vm(*compiled.program, options);
    std::string output;
    const VmResult result = vm.run(output);
    stats.execute_us = micros(t2, Clock::now());
    if (result.coro) stats.coro_json = coro_stats_json(result);
    write_stdout(output, nullptr);
    return finish_run(source, source_path, link, result, returned, stats, eval, exit_code);
}

} // namespace

} // namespace nift::rt
