#include "nift/nift.h"
#include <iostream>
#include <optional>
#include <set>
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

static bool has_suffix(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Differential harness: renders via the standalone C++ Engine and prints a
// stable observable JSON result (output, dependencies, requirements,
// optionally recorded loader keys) or the error message. Args:
//   engine_harness <root> <page_text|-> <template_text|-> <page_name> <current_output|-> <page_path|-> <template_path|-> <mode> <seam|->
// A "-" for text means empty text; page_path/template_path override the text
// args when non-"-". Bindings are passed as simple "name=value" pairs on stdin
// (one per line); values are inserted as strings.
//
// The optional "seam" argument installs deterministic fixture seams:
//   "-"      no loader, no environment provider (default reads/process env)
//   "loader" custom loader that serves a fixed fixture document per resolved
//            path key and records every key it is asked for (loaderKeys)
//   "env"    custom environment provider with fixed values
int run_main(int argc, char** argv) {
    if (argc < 8) { std::cerr << "usage: engine_harness root page template page_name current_output page_path template_path [mode] [seam]\n"; return 2; }
    std::string root = argv[1];
    std::string page_text = argv[2] == std::string("-") ? std::string() : argv[2];
    std::string template_text = argv[3] == std::string("-") ? std::string() : argv[3];
    std::string page_name = argv[4] == std::string("-") ? std::string() : argv[4];
    std::string current_output = argv[5] == std::string("-") ? std::string() : argv[5];
    std::string page_path = argv[6] == std::string("-") ? std::string() : argv[6];
    std::string template_path = argv[7] == std::string("-") ? std::string() : argv[7];
    const std::string mode = argc > 8 ? argv[8] : "composed";
    const std::string seam = argc > 9 ? argv[9] : "-";

    nift::Engine engine;
    engine.set_root(root);
    std::set<std::string> loader_keys;
    if (seam == "loader") {
        engine.set_loader([&](std::string_view key) -> std::optional<std::string> {
            loader_keys.insert(std::string(key));
            const std::string k(key);
            if (has_suffix(k, "/templates/template.html")) return "<main>@content</main>\n";
            if (has_suffix(k, "/content/blog.html")) return "<p>LOADER-CONTENT</p>\n";
            if (has_suffix(k, "/content/post.html")) return "@input(\"part.html\")\n";
            if (has_suffix(k, "/content/part.html")) return "<p>LOADER-PART</p>\n";
            return std::nullopt;
        });
    }
    if (seam == "env") {
        engine.set_environment_provider([](std::string_view name) -> std::optional<std::string> {
            if (name == "NIFT_ENV_A") return "alpha";
            if (name == "NIFT_ENV_B") return "beta";
            return std::nullopt;
        });
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string name = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        // A "json:" prefix binds a JSON value instead of a string, so the
        // differential can exercise arrays/objects/numbers/bools (NR10).
        if (value.rfind("json:", 0) == 0) engine.set_json(name, value.substr(5));
        else engine.set(name, value);
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
    if (seam == "loader") {
        std::cout << "],\"loaderKeys\":[";
        first = true;
        for (const auto& k : loader_keys) { if (!first) std::cout << ","; first = false; std::cout << "\"" << json_escape(k) << "\""; }
    }
    std::cout << "]}\n";
    return 0;
}

// On Windows, char argv is the ANSI codepage (mangles UTF-8 templates) and
// text-mode stdout converts '\n' to '\r\n'; the differential compares
// byte-identical output with the Rust harness, so the harness needs UTF-8
// wide args and binary stdout.
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <vector>
int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_BINARY);
    std::vector<std::string> utf8_args;
    utf8_args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string arg(static_cast<std::size_t>(size > 0 ? size - 1 : 0), '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, arg.data(), size, nullptr, nullptr);
        utf8_args.push_back(std::move(arg));
    }
    std::vector<char*> c_args;
    c_args.reserve(utf8_args.size());
    for (auto& arg : utf8_args) c_args.push_back(arg.data());
    return run_main(argc, c_args.data());
}
#else
int main(int argc, char** argv) { return run_main(argc, argv); }
#endif
