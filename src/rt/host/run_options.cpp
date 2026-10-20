#include "rt/host/run_options.h"
// rt-lint: cold-file run() option parsing, once per call, never on the VM dispatch path

#include <algorithm>
#include <cmath>

namespace nift::rt::host {

const char* run_mode_name(RunMode mode) {
    switch (mode) {
        case RunMode::Capture: return "capture";
        case RunMode::Pipe: return "pipe";
        case RunMode::Stream: return "stream";
        case RunMode::Interactive: return "interactive";
        case RunMode::Detached: return "detached";
    }
    return "capture";
}

const std::vector<std::string>& run_option_keys() {
    static const std::vector<std::string> keys = {"mode", "timeout_ms", "kill_after_ms", "stdin"};
    return keys;
}

std::size_t edit_distance(std::string_view a, std::string_view b) {
    const std::size_t n = a.size(), m = b.size();
    std::vector<std::vector<std::size_t>> d(n + 1, std::vector<std::size_t>(m + 1, 0));
    for (std::size_t i = 0; i <= n; ++i) d[i][0] = i;
    for (std::size_t j = 0; j <= m; ++j) d[0][j] = j;
    for (std::size_t i = 1; i <= n; ++i) {
        for (std::size_t j = 1; j <= m; ++j) {
            const std::size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
                d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
        }
    }
    return d[n][m];
}

static std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

std::string suggest_option(std::string_view unknown, const std::vector<std::string>& known) {
    // Case and '-' vs '_' differences are always suggested; otherwise at most
    // 2 edits, and fewer than half of the shorter word (no wild guesses).
    std::string folded = lower(unknown);
    std::replace(folded.begin(), folded.end(), '-', '_');
    std::string best;
    std::size_t best_distance = static_cast<std::size_t>(-1);
    for (const auto& key : known) {
        if (folded == key) return key;
        const std::size_t distance = edit_distance(folded, key);
        const std::size_t shorter = std::min(folded.size(), key.size());
        if (distance <= 2 && distance * 2 < shorter && distance < best_distance) {
            best = key;
            best_distance = distance;
        }
    }
    return best;
}

static std::string describe(const OptionValue& v) {
    switch (v.type) {
        case OptionValue::Type::Null: return "null";
        case OptionValue::Type::Bool: return "bool";
        case OptionValue::Type::Int: case OptionValue::Type::Number: return "number";
        case OptionValue::Type::String: return "string";
        case OptionValue::Type::Object: return "object";
        case OptionValue::Type::Other: return v.type_name.empty() ? std::string("value") : v.type_name;
    }
    return "value";
}

static bool milliseconds(const OptionValue& v, std::chrono::milliseconds& out) {
    std::int64_t n = 0;
    if (v.type == OptionValue::Type::Int) n = v.integer;
    else if (v.type == OptionValue::Type::Number) {
        if (!std::isfinite(v.number) || std::floor(v.number) != v.number) return false;
        if (v.number < 0 || v.number > static_cast<double>(kMaxOptionMilliseconds)) return false;
        n = static_cast<std::int64_t>(v.number);
    } else return false;
    if (n < 0 || n > kMaxOptionMilliseconds) return false;
    out = std::chrono::milliseconds(n);
    return true;
}

static const char* kStdinForms = "\"inherit\", \"null\", {\"file\": path} or {\"text\": string}";

HostStatus parse_stdin(const OptionValue& value, const std::string& b, StdinOption& out, HostError& error) {
    auto bad = [&](std::string message) { return make_error(error, "runtime.invalid_argument", b + ": " + message); };
    out = StdinOption();
    out.given = true;
    if (value.type == OptionValue::Type::String) {
        if (value.text == "inherit") { out.kind = StdinOption::Kind::Inherit; return HostStatus::Ok; }
        if (value.text == "null") { out.kind = StdinOption::Kind::Null; return HostStatus::Ok; }
        static const std::vector<std::string> words = {"inherit", "null"};
        const std::string suggestion = suggest_option(value.text, words);
        std::string message = std::string("stdin must be ") + kStdinForms;
        if (!suggestion.empty()) message += " (did you mean \"" + suggestion + "\"?)";
        else message += " (a path is given as {\"file\": \"" + value.text + "\"}, text as {\"text\": ...})";
        return bad(message);
    }
    if (value.type != OptionValue::Type::Object)
        return bad(std::string("stdin must be ") + kStdinForms + ", not " + describe(value));
    if (value.keys.size() != 1)
        return bad(std::string("stdin object must have exactly one key, \"file\" or \"text\" (got ") +
                   std::to_string(value.keys.size()) + ")");
    const std::string& key = value.keys[0];
    const OptionValue& v = value.values[0];
    static const std::vector<std::string> forms = {"file", "text"};
    if (key != "file" && key != "text") {
        const std::string suggestion = suggest_option(key, forms);
        std::string message = "unknown stdin form '" + key + "'";
        message += suggestion.empty() ? std::string(" (forms: {\"file\": path}, {\"text\": string})")
                                      : " (did you mean '" + suggestion + "'?)";
        return bad(message);
    }
    if (v.type != OptionValue::Type::String) {
        std::string message = "stdin." + key + " must be a string, not " + describe(v);
        if (key == "text" && v.type == OptionValue::Type::Other && v.type_name == "bytes")
            message += " (binary stdin is not supported yet; text is never reinterpreted from bytes)";
        return bad(message);
    }
    if (key == "file") {
        if (v.text.empty()) return bad("stdin.file must not be empty");
        out.kind = StdinOption::Kind::File;
    } else out.kind = StdinOption::Kind::Text;
    out.value = v.text;
    return HostStatus::Ok;
}

HostStatus parse_run_options(const OptionObject& object, std::string_view builtin, RunOptions& out, HostError& error) {
    const std::string b(builtin);
    auto bad = [&](std::string message) { return make_error(error, "runtime.invalid_argument", b + ": " + message); };
    RunOptions options;
    options.present = true;
    bool seen_mode = false, seen_timeout = false, seen_kill = false, seen_stdin = false;
    // Unknown keys first, in object order: a typo must never be reported as
    // a missing/incompatible option instead (the user's actual mistake).
    for (const auto& [key, value] : object) {
        (void)value;
        const auto& keys = run_option_keys();
        if (std::find(keys.begin(), keys.end(), key) != keys.end()) continue;
        const std::string suggestion = suggest_option(key, keys);
        if (!suggestion.empty()) return bad("unknown option '" + key + "' (did you mean '" + suggestion + "'?)");
        return bad("unknown option '" + key + "' (options: mode, timeout_ms, kill_after_ms, stdin)");
    }
    for (const auto& [key, value] : object) {
        if (key == "mode") {
            if (seen_mode) return bad("option 'mode' given twice");
            seen_mode = true;
            if (value.type != OptionValue::Type::String) return bad("mode must be a string, not " + describe(value));
            if (value.text == "capture") options.mode = RunMode::Capture;
            else if (value.text == "pipe") options.mode = RunMode::Pipe;
            else if (value.text == "stream") options.mode = RunMode::Stream;
            else if (value.text == "interactive") options.mode = RunMode::Interactive;
            else if (value.text == "detached") options.mode = RunMode::Detached;
            else {
                static const std::vector<std::string> modes = {"capture", "pipe", "stream", "interactive", "detached"};
                const std::string suggestion = suggest_option(value.text, modes);
                std::string message = "mode must be one of capture, pipe, stream, interactive, detached";
                if (!suggestion.empty()) message += " (did you mean '" + suggestion + "'?)";
                return bad(message);
            }
        } else if (key == "timeout_ms") {
            if (seen_timeout) return bad("option 'timeout_ms' given twice");
            seen_timeout = true;
            std::chrono::milliseconds ms{0};
            if (!milliseconds(value, ms)) return bad("timeout_ms must be a non-negative integer");
            options.timeout = ms;
        } else if (key == "stdin") {
            if (seen_stdin) return bad("option 'stdin' given twice");
            seen_stdin = true;
            HostStatus st = parse_stdin(value, b, options.stdin_source, error);
            if (st != HostStatus::Ok) return st;
        } else if (key == "kill_after_ms") {
            if (seen_kill) return bad("option 'kill_after_ms' given twice");
            seen_kill = true;
            std::chrono::milliseconds ms{0};
            if (!milliseconds(value, ms)) return bad("kill_after_ms must be a non-negative integer");
            options.kill_after = ms;
        }
    }
    // Incompatible combinations (design §11.7, §12.2).
    if (options.mode == RunMode::Detached && options.timeout)
        return bad("deadline is not allowed for a detached process");
    const auto& in = options.stdin_source;
    if (in.given && options.mode == RunMode::Interactive)
        return bad("stdin is not allowed in mode interactive (the process reads what write() sends)");
    if (in.given && options.mode == RunMode::Detached &&
        (in.kind == StdinOption::Kind::Inherit || in.kind == StdinOption::Kind::Text))
        return bad(std::string("stdin ") + (in.kind == StdinOption::Kind::Inherit ? "\"inherit\"" : "{\"text\": ...}") +
                   " is not allowed for a detached process (use \"null\" or {\"file\": path})");
    if (options.kill_after && !options.timeout &&
        (options.mode == RunMode::Capture || options.mode == RunMode::Pipe || options.mode == RunMode::Stream))
        return bad(std::string("kill_after_ms needs timeout_ms in mode ") + run_mode_name(options.mode) +
                   " (nothing else can cancel a blocking run)");
    out = options;
    return HostStatus::Ok;
}

HostStatus check_run_context(const RunOptions& options, bool in_build, std::string_view builtin, HostError& error) {
    if (in_build && options.mode == RunMode::Detached)
        return make_error(
            error, "process.detach_in_build",
            std::string(builtin) + ": a detached process is not allowed during a build (use a standalone script)");
    return HostStatus::Ok;
}

} // namespace nift::rt::host
