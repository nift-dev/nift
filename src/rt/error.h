#pragma once
// Error values (CP09), matching legacy RuntimeErrorData: `error(message)`
// gives code "user.raised", category "user"; members are message, code,
// category, source, line, column and cause.
#include "rt/value.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace nift::rt {

// Legacy rules: user codes are "user.<seg>..." (2–8 lowercase segments);
// the cause is Null or an Error, the chain at most 16 deep; an origin is
// wholly present or absent. Returns false with `error` set when invalid.
bool make_error(std::string message, std::string code, const Value& cause, std::string source,
                std::size_t line, std::size_t column, Value& out, std::string& error);
Value make_user_error(std::string message);  // code "user.raised"
bool valid_user_error_code(std::string_view code);

// `error.message` etc.; returns false for an unknown member name.
bool error_member(const Value& error, std::string_view member, Value& out);

} // namespace nift::rt
