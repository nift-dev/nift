#include "rt/natives/minify_builtin.h"

#include "rt/atom.h"
#include "rt/build.h"
#include "rt/natives/support.h"
#include "rt/string.h"
#include <minify/Minify.h>

// rt-lint: cold-file adapter to embedded Minify++, outside VM dispatch.
namespace nift::rt::natives {

Status fn_minify(const Context& context, const Value* args, std::uint32_t argc,
                 Value& out, std::string& error) {
    if (argc != 2 || !args[0].is(Kind::String))
        return fail(error, "minify: source or path must be a string");
    if (!args[1].is(Kind::String))
        return fail(error, "minify: expected an in-memory format string");
    const auto source = string_view_of(args[0]);
    const auto format = string_view_of(args[1]);
    // The backend allocates ordinary C++ strings and parser scratch. Admit
    // input against the VM budget before copying it into the parser.
    const auto format_storage = size_add(size_mul(format.size(), 4), 512);
    const auto scratch = size_add(size_mul(source.size(), 1024), format_storage);
    if (!reserve_result(context, scratch)) {
        error = "minify: input exceeds the memory budget";
        return Status::Limit;
    }

    // The embedded JSON parser is recursive. Ignore delimiters in strings,
    // including escaped quotes. JSX can recursively parse brace expressions.
    if (format == "json" || format == "jsx") {
        std::size_t nested = 0;
        bool quoted = false, escaped = false;
        for (const char c : source) {
            if (format == "json" && (quoted || c == '"')) {
                if (escaped) escaped = false;
                else if (c == '\\' && quoted) escaped = true;
                else if (c == '"') quoted = !quoted;
                continue;
            }
            if (c == '{' || (format == "json" && c == '[')) {
                if (++nested > 128) {
                    error = "minify: input exceeds the parser nesting budget";
                    return Status::Limit;
                }
            } else if (format == "json" && (c == '}' || c == ']') && nested) {
                --nested;
            }
        }
    }

    minify::Format kind{};
    std::string output, message;
    const bool known = format == "html" || format == "css" || format == "js" ||
                       format == "jsx" || format == "json" || format == "xml" ||
                       format == "svg";
    const bool ok = known && minify::format_for_extension("." + std::string(format), kind) &&
                    minify::run(kind, std::string(source), output, message);
    if (!known) {
        constexpr std::string_view prefix = "unsupported format '";
        message.reserve(prefix.size() + format.size() + 1);
        message.append(prefix);
        message.append(format);
        message.push_back('\'');
    }
    if (!ok) output.clear();
    if (!reserve_result(context, size_add(size_mul(size_add(size_add(output.size(), message.size()),
                                                           format.size()), 2), 512))) {
        error = "minify: result exceeds the memory budget";
        return Status::Limit;
    }

    build::Object result;
    result.set(atom("ok"), Value::boolean(ok));
    result.set(atom("output"), make_string(output));
    result.set(atom("error"), make_string(message));
    result.set(atom("format"), make_string(format));
    result.set(atom("written"), make_string(""));
    out = std::move(result).publish();
    return Status::Ok;
}

} // namespace nift::rt::natives
