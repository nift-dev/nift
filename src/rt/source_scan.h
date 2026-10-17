#pragma once
// Byte-level scanning shared by the statement and expression layers:
// whitespace, trimming, identifiers and the legacy find_balanced.
#include <cstdint>
#include <string_view>

namespace nift::rt::detail {

bool is_space(char c);
std::uint32_t skip_space(std::string_view src, std::uint32_t p, std::uint32_t end);
// Parser::trim_copy trims exactly " \t\r\n".
std::string_view trim(std::string_view text);
// include/nift/context.h valid_binding_identifier.
bool valid_identifier(std::string_view text);
// First occurrence of `what` in src[from, end), or `end`.
std::uint32_t find_in(std::string_view src, std::string_view what, std::uint32_t from, std::uint32_t end);
// Whether src[p, end) starts with `what`.
bool starts(std::string_view src, std::uint32_t p, std::uint32_t end, std::string_view what);

// Legacy Parser::find_balanced on src[open, end): quote, backtick-run, @/*,
// <!-- and @// aware. Sets `close` to the matching close character.
bool find_balanced(std::string_view src, std::uint32_t open, std::uint32_t end,
                   char open_char, char close_char, std::uint32_t& close);

// End of a translator statement starting at `from`: the first ';' or newline
// at bracket depth 0 outside quotes (depth may go negative, which suppresses
// splitting until it returns to zero), or `end`.
std::uint32_t statement_end(std::string_view src, std::uint32_t from, std::uint32_t end);

} // namespace nift::rt::detail
