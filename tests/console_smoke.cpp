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
    check(console::display_width("\t@path") == 13,
          "tab display width uses 8-column stops");
    check(console::expand_tabs("\t@path") == std::string(8, ' ') + "@path",
          "tab expansion is deterministic");

    const auto message = console::highlight_diagnostic_message(
        "@path path must stay inside the Nift project: /assets/css/style.css", true);
    check(message.find("\033[1;35m@path\033[0m") != std::string::npos,
          "diagnostic message colours Nift directive");
    check(message.find("\033[1;31m/assets/css/style.css\033[0m") != std::string::npos,
          "diagnostic message colours offending detail");

    // Diagnostic directive colouring is lexical, not a @path special case.
    // Cover the complete current @function surface plus a future lowercase
    // function token so new functions inherit the behaviour automatically.
    const std::vector<std::string> directives = {
        "content", "pathtopage", "filter", "map", "sort", "slice", "find",
        "some", "every", "distinct", "reverse", "sum", "prod", "min", "max",
        "reduce", "substr", "join", "input", "path", "pathto", "pathtofile", "getenv",
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

    const std::string source = "  <link href=\"@path('/assets/css/style.css')\">";
    const auto start = source.find("@path");
    const auto coloured = console::highlight_nift_source(
        source, start, std::string("@path('/assets/css/style.css')").size(), true);
    check(coloured.find("\033[1;35m@path\033[0m") != std::string::npos,
          "source excerpt colours directive");
    check(coloured.find("\033[1;31m('/assets/css/style.css')") == std::string::npos,
          "offending call is tokenized instead of flattened into one red blob");
    check(coloured.find("\033[1;31m'/assets/css/style.css'\033[0m") != std::string::npos,
          "offending quoted value is highlighted");
    check(coloured.find("\033[32m\"@path") == std::string::npos,
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

    // ---- bounded source excerpts for long/generated lines ----
    const std::string long_line(20000, 'x');
    const auto end_excerpt = console::render_source_excerpt(
        long_line + "@bad(foo)", 20001, 9);
    check(end_excerpt.excerpt.size() < 256,
          "long single-line diagnostic excerpt is bounded");
    check(end_excerpt.excerpt.find("...") == 0,
          "long line excerpt is left-cropped with leading ellipsis");
    check(end_excerpt.excerpt.find("@bad(foo)") != std::string::npos,
          "long line excerpt retains the offending span");
    check(end_excerpt.excerpt.find("xxx") != std::string::npos,
          "long line excerpt retains left context");
    check(end_excerpt.caret_columns ==
              console::display_width(end_excerpt.excerpt.substr(0, end_excerpt.span_byte_start)),
          "caret column is relative to the rendered excerpt, not the original column");

    const auto begin_excerpt = console::render_source_excerpt(
        "@bad(foo)" + long_line, 1, 9);
    check(begin_excerpt.excerpt.find("@bad(foo)") == 0,
          "column-1 long line excerpt has no leading ellipsis");
    check(begin_excerpt.excerpt.find("...") != std::string::npos,
          "column-1 long line excerpt is right-cropped with trailing ellipsis");
    check(begin_excerpt.caret_columns == 0,
          "column-1 caret is at the excerpt start");

    const auto middle_excerpt = console::render_source_excerpt(
        std::string(10000, 'a') + "@bad(foo)" + std::string(10000, 'b'), 10001, 9);
    check(middle_excerpt.excerpt.find("...") == 0,
          "middle error excerpt is left-cropped");
    check(middle_excerpt.excerpt.find("@bad(foo)") != std::string::npos,
          "middle error excerpt retains the span");
    check(middle_excerpt.caret_columns ==
              console::display_width(middle_excerpt.excerpt.substr(0, middle_excerpt.span_byte_start)),
          "middle error caret is excerpt-relative");

    const auto short_excerpt = console::render_source_excerpt(
        "@substr(1, 2)", 1, 10);
    check(short_excerpt.excerpt == "@substr(1, 2)",
          "short line excerpt keeps the whole line (no crop)");
    check(short_excerpt.excerpt.find("...") == std::string::npos,
          "short line excerpt has no ellipsis");
    check(short_excerpt.caret_columns == 0, "short line caret starts at column 1");

    const auto end_line_excerpt = console::render_source_excerpt("hello world", 11, 0);
    check(end_line_excerpt.caret_columns == 10,
          "error at line end (column == length) still places the caret");
    check(end_line_excerpt.underline_columns == 1,
          "empty span at line end produces a single-caret underline");

    const auto beyond_excerpt = console::render_source_excerpt("hello", 500, 0);
    check(beyond_excerpt.caret_columns <= beyond_excerpt.excerpt.size(),
          "column beyond line length is clamped inside the excerpt");

    const auto long_span_excerpt = console::render_source_excerpt(
        "prefix " + std::string(200, 'z'), 8, 200);
    check(long_span_excerpt.underline_columns <= 40,
          "very long span caps the underline");
    check(long_span_excerpt.excerpt.find("...") != std::string::npos,
          "very long span is cropped with ellipsis");
    check(long_span_excerpt.excerpt.size() < 256,
          "very long span keeps the excerpt bounded");

    const auto tab_excerpt = console::render_source_excerpt(
        "a\tb\tc\t@bad(foo)", 8, 9);
    check(tab_excerpt.excerpt.find('\t') == std::string::npos,
          "excerpt expands tabs to spaces");
    check(tab_excerpt.caret_columns ==
              console::display_width(tab_excerpt.excerpt.substr(0, tab_excerpt.span_byte_start)),
          "caret aligns with the tab-expanded excerpt");

    const auto utf8_excerpt = console::render_source_excerpt(
        "\xC3\xA9\xC3\xA9\xC3\xA9 @bad(foo)", 8, 9);
    check(utf8_excerpt.caret_columns ==
              console::display_width(utf8_excerpt.excerpt.substr(0, utf8_excerpt.span_byte_start)),
          "caret aligns with the UTF-8 excerpt using display columns");

    if (failures) return 1;
    std::cout << "console diagnostics smoke passed\n";
    return 0;
}
