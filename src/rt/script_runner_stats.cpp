#include "rt/script_runner_stats.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

// rt-lint: cold-file process-level entry reporting only

namespace nift::rt::detail {


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
         << ",\"code\":\"" << json_escape(s.code) << "\",\"catchable\":" << (s.catchable ? "true" : "false")
         << ",\"error_origin\":" << (s.error_origin ? "true" : "false")
         << ",\"heap_bytes\":" << s.heap_bytes;
    const auto list = [&line](const char* name, const std::vector<std::uint64_t>& values) {
        line << ",\"" << name << "\":[";
        for (std::size_t k = 0; k < values.size(); ++k) line << (k ? "," : "") << values[k];
        line << "]";
    };
    list("large_lines", s.large_lines);
    list("derived_lines", s.derived_lines);
    line << ",\"evidence_truncated\":" << (s.evidence_truncated ? "true" : "false")
         << ",\"heap_peak\":" << s.heap_peak << ",\"heap_live\":" << s.heap_live << s.inventory_json << s.coro_json
         << ",\"path\":\""
         << json_escape(s.path) << "\"}\n";
    if (std::string(target) == "1") {
        std::cerr << line.str() << std::flush;
    } else {
        std::ofstream file(target, std::ios::app);
        file << line.str();
    }
}

} // namespace nift::rt::detail
