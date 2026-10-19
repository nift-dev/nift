#include "rt/script_runner.h"

#include "rt/compiler.h"
#include "rt/parser.h"
#include "rt/unit_check.h"
#include "rt/vm.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

// rt-lint: cold-file process-level setup and reporting, once per script

namespace nift::rt {

namespace {

using Clock = std::chrono::steady_clock;

double micros(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double, std::micro>(to - from).count();
}

struct Stats {
    std::string outcome;  // vm | vm-error | fallback | require-error | internal-error | compiled
    std::string reason;
    std::size_t line = 0, column = 0;
    double parse_us = 0, compile_us = 0, execute_us = 0;
    std::size_t protos = 0, instructions = 0, source_bytes = 0;
    bool beyond_legacy_depth = false;
    int executions = 0;         // times the unit started executing on the VM (0 or 1)
    int legacy_executions = 0;  // 1 when the unit was handed to legacy (before anything executed)
    // Evidence for the D-018 difference policies (rt/vm.h).
    std::uint64_t large_numbers = 0, large_in_loop_path = 0;
    bool error_in_loop_path = false;
    std::vector<std::uint64_t> large_lines, derived_lines;
    bool evidence_truncated = false;
    std::string path;
    std::string code;        // vm-error: the diagnostic code when the stop has one (CP15)
    bool catchable = false;  // vm-error: legacy raises it as a catchable Error
};

std::string json_escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) < 0x20) { out += ' '; continue; }
        out += c;
    }
    return out;
}

void report(const Stats& s) {
    const char* target = std::getenv("NIFT_VM_STATS");
    if (!target || !*target || std::string(target) == "0") return;
    std::ostringstream line;
    line << "{\"nift_vm\":1,\"outcome\":\"" << s.outcome << "\",\"reason\":\"" << json_escape(s.reason)
         << "\",\"line\":" << s.line << ",\"column\":" << s.column << ",\"parse_us\":" << s.parse_us
         << ",\"compile_us\":" << s.compile_us << ",\"execute_us\":" << s.execute_us << ",\"protos\":" << s.protos
         << ",\"instructions\":" << s.instructions << ",\"source_bytes\":" << s.source_bytes
         << ",\"beyond_legacy_depth\":" << (s.beyond_legacy_depth ? "true" : "false")
         << ",\"vm_executions\":" << s.executions << ",\"legacy_executions\":" << s.legacy_executions
         << ",\"large_numbers\":" << s.large_numbers << ",\"large_in_loop_path\":" << s.large_in_loop_path
         << ",\"error_in_loop_path\":" << (s.error_in_loop_path ? "true" : "false")
         << ",\"code\":\"" << json_escape(s.code) << "\",\"catchable\":" << (s.catchable ? "true" : "false");
    const auto list = [&line](const char* name, const std::vector<std::uint64_t>& values) {
        line << ",\"" << name << "\":[";
        for (std::size_t k = 0; k < values.size(); ++k) line << (k ? "," : "") << values[k];
        line << "]";
    };
    list("large_lines", s.large_lines);
    list("derived_lines", s.derived_lines);
    line << ",\"evidence_truncated\":" << (s.evidence_truncated ? "true" : "false") << ",\"path\":\""
         << json_escape(s.path) << "\"}\n";
    if (std::string(target) == "1") {
        std::cerr << line.str() << std::flush;
    } else {
        std::ofstream file(target, std::ios::app);
        file << line.str();
    }
}

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
    if (mode == VmMode::Classify) {
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
    stats.outcome = "require-error";
    report(stats);
    error_line(path, stats, "NIFT_VM=require: not compiled for the VM (" + stats.reason + ")");
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

// Test-only knobs: NIFT_VM_HEAP_BUDGET=<bytes> lowers the memory budget
// (review round 2 R5 tests); NIFT_VM_FLUSH=<bytes> the streaming threshold.
bool size_from_environment(const char* name, std::size_t& out) {
    const char* value = std::getenv(name);
    if (!value || !*value) return false;
    char* end = nullptr;
    const unsigned long long limit = std::strtoull(value, &end, 10);
    if (!end || *end != '\0' || limit == 0) return false;
    out = static_cast<std::size_t>(limit);
    return true;
}

void limits_from_environment(VmOptions& options) {
    size_from_environment("NIFT_VM_HEAP_BUDGET", options.heap_budget);
    size_from_environment("NIFT_VM_FLUSH", options.flush_threshold);
}

bool run_unit(const std::string& source, const std::filesystem::path& source_path, VmMode mode, Stats& stats,
              int& exit_code);

} // namespace

VmMode vm_mode_from_environment() {
    const char* value = std::getenv("NIFT_VM");
    if (!value || !*value || std::string(value) == "0") return VmMode::Off;
    if (std::string(value) == "require") return VmMode::Require;
    if (std::string(value) == "classify") return VmMode::Classify;
    return VmMode::On;
}

bool run_script_on_vm(const std::string& source, const std::filesystem::path& source_path, int& exit_code) {
    const VmMode mode = vm_mode_from_environment();
    if (mode == VmMode::Off) return false;
    Stats stats;
    stats.source_bytes = source.size();
    stats.path = source_path.generic_string();
    try {
        return run_unit(source, source_path, mode, stats, exit_code);
    } catch (const std::bad_alloc&) {
        internal_failure(stats, source_path, "out of memory (std::bad_alloc)", exit_code);
    } catch (const std::exception& error) {
        internal_failure(stats, source_path, std::string("exception: ") + error.what(), exit_code);
    } catch (...) {
        internal_failure(stats, source_path, "unknown exception", exit_code);
    }
    return true;
}

namespace {

bool run_unit(const std::string& source, const std::filesystem::path& source_path, VmMode mode, Stats& stats,
              int& exit_code) {
    const auto t0 = Clock::now();
    Ast ast(source);
    const ParseResult parsed = parse_script(ast);
    const auto t1 = Clock::now();
    stats.parse_us = micros(t0, t1);
    if (!parsed.ok) {
        stats.reason = "parse-error";  // legacy reports the identical diagnostic (CP10)
        locate_into(stats, source, parsed.error.offset);
        return not_compiled(stats, mode, source_path, exit_code);
    }
    const NodeId opaque = first_opaque_node(ast, parsed.root);
    if (opaque != kNoNode) {
        stats.reason = std::string("opaque:") + opaque_reason_name(ast.at(opaque).reason);
        locate_into(stats, source, ast.at(opaque).begin);
        return not_compiled(stats, mode, source_path, exit_code);
    }
    CompileResult compiled = compile_unit(ast, parsed.root);
    const auto t2 = Clock::now();
    stats.compile_us = micros(t1, t2);
    if (!compiled.program) {
        stats.reason = compiled.reason;
        locate_into(stats, source, compiled.offset);
        return not_compiled(stats, mode, source_path, exit_code);
    }
    stats.protos = compiled.program->protos.size();
    for (const auto& proto : compiled.program->protos) stats.instructions += proto->code.size();
    if (mode == VmMode::Classify) {
        stats.outcome = "compiled";
        report(stats);
        exit_code = 0;
        return true;
    }
    const char* dump = std::getenv("NIFT_VM_DUMP");
    if (dump && std::string(dump) == "1") std::cerr << disassemble(*compiled.program);

    VmOptions options;
    limits_from_environment(options);
    options.flush = &write_stdout;  // output streams: nothing is ever replayed (CP12, D-013)
    // From here on the unit runs on the VM and only on the VM: whatever
    // happens, the legacy engine never runs it (engine selection is final).
    stats.executions = 1;
    Vm vm(*compiled.program, options);
    std::string output;
    const VmResult result = vm.run(output);
    stats.execute_us = micros(t2, Clock::now());
    stats.beyond_legacy_depth = result.beyond_legacy_depth;
    stats.large_numbers = result.large_numbers;
    stats.large_in_loop_path = result.large_in_loop_path;
    stats.error_in_loop_path = result.error_in_loop_path;
    stats.large_lines = result.large_lines;
    stats.derived_lines = result.derived_lines;
    stats.evidence_truncated = result.evidence_truncated;
    write_stdout(output, nullptr);
    if (result.reason == StopReason::None) {
        if (result.returned && !result.return_text.empty()) std::cout << result.return_text << '\n';
        std::cout.flush();
        stats.outcome = "vm";
        report(stats);
        exit_code = 0;
        return true;
    }
    locate_into(stats, source, result.offset);
    if (result.reason == StopReason::Error) {
        // A program error, in legacy's format, at the failing expression.
        stats.outcome = "vm-error";
        stats.reason = result.message;
        stats.code = result.code;
        stats.catchable = result.catchable;
        report(stats);
        error_line(source_path, stats, result.message);
        exit_code = 1;
        return true;
    }
    internal_failure(stats, source_path, result.message, exit_code);
    return true;
}

} // namespace

} // namespace nift::rt
