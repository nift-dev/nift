#ifndef MINIFYPP_MINIFY_H
#define MINIFYPP_MINIFY_H

#include <string>
#include <vector>

namespace minify {

inline constexpr int format_version = 1;

enum class Format { Html, Css, JavaScript, Jsx, Json, Xml, Svg };
enum class OptimizationLevel { Conservative, Structured, Aggressive };
enum class JavaScriptOptimizationPass {
    BindingRename,
    ConstantFold,
    ConstantConditional,
    UnreachableCode,
    CompoundAssignment,
    DeclarationJoin,
    UnusedBinding,
    DeadStore,
    LiteralIife,
    PropertyMangle
};

struct Options {
    Options() = default;
    Options(OptimizationLevel level) : optimization(level) {}
    OptimizationLevel optimization = OptimizationLevel::Conservative;
    bool structured_jsx_expressions = false;
    bool mangle_top_level = false;
    std::vector<std::string> property_mangle_allowlist;
    std::vector<JavaScriptOptimizationPass> disabled_javascript_passes;
};

bool html(const std::string& input, std::string& output, std::string& error);
bool css(const std::string& input, std::string& output, std::string& error);
bool javascript(const std::string& input, std::string& output, std::string& error);
bool javascript(const std::string& input, std::string& output, std::string& error,
                const Options& options);
// Returns a deterministic, identifier-spelling-independent binding topology.
// Unresolved names and property-like identifiers remain spelling-sensitive.
bool javascript_binding_signature(const std::string& input, std::string& signature,
                                  std::string& error);
// Reports binding/mangling coverage without changing source text. The stable
// tab-separated schema is intended for benchmark and optimizer diagnostics.
bool javascript_mangle_report(const std::string& input, std::string& report,
                              std::string& error);
bool javascript_effect_signature(const std::string& input, std::string& signature,
                                 std::string& error);
// Returns a deterministic source-positioned semantic inventory. Node IDs are
// stable for a given non-trivia token stream and independent of whitespace.
bool javascript_ir_signature(const std::string& input, std::string& signature,
                             std::string& error);
bool javascript_cfg_signature(const std::string& input, std::string& signature,
                              std::string& error);
bool jsx(const std::string& input, std::string& output, std::string& error);
bool jsx(const std::string& input, std::string& output, std::string& error,
         const Options& options);
bool json(const std::string& input, std::string& output, std::string& error);
bool xml(const std::string& input, std::string& output, std::string& error);
bool svg(const std::string& input, std::string& output, std::string& error);

bool format_for_extension(const std::string& extension, Format& format);
bool run(Format format, const std::string& input, std::string& output, std::string& error);
bool run(Format format, const std::string& input, std::string& output,
         std::string& error, const Options& options);

} // namespace minify

#endif
