// CP10 oracle: compares rt::parse_script with the legacy script translator
// (Parser::translate_function_program), whose failures are the only
// parse-time diagnostics of a .f script (code native.translation_error).
//
//   legacy_oracle compare FILE...                 verdicts must agree
//   legacy_oracle mutate SEED COUNT OUT FILE...   build an error corpus of
//                                                 COUNT legacy-rejected mutants
//   legacy_oracle errors CORPUS                   stored corpus still matches legacy
//   legacy_oracle bench ITERS FILE...             legacy translation only
#include "Parser.h"
#include "ScriptHost.h"
#include "rt_parser_tool.h"

#include <filesystem>
#include <iostream>
#include <set>

struct RtParserOracleAccess {
    static rt_tool::Verdict verdict(Parser& parser, const std::string& source) {
        std::string translated, message;
        nift::detail::SourceView view;
        nift::detail::DiagnosticOrigin origin;
        rt_tool::Verdict verdict;
        verdict.ok = parser.translate_function_program(source, translated, message,
                                                       nift::detail::SourceView::identity("oracle.f", source), &view,
                                                       &origin);
        if (!verdict.ok) {
            verdict.line = origin.line;
            verdict.column = origin.column;
            verdict.message = message;
        }
        return verdict;
    }
};

namespace {

bool load(const std::string& path, std::string& source) {
    std::string raw;
    if (!rt_tool::read_file(path, raw)) {
        std::cerr << "cannot read " << path << '\n';
        return false;
    }
    source = rt_tool::strip_shebang(raw);
    return true;
}

int compare_mode(Parser& parser, int argc, char** argv) {
    std::size_t files = 0, accepted = 0, rejected = 0, mismatched = 0;
    for (int i = 2; i < argc; ++i) {
        std::string source;
        if (!load(argv[i], source)) return 2;
        const auto legacy = RtParserOracleAccess::verdict(parser, source);
        const auto rt = rt_tool::rt_verdict(source);
        ++files;
        if (legacy.str() != rt.str()) {
            ++mismatched;
            std::cout << "MISMATCH\t" << argv[i] << "\n  legacy: " << legacy.str() << "\n  rt:     " << rt.str()
                      << '\n';
        } else if (legacy.ok) {
            ++accepted;
        } else {
            ++rejected;
            std::cout << "REJECT\t" << argv[i] << '\t' << legacy.str() << '\n';
        }
    }
    std::cout << "compare: files=" << files << " legacy_accepted=" << accepted << " legacy_rejected_identical="
              << rejected << " mismatched=" << mismatched << '\n';
    return mismatched ? 1 : 0;
}

int mutate_mode(Parser& parser, int argc, char** argv) {
    if (argc < 6) return 2;
    rt_tool::Mutator mutator(std::stoull(argv[2]));
    const std::size_t wanted = std::stoul(argv[3]);
    const std::string out_path = argv[4];
    std::vector<std::string> seeds;
    for (int i = 5; i < argc; ++i) {
        std::string source;
        if (!load(argv[i], source)) return 2;
        if (source.size() <= 1536) seeds.push_back(source);
    }
    if (seeds.empty()) return 2;
    std::set<std::string> seen;
    std::vector<rt_tool::ErrorCase> cases;
    std::map<std::string, std::size_t> messages;
    std::size_t tried = 0, legacy_ok = 0, rt_false_reject = 0, mismatched = 0;
    while (cases.size() < wanted && tried < wanted * 200) {
        ++tried;
        const std::string mutant = mutator.structural(seeds[mutator.below(seeds.size())]);
        if (!seen.insert(mutant).second) continue;
        const auto legacy = RtParserOracleAccess::verdict(parser, mutant);
        const auto rt = rt_tool::rt_verdict(mutant);
        if (legacy.ok) {
            ++legacy_ok;
            if (!rt.ok) {
                ++rt_false_reject;
                std::cout << "FALSE-REJECT rt: " << rt.str() << "\n----\n" << mutant << "\n----\n";
            }
            continue;
        }
        if (legacy.str() != rt.str()) {
            ++mismatched;
            std::cout << "MISMATCH legacy: " << legacy.str() << "\n         rt:     " << rt.str() << "\n----\n"
                      << mutant << "\n----\n";
        }
        ++messages[legacy.message];
        cases.push_back({mutant, legacy});
    }
    std::ofstream out(out_path, std::ios::binary);
    for (const auto& c : cases) rt_tool::write_case(out, c);
    std::cout << "mutate: tried=" << tried << " legacy_accepted=" << legacy_ok
              << " rt_rejected_legacy_accepted=" << rt_false_reject << " legacy_rejected=" << cases.size()
              << " mismatched=" << mismatched << '\n';
    for (const auto& [message, count] : messages) std::cout << "  " << count << "\t" << message << '\n';
    return mismatched || rt_false_reject ? 1 : 0;
}

// Re-checks a stored error corpus against the legacy translator (drift).
int errors_mode(Parser& parser, const std::string& path) {
    std::string text;
    std::vector<rt_tool::ErrorCase> cases;
    if (!rt_tool::read_file(path, text) || !rt_tool::read_cases(text, cases)) return 2;
    std::size_t matched = 0;
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const auto got = RtParserOracleAccess::verdict(parser, cases[i].source);
        if (got.str() == cases[i].expected.str()) { ++matched; continue; }
        std::cout << "DRIFT case " << i << "\n  stored: " << cases[i].expected.str() << "\n  legacy: " << got.str()
                  << '\n';
    }
    std::cout << "error corpus: " << matched << '/' << cases.size() << " identical\n";
    return matched == cases.size() && !cases.empty() ? 0 : 1;
}

int bench_mode(Parser& parser, int argc, char** argv) {
    if (argc < 4) return 2;
    const long iterations = std::stol(argv[2]);
    std::vector<std::string> sources;
    std::size_t bytes = 0;
    for (int i = 3; i < argc; ++i) {
        std::string source;
        if (!load(argv[i], source)) return 2;
        bytes += source.size();
        sources.push_back(std::move(source));
    }
    std::size_t ok = 0;
    for (long n = 0; n < iterations; ++n)
        for (const auto& source : sources) ok += RtParserOracleAccess::verdict(parser, source).ok;
    std::cout << "bench-legacy: files=" << sources.size() << " bytes=" << bytes << " iterations=" << iterations
              << " ok=" << ok << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: legacy_oracle compare|mutate|bench ...\n";
        return 2;
    }
    ScriptRenderHost host(std::filesystem::current_path());
    TrackedInfo tracked;
    Parser parser(host, tracked);
    const std::string mode = argv[1];
    if (mode == "compare") return compare_mode(parser, argc, argv);
    if (mode == "mutate") return mutate_mode(parser, argc, argv);
    if (mode == "bench") return bench_mode(parser, argc, argv);
    if (mode == "errors") return errors_mode(parser, argv[2]);
    return 2;
}
