#include "rt/script_runner.h"

#include "rt/compiler.h"
#include "rt/parser.h"
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
    std::string outcome;  // vm | fallback | replayed | require-error | internal-error
    std::string reason;
    std::size_t line = 0, column = 0;
    double parse_us = 0, compile_us = 0, execute_us = 0;
    std::size_t protos = 0, instructions = 0, source_bytes = 0;
    bool beyond_legacy_depth = false;
    std::string path;
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
         << ",\"beyond_legacy_depth\":" << (s.beyond_legacy_depth ? "true" : "false") << ",\"path\":\""
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
NodeId first_opaque(const Ast& ast, NodeId id) {
    if (id == kNoNode) return kNoNode;
    const Node& n = ast.at(id);
    if (n.kind == NodeKind::Opaque) return id;
    for (NodeId child : {n.a, n.b, n.c}) {
        const NodeId r = first_opaque(ast, child);
        if (r != kNoNode) return r;
    }
    const NodeId* list = ast.list(n);
    for (std::uint32_t k = 0; k < n.count; ++k) {
        const NodeId r = first_opaque(ast, list[k]);
        if (r != kNoNode) return r;
    }
    return kNoNode;
}

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

// Test-only knob: NIFT_VM_OUTPUT_LIMIT=<bytes> lowers the output buffer limit
// (review D7 regression tests); unset or invalid keeps the 64 MiB default.
void output_limit_from_environment(VmOptions& options) {
    const char* value = std::getenv("NIFT_VM_OUTPUT_LIMIT");
    if (!value || !*value) return;
    char* end = nullptr;
    const unsigned long long limit = std::strtoull(value, &end, 10);
    if (end && *end == '\0' && limit > 0) options.output_limit = static_cast<std::size_t>(limit);
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
    const NodeId opaque = first_opaque(ast, parsed.root);
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
    output_limit_from_environment(options);
    // Only NIFT_VM=require (never replays) may flush before completion; under
    // NIFT_VM=1 reaching the limit stops the run and legacy replays it (D7).
    if (mode == VmMode::Require) options.flush = &write_stdout;
    Vm vm(*compiled.program, options);
    std::string output;
    const VmResult result = vm.run(output);
    stats.execute_us = micros(t2, Clock::now());
    stats.beyond_legacy_depth = result.beyond_legacy_depth;
    if (result.reason == BailReason::Internal) {
        locate_into(stats, source, result.offset);
        internal_failure(stats, source_path, result.message, exit_code);
        return true;
    }
    if (result.reason == BailReason::None) {
        write_stdout(output, nullptr);
        if (result.returned && !result.return_text.empty()) std::cout << result.return_text << '\n';
        std::cout.flush();
        stats.outcome = "vm";
        report(stats);
        exit_code = 0;
        return true;
    }
    stats.reason = std::string("bail:") + bail_reason_name(result.reason);
    locate_into(stats, source, result.offset);
    if (!result.committed && mode != VmMode::Require) {
        stats.outcome = "replayed";  // nothing was written: legacy runs the unit from the start
        report(stats);
        return false;
    }
    if (result.committed && mode != VmMode::Require) {  // unreachable: only require mode flushes early
        internal_failure(stats, source_path, "output committed before a bail", exit_code);
        return true;
    }
    write_stdout(output, nullptr);
    stats.outcome = "require-error";
    report(stats);
    error_line(source_path, stats, result.message);
    exit_code = 1;
    return true;
}

} // namespace

} // namespace nift::rt
