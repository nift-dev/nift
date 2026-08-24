#include "nift/nift.h"
#include <iostream>
#include <string>

static std::string json_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

// Differential harness: renders via the standalone C++ Engine and prints a
// stable observable JSON result (output, dependencies, requirements) or the
// error message. Args:
//   engine_harness <root> <page_text|-> <template_text|-> <page_name> <current_output|-> <page_path|-> <template_path|->
// A "-" for text means empty text; page_path/template_path override the text
// args when non-"-". Bindings are passed as simple "name=value" pairs on stdin
// (one per line); values are inserted as strings.
int main(int argc, char** argv) {
    if (argc < 8) { std::cerr << "usage: engine_harness root page template page_name current_output page_path template_path\n"; return 2; }
    std::string root = argv[1];
    std::string page_text = argv[2] == std::string("-") ? std::string() : argv[2];
    std::string template_text = argv[3] == std::string("-") ? std::string() : argv[3];
    std::string page_name = argv[4] == std::string("-") ? std::string() : argv[4];
    std::string current_output = argv[5] == std::string("-") ? std::string() : argv[5];
    std::string page_path = argv[6] == std::string("-") ? std::string() : argv[6];
    std::string template_path = argv[7] == std::string("-") ? std::string() : argv[7];
    const std::string mode = argc > 8 ? argv[8] : "composed";

    nift::Engine engine;
    engine.set_root(root);
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        engine.set(line.substr(0, eq), line.substr(eq + 1));
    }

    nift::Context context;
    if (!page_name.empty()) context.set_page_name(page_name);
    if (!current_output.empty()) context.set_current_output(current_output);

    nift::Source page = page_path.empty() ? nift::Source::text(page_text) : nift::Source::path(page_path);
    nift::Source tpl = template_path.empty() ? nift::Source::text(template_text) : nift::Source::path(template_path);

    auto result = mode == "partial" ? engine.render(page, context) : engine.render(page, tpl, context);
    if (!result.ok()) {
        std::cout << "{\"ok\":false,\"error\":\"" << json_escape(result.error().message) << "\"}\n";
        return 0;
    }
    std::cout << "{\"ok\":true,\"output\":\"" << json_escape(result.output()) << "\",\"dependencies\":[";
    bool first = true;
    for (const auto& d : result.dependencies()) { if (!first) std::cout << ","; first = false; std::cout << "\"" << json_escape(d) << "\""; }
    std::cout << "],\"requirements\":[";
    first = true;
    for (const auto& r : result.requirements()) { if (!first) std::cout << ","; first = false; std::cout << "\"" << json_escape(r) << "\""; }
    std::cout << "]}\n";
    return 0;
}
