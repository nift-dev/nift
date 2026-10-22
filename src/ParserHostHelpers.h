#pragma once

// Host helpers shared by the legacy interpreter and the Runtime 2.0 Host
// natives: wall-clock epoch, the operating-system CSPRNG and glob traversal.
// Ordinary C++ types only -- no RuntimeValue, parser or legacy value
// machinery -- so src/rt/natives can use them and still link without the
// legacy value bridge (CP14 isolation). Implemented in ParserHostHelpers.cpp;
// ParserHelpers.h includes this header for the legacy callers.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#ifndef NIFT_PARSER_HELPER_HIDDEN
#if !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#define NIFT_PARSER_HELPER_HIDDEN __attribute__((visibility("hidden")))
#else
#define NIFT_PARSER_HELPER_HIDDEN
#endif
#endif

namespace nift::detail {

NIFT_PARSER_HELPER_HIDDEN bool nift_unix_epoch_milliseconds(
    std::int64_t& value, std::string& error);
// Fills data[0, count) from the operating-system CSPRNG. The caller owns (and
// has already budgeted) the buffer; nothing is allocated here.
NIFT_PARSER_HELPER_HIDDEN bool nift_secure_random_fill(
    unsigned char* data, std::size_t count, std::string& error);

NIFT_PARSER_HELPER_HIDDEN bool glob_has_magic(const std::string& value);
NIFT_PARSER_HELPER_HIDDEN bool glob_component_match(const std::string& pattern, const std::string& name);
enum class GlobPrefixEvent { TerminalPath, PrefixNode, PrefixResolve, FullDisplayPath, FinalValue };
#ifdef NIFT_TEST_GLOB_PREFIX_STATS
NIFT_PARSER_HELPER_HIDDEN void record_glob_prefix_event(GlobPrefixEvent event);
#else
inline void record_glob_prefix_event(GlobPrefixEvent) {}
#endif
struct GlobPrefix {
    std::filesystem::path original;
    std::filesystem::path resolved;
    std::error_code error;
    bool ready = false;
    bool requires_leaf_resolution = false;
    std::string relative_display;
    bool relative_fallback = false;
};
struct GlobMatch {
    std::filesystem::path absolute;
    std::shared_ptr<GlobPrefix> prefix;
    bool resolve_leaf = true;
};
NIFT_PARSER_HELPER_HIDDEN std::vector<GlobMatch> glob_expand_shared(const std::filesystem::path& resolved_pattern);
NIFT_PARSER_HELPER_HIDDEN std::vector<std::filesystem::path> glob_expand(const std::filesystem::path& resolved_pattern);
// Additive streaming visitor: traversal order, no collection or sorting. The
// callback returns false to stop the walk immediately. Existing callers keep
// the sorted vector interfaces above.
using GlobVisitor = std::function<bool(const std::filesystem::path&)>;
NIFT_PARSER_HELPER_HIDDEN bool glob_visit(const std::filesystem::path& resolved_pattern,
                                          const GlobVisitor& visitor);

}  // namespace nift::detail
