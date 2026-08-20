#include "Console.h"

#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}

int main() {
    check(console::display_width("\t@pathto") == 15,
          "tab display width uses 8-column stops");
    check(console::expand_tabs("\t@pathto") == std::string(8, ' ') + "@pathto",
          "tab expansion is deterministic");

    const auto message = console::highlight_diagnostic_message(
        "@pathto path must stay inside the Nift project: /assets/css/style.css", true);
    check(message.find("\033[1;35m@pathto\033[0m") != std::string::npos,
          "diagnostic message colours Nift directive");
    check(message.find("\033[1;31m/assets/css/style.css\033[0m") != std::string::npos,
          "diagnostic message colours offending detail");

    // Diagnostic directive colouring is lexical, not a @pathto special case.
    // Cover the complete current @function surface plus a future lowercase
    // function token so new functions inherit the behaviour automatically.
    const std::vector<std::string> directives = {
        "content", "pathtopage", "filter", "map", "sort", "slice", "find",
        "some", "every", "distinct", "reverse", "sum", "prod", "min", "max",
        "reduce", "substr", "join", "input", "pathto", "pathtofile", "getenv",
        "ent", "json", "dep", "if", "for", "item", "paginate"
    };
    for (const auto& name : directives) {
        const std::string token = "@" + name;
        const auto highlighted_message = console::highlight_diagnostic_message(
            token + " diagnostic", true);
        check(highlighted_message.find("\033[1;35m" + token + "\033[0m") != std::string::npos,
              "all @functions colour in diagnostic messages");

        const std::string sample = "prefix " + token + "('x') suffix";
        const auto highlighted_source = console::highlight_nift_source(
            sample, sample.size(), 0, true);
        check(highlighted_source.find("\033[1;35m" + token + "\033[0m") != std::string::npos,
              "all @functions colour in source excerpts");
    }

    check(console::nift_function_token_end("@input('x')", 0) == 6,
          "generic function scanner captures the whole lowercase function token");
    check(console::nift_function_token_end("@Input('x')", 0) == 0,
          "function scanner follows Nift lowercase function-name grammar");
    check(console::nift_function_token_end("email@example.com", 5) == 5,
          "ordinary at-sign text is not treated as a Nift function");
    check(console::nift_function_token_end("@media screen", 0) == 0,
          "CSS at-rules are not treated as Nift functions");
    check(console::nift_function_token_end("@futuredirective()", 0) == 0,
          "unknown lowercase at-words are not styled as current Nift functions");

    const std::string source = "  <link href=\"@pathto('/assets/css/style.css')\">";
    const auto start = source.find("@pathto");
    const auto coloured = console::highlight_nift_source(
        source, start, std::string("@pathto('/assets/css/style.css')").size(), true);
    check(coloured.find("\033[1;35m@pathto\033[0m") != std::string::npos,
          "source excerpt colours directive");
    check(coloured.find("\033[1;31m('/assets/css/style.css')") == std::string::npos,
          "offending call is tokenized instead of flattened into one red blob");
    check(coloured.find("\033[1;31m'/assets/css/style.css'\033[0m") != std::string::npos,
          "offending quoted value is highlighted");
    check(coloured.find("\033[32m\"@pathto") == std::string::npos,
          "outer HTML attribute quote is not misclassified as a Nift string");
    check(coloured.find("\033[32m\">") == std::string::npos,
          "closing HTML attribute syntax is not misclassified as a Nift string");

    const std::string rich = "<div class=\"card\">@input('partials/header.html') $[page.title] @content</div>";
    const auto rich_coloured = console::highlight_nift_source(rich, rich.size(), 0, true);
    check(rich_coloured.find("\033[1;35m@input\033[0m") != std::string::npos,
          "@input is syntax highlighted");
    check(rich_coloured.find("\033[32m'partials/header.html'\033[0m") != std::string::npos,
          "non-offending string values are syntax highlighted");
    check(rich_coloured.find("\033[1;36m$[page.title]\033[0m") != std::string::npos,
          "$[...] values are syntax highlighted");
    check(rich_coloured.find("\033[1;35m@content\033[0m") != std::string::npos,
          "parameterless @content is syntax highlighted");
    check(rich_coloured.find("\033[32m\"card\"\033[0m") == std::string::npos,
          "ordinary HTML strings are left untouched");

    const auto plain = console::highlight_nift_source(source, start, 7, false);
    check(plain == source, "plain diagnostics remain ANSI-free");

    if (failures) return 1;
    std::cout << "console diagnostics smoke passed\n";
    return 0;
}
