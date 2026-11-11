#include "rt/script_runner_stats.h"
#include "rt/eval_refusal_inventory.h"
#include "rt/refusal_inventory.h"
#include "rt/op_admission.h"

#include <algorithm>

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
         << ",\"instructions\":" << s.instructions << ",\"deferred\":" << s.deferred
         << ",\"source_bytes\":" << s.source_bytes
         << ",\"beyond_legacy_depth\":" << (s.beyond_legacy_depth ? "true" : "false")
         << ",\"vm_executions\":" << s.executions << ",\"legacy_executions\":" << s.legacy_executions
         << ",\"large_numbers\":" << s.large_numbers << ",\"large_in_loop_path\":" << s.large_in_loop_path
         << ",\"error_in_loop_path\":" << (s.error_in_loop_path ? "true" : "false")
         << ",\"code\":\"" << json_escape(s.code) << "\",\"catchable\":" << (s.catchable ? "true" : "false")
         << ",\"error_origin\":" << (s.error_origin ? "true" : "false")
         << ",\"heap_bytes\":" << s.heap_bytes
         << ",\"dispatch_defects\":" << dispatch_defect_count();  // B4: process-wide, see rt/op_admission.h
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

namespace {

// The source line containing `offset` (at most 160 bytes), for the inventory.
std::string line_text(const std::string& source, std::uint32_t offset) {
    const std::size_t begin = source.rfind('\n', offset == 0 ? 0 : offset - 1);
    const std::size_t from = begin == std::string::npos || offset == 0 ? 0 : begin + 1;
    const std::size_t end = source.find('\n', offset);
    return source.substr(from, std::min<std::size_t>((end == std::string::npos ? source.size() : end) - from, 160));
}

} // namespace

// NIFT_VM=classify-all: every refusal reason (rt/refusal_inventory.h).
void add_inventory(const std::string& source, const std::filesystem::path& source_path, Stats& stats, bool eval,
                   const std::vector<InvocationBinding>* invocation) {
    const RefusalInventory inventory = eval ? inventory_eval_refusals(source)
                                            : inventory_refusals(source, source_path, 2048, invocation);
    std::string out = ",\"reasons\":[";
    for (std::size_t k = 0; k < inventory.items.size(); ++k) {
        const InventoryItem& item = inventory.items[k];
        const std::string text = item.text.empty() ? line_text(source, item.offset) : item.text;
        out += (k ? ",{" : "{") + std::string("\"reason\":\"") + json_escape(item.reason) +
               "\",\"line\":" + std::to_string(item.line) + ",\"column\":" + std::to_string(item.column) +
               ",\"consequential\":" + (item.consequential ? "true" : "false") +
               (item.path.empty() ? std::string() : ",\"path\":\"" + json_escape(item.path) + "\"") +
               (item.detail.empty() ? std::string() : ",\"detail\":\"" + json_escape(item.detail) + "\"" +
                                                         ",\"text\":\"" + json_escape(text) +
                                                         "\"") + "}";
    }
    out += "],\"inventory_complete\":" + std::string(inventory.complete ? "true" : "false") +
           ",\"inventory_stop\":\"" + json_escape(inventory.stop) + "\",\"inventory_rounds\":" +
           std::to_string(inventory.rounds);
    stats.inventory_json = out;
}

} // namespace nift::rt::detail
