#include <jspp/frontend.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>

namespace {
const char* status_name(jspp::syntax::ParseStatus status) {
    switch (status) {
    case jspp::syntax::ParseStatus::Success: return "success";
    case jspp::syntax::ParseStatus::SyntaxError: return "syntax-error";
    case jspp::syntax::ParseStatus::Unsupported: return "unsupported";
    }
    return "unknown";
}
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: jspp-frontend-observe FILE...\n";
        return 2;
    }
    for (int argument = 1; argument < argc; ++argument) {
        std::ifstream input(argv[argument], std::ios::binary);
        if (!input) {
            std::cerr << "cannot open " << argv[argument] << '\n';
            return 1;
        }
        std::string source{std::istreambuf_iterator<char>(input), {}};
        jspp::syntax::ParseResult result;
        const auto status = jspp::syntax::parse(std::move(source), result);
        std::map<std::string, std::size_t> features;
        for (const auto& token : result.tree.lexical.tokens) {
            const std::string_view spelling = jspp::syntax::spelling(result.tree.lexical, token);
            if (spelling == "function" || spelling == "class" || spelling == "=>" ||
                spelling == "import" || spelling == "export" || spelling == "async" ||
                spelling == "await" || spelling == "yield" || spelling == "?." ||
                spelling == "??" || spelling == "..." || spelling == "#")
                ++features[std::string(spelling)];
            if (token.kind == jspp::syntax::TokenKind::Template) ++features["template"];
            if (token.kind == jspp::syntax::TokenKind::Regex) ++features["regex"];
        }
        std::cout << argv[argument] << '\t' << status_name(status) << '\t'
                  << result.tree.lexical.tokens.size() << '\t'
                  << result.tree.nodes.size();
        for (const auto& feature : features)
            std::cout << '\t' << feature.first << '=' << feature.second;
        if (status != jspp::syntax::ParseStatus::Success)
            std::cout << "\tat=" << result.diagnostic.range.begin
                      << "\tdiagnostic=" << result.diagnostic.message;
        std::cout << '\n';
    }
}
