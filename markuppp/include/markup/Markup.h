#pragma once

#include <string>
#include <functional>

namespace markup {

enum class Format {
    Markdown,
    AsciiDoc,
    ReStructuredText,
};

struct Options {
    // Library callers normally need an embeddable HTML fragment. The CLI's
    // --standalone flag opts into a complete HTML document.
    bool standalone = false;
    bool allow_raw_html = true;
    enum class MarkdownProfile {
        CommonMark,
        Extended,
    };
    MarkdownProfile markdown_profile = MarkdownProfile::CommonMark;
    std::string title;

    // AsciiDoc conversion is IO-free unless a host supplies this resolver.
    // The resolver receives (including identity, requested target) and returns
    // content plus a stable canonical identity. Hosts enforce their own root
    // and traversal policy before returning true.
    using AsciiDocIncludeResolver = std::function<bool(
        const std::string&, const std::string&, std::string&, std::string&, std::string&)>;
    AsciiDocIncludeResolver asciidoc_include_resolver;
    std::string asciidoc_source_identity = "<input>";
    std::function<void(const std::string&)> asciidoc_dependency;
    std::function<void(const std::string&)> asciidoc_diagnostic;
    using RstResourceResolver = std::function<bool(
        const std::string&, const std::string&, std::string&, std::string&, std::string&)>;
    RstResourceResolver rst_resource_resolver;
    std::string rst_source_identity = "<input>";
    std::function<void(const std::string&)> rst_dependency;
    std::function<void(const std::string&)> rst_diagnostic;
};

inline constexpr unsigned api_version = 4;

bool format_for_extension(const std::string& extension, Format& format);
const char* format_name(Format format);
bool is_supported(Format format);

// Converts one UTF-8 string without reading files or using global state.
// On failure, output is cleared and error contains a stable diagnostic.
bool convert(Format format, const std::string& input, std::string& output,
             std::string& error, const Options& options = {});

} // namespace markup
