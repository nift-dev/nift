#pragma once
// CP16 Host layer: parsing and validation of run()'s trailing options object
// (D-030 alternative A; design §12.2). Independent of the VM value type: the
// VM wiring (later) converts the object's members into OptionValue.
//
//   run(prog, args..., {"mode": "capture"|"pipe"|"stream"|"interactive"|"detached",
//                       "timeout_ms": n, "kill_after_ms": n,
//                       "stdin": "inherit"|"null"|{"file": path}|{"text": string}})
//
// `capture` is today's file capture, unchanged; `pipe` is the explicit
// opt-in to pipe capture (D-034). `stdin` omitted = "inherit" = today.
//
// Rules (D-030): unknown keys are fatal with a spelling suggestion; wrong
// value types and incompatible combinations are fatal argument errors
// (`runtime.invalid_argument`, not catchable); no options object means
// today's behaviour exactly.
#include "rt/host/host_error.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nift::rt::host {

struct OptionValue {
    enum class Type { Null, Bool, Int, Number, String, Object, Other };
    Type type = Type::Null;
    bool boolean = false;
    std::int64_t integer = 0;
    double number = 0;
    std::string text;       // String
    std::string type_name;  // Other: the Nift type name, for messages ("bytes", "array", ...)
    std::vector<std::string> keys;     // Object members, in order
    std::vector<OptionValue> values;
    static OptionValue of_int(std::int64_t v) { OptionValue o; o.type = Type::Int; o.integer = v; return o; }
    static OptionValue of_number(double v) { OptionValue o; o.type = Type::Number; o.number = v; return o; }
    static OptionValue of_string(std::string v) {
        OptionValue o;
        o.type = Type::String;
        o.text = std::move(v);
        return o;
    }
    static OptionValue of_bool(bool v) { OptionValue o; o.type = Type::Bool; o.boolean = v; return o; }
    static OptionValue other(std::string name) {
        OptionValue o;
        o.type = Type::Other;
        o.type_name = std::move(name);
        return o;
    }
    static OptionValue object(std::vector<std::pair<std::string, OptionValue>> members) {
        OptionValue o;
        o.type = Type::Object;
        for (auto& m : members) { o.keys.push_back(std::move(m.first)); o.values.push_back(std::move(m.second)); }
        return o;
    }
};

using OptionObject = std::vector<std::pair<std::string, OptionValue>>;

enum class RunMode { Capture, Pipe, Stream, Interactive, Detached };
const char* run_mode_name(RunMode mode);

// stdin source (D-034). File: read by the Host through HostCall (path
// policy, dependency recording, read limit) and fed like Text, so the child
// gets exactly the recorded bytes. Text: strings only (no byte
// reinterpretation; binary stdin is a later byte-oriented form).
struct StdinOption {
    enum class Kind { Inherit, Null, File, Text };
    Kind kind = Kind::Inherit;
    bool given = false;   // the key was present
    std::string value;    // File: the path as spelled; Text: the text
};

struct RunOptions {
    bool present = false;  // an options object was passed
    RunMode mode = RunMode::Capture;
    std::optional<std::chrono::milliseconds> timeout;
    std::optional<std::chrono::milliseconds> kill_after;  // unset: the 2,000 ms default (D-030)
    StdinOption stdin_source;
    // The mode returns a Process handle (interactive, detached) rather than a result object.
    bool returns_handle() const { return mode == RunMode::Interactive || mode == RunMode::Detached; }
};

inline constexpr std::chrono::milliseconds kDefaultKillAfter{2000};
// Largest accepted millisecond value: integers a Nift number holds exactly.
inline constexpr std::int64_t kMaxOptionMilliseconds = 9007199254740991LL;  // 2^53 - 1

// Known option keys, in documentation order.
const std::vector<std::string>& run_option_keys();

// Validates `object` for built-in `builtin` ("run"). Returns Ok, or Fatal
// with `runtime.invalid_argument` and the exact message.
HostStatus parse_run_options(const OptionObject& object, std::string_view builtin, RunOptions& out, HostError& error);

// Context check made at launch (not in the parser, which has no context):
// detaching inside a build is `process.detach_in_build` (D-030).
HostStatus check_run_context(const RunOptions& options, bool in_build, std::string_view builtin, HostError& error);

// The `stdin` value alone (exposed for tests).
HostStatus parse_stdin(const OptionValue& value, const std::string& builtin, StdinOption& out, HostError& error);

// Closest known key within a small edit distance, or empty.
std::string suggest_option(std::string_view unknown, const std::vector<std::string>& known);
// Optimal-string-alignment distance (transpositions count as one edit).
std::size_t edit_distance(std::string_view a, std::string_view b);

} // namespace nift::rt::host
