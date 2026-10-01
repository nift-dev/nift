#include "JsonFile.h"
#include "RuntimeJson.h"
#include "Parser.h"
#include "ParserAsyncPool.h"
#include "ParserHelpers.h"
#include "FrontMatter.h"
#include "Console.h"
#include "FileSystem.h"
#include "Environment.h"
#include "FfiAbi.h"
#include "Proc.h"
#include "ProjectInfo.h"
#include "Automation.h"
#include "Ast.h"
#include "Json.h"
#include "JsonSchema.h"
#include "RenderHost.h"
#include <markup/Markup.h>
#include <minify/Minify.h>
#include <nift/context.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>
#include <sstream>
#include <fstream>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <deque>
#include <condition_variable>
#include <set>
#include <thread>
#include <atomic>
#include <charconv>
#include <limits>
#include <cstdint>
#include <cerrno>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif


namespace fs = std::filesystem;

using nift::detail::nift_binding_type;
using nift::detail::nift_binding_type_name;
using nift::detail::nift_type_assignable;
using nift::detail::kMaxCallableDepth;
using nift::detail::parse_runtime_json;
using nift::detail::call_runtime_host;
using nift::detail::runtime_scalar_key;
using nift::detail::runtime_contains_bytes;
using nift::detail::runtime_bytes_string;
using nift::detail::nift_atomic_add_sub_checked;

Parser::FfiLibraryInstance::~FfiLibraryInstance() {
    if (!handle || closed) return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

namespace {

thread_local Parser* nift_ffi_callback_parser = nullptr;
thread_local std::string nift_ffi_callback_tag;
thread_local std::string nift_ffi_callback_error;
extern "C" std::int64_t nift_ffi_callback_i64_trampoline(std::int64_t x) {
    if (!nift_ffi_callback_parser) return std::numeric_limits<std::int64_t>::min();
    std::int64_t out = 0; std::string error;
    if (!nift_ffi_callback_parser->invoke_ffi_callback_i64(nift_ffi_callback_tag, x, out, error)) { nift_ffi_callback_error = std::move(error); return std::numeric_limits<std::int64_t>::min(); }
    return out;
}

class NiftFfiCallbackActivation {
public:
    ~NiftFfiCallbackActivation() {
        if (!active_) return;
        nift_ffi_callback_parser = old_parser_;
        nift_ffi_callback_tag = std::move(old_tag_);
        nift_ffi_callback_error = std::move(old_error_);
    }

    bool activate(Parser* parser, const std::string& tag, std::string& error) {
        if (nift_ffi_callback_parser) {
            error = "ffi_call: nested native callback activation is not supported";
            return false;
        }
        old_parser_ = nift_ffi_callback_parser;
        old_tag_ = std::move(nift_ffi_callback_tag);
        old_error_ = std::move(nift_ffi_callback_error);
        nift_ffi_callback_parser = parser;
        nift_ffi_callback_tag = tag;
        nift_ffi_callback_error.clear();
        active_ = true;
        return true;
    }

    const std::string& error() const { return nift_ffi_callback_error; }

private:
    Parser* old_parser_ = nullptr;
    std::string old_tag_;
    std::string old_error_;
    bool active_ = false;
};
// Filesystem-root restriction (NIFT_FS_ROOT): true when the standalone script
// path is inside the configured root (or no root is set / not standalone).
static bool nift_fs_root_allowed(const fs::path& p, bool standalone, std::string& err){
    const char* r = std::getenv("NIFT_FS_ROOT");
    if (!r || !*r || !standalone) return true;
    const fs::path root = fs::absolute(fs::path(r)).lexically_normal();
    if (filesystem::path_within(root, p)) return true;
    err = "path escapes configured filesystem root: " + p.generic_string();
    return false;
}
}  // namespace

using nift::detail::append_indented;
using nift::detail::built_in_metadata_name;
using nift::detail::compare_sort_keys;
using nift::detail::entity;
using nift::detail::glob_component_match;
using nift::detail::glob_expand;
using nift::detail::glob_has_magic;
using nift::detail::insertion_indent;
using nift::detail::make_loop_metadata;
using nift::detail::normalize_control_block_body;
using nift::detail::numeric_exponent_sign;
using nift::detail::parse_callable_parameters;
using nift::detail::parse_for_collection_clause;
using nift::detail::parse_parameters;
using nift::detail::reserved_binding_name;
using nift::detail::sortable_scalar;
using nift::detail::strip_presentation_chain;
using nift::detail::unescape_parameter_string;
using nift::detail::valid_binding_identifier;


// Nift source numeric literals preserve their lexical int/double form: 0, 8
// are int; 0.0, 8.5, 1e3 are double even when the value is integral. An
// arithmetic expression containing any double-typed operand is double; binding
// references propagate their inferred type so `total = total + value` stays
// double even when the accumulated value is integral. Everything else
// (computed expressions, JSON, injection) keeps value-based classification; a
// parsed JSON number keeps its established representation.
int Parser::nift_binding_type_from_text(const std::string& source,
                                        const nift::RuntimeValue& value) const {
    const int t = expression_type(source);
    if (t == 3) return 3;
    if (t == 2) return nift_binding_type(value);
    if (t >= 0 && t != 2 && t != 3) return t;
    return nift_binding_type(value);
}

int Parser::expression_type(const std::string& source) const {
    std::string t = [&]() {
        const auto f = source.find_first_not_of(" \t\r\n");
        if (f == std::string::npos) return std::string{};
        const auto l = source.find_last_not_of(" \t\r\n");
        return source.substr(f, l - f + 1);
    }();
    bool wrapped = true;
    while (wrapped && t.size() >= 2 && t.front() == '(' && t.back() == ')') {
        wrapped = false;
        int depth = 0;
        bool fully_enclosed = true;
        for (std::size_t i = 0; i < t.size(); ++i) {
            if (t[i] == '(') ++depth;
            else if (t[i] == ')') {
                --depth;
                if (depth == 0 && i + 1 != t.size()) { fully_enclosed = false; break; }
                if (depth < 0) { fully_enclosed = false; break; }
            }
        }
        if (fully_enclosed && depth == 0) {
            t = t.substr(1, t.size() - 2);
            wrapped = true;
        }
    }
    if (t.empty()) return -1;

    char* end = nullptr;
    std::strtod(t.c_str(), &end);
    if (end && *end == '\0') {
        if (t.find('.') != std::string::npos ||
            t.find('e') != std::string::npos ||
            t.find('E') != std::string::npos)
            return 3;
        return 2;
    }

    auto find_top_level_binary = [&](const std::string& ops) -> std::size_t {
        bool quoted = false; char quote = 0; int parens = 0; int brackets = 0;
        for (std::size_t i = t.size(); i-- > 0;) {
            char c = t[i];
            if (quoted) { if (c == quote && (i == 0 || t[i - 1] != '\\')) quoted = false; continue; }
            if (c == '\'' || c == '"') { quoted = true; quote = c; continue; }
            if (c == ']') { ++brackets; continue; }
            if (c == '[') { if (brackets) --brackets; continue; }
            if (brackets) continue;
            if (c == ')') { ++parens; continue; }
            if (c == '(') { if (parens) --parens; continue; }
            if (parens || ops.find(c) == std::string::npos) continue;
            if ((c == '+' || c == '-') && (i == 0 || std::string("+-*/%(<>=!&|?:,").find(t[i - 1]) != std::string::npos)) continue;
            return i;
        }
        return std::string::npos;
    };

    std::size_t pos = find_top_level_binary("+-");
    if (pos == std::string::npos) pos = find_top_level_binary("*/%");
    if (pos != std::string::npos) {
        const int lt = expression_type(t.substr(0, pos));
        const int rt = expression_type(t.substr(pos + 1));
        if (lt == 3 || rt == 3) return 3;
        if (lt == 2 && rt == 2) return 2;
        return -1;
    }

    std::size_t root_len = 0;
    while (root_len < t.size() &&
           (std::isalnum(static_cast<unsigned char>(t[root_len])) || t[root_len] == '_')) ++root_len;
    if (root_len == 0) return -1;

    auto binding_type_for = [&](const VariableBinding& b, const std::string& path) -> int {
        if (path.empty()) return b.type;
        if (!b.value || !b.value->is_string() ||
            b.value->string.rfind("\x1fnift:struct:", 0) != 0 || path[0] != '.') return -1;
        const auto it = struct_instances_.find(b.value->string.substr(13));
        if (it == struct_instances_.end()) return -1;
        std::shared_ptr<StructInstance> current = it->second;
        std::size_t mp = 1;
        int type = -1;
        while (true) {
            std::size_t me = mp;
            while (me < path.size() &&
                   (std::isalnum(static_cast<unsigned char>(path[me])) || path[me] == '_')) ++me;
            const std::string member = path.substr(mp, me - mp);
            if (member.empty()) return -1;
            const auto fit = current->fields.find(member);
            if (fit == current->fields.end()) return -1;
            type = fit->second.type;
            if (me == path.size()) return type;
            if (path[me] != '.' || !fit->second.value || !fit->second.value->is_string() ||
                fit->second.value->string.rfind("\x1fnift:struct:", 0) != 0) return -1;
            const auto next = struct_instances_.find(fit->second.value->string.substr(13));
            if (next == struct_instances_.end()) return -1;
            current = next->second;
            mp = me + 1;
        }
    };

    for (auto scope = variable_scopes_.rbegin(); scope != variable_scopes_.rend(); ++scope) {
        const auto it = scope->find(t.substr(0, root_len));
        if (it != scope->end())
            return binding_type_for(it->second, t.substr(root_len));
    }
    if (!receiver_stack_.empty()) {
        const auto rf = receiver_stack_.back()->fields.find(t.substr(0, root_len));
        if (rf != receiver_stack_.back()->fields.end())
            return binding_type_for(rf->second, t.substr(root_len));
    }
    return -1;
}

// Rejects mutations that would create a direct or indirect cycle through
// user-visible reference graphs. Adding an edge container->target creates a
// cycle iff target can already reach container through existing struct-field
// and collection-value references. Callable references are leaves (their
// captured lexical environments are internal, not user-visible graph edges),
// and array values are plain copies, so neither participates in the walk.
bool Parser::reference_would_cycle(const std::string& target_ref, const std::string& container_ref) const {
    std::unordered_set<std::string> visited;
    std::function<bool(const std::string&)> reach;
    reach = [&](const std::string& ref)->bool {
        if (ref == container_ref) return true;
        if (!visited.insert(ref).second) return false;
        if (ref.rfind("\x1fnift:struct:", 0) == 0) {
            auto si = struct_instances_.find(ref.substr(13));
            if (si != struct_instances_.end()) for (const auto& f : si->second->fields) { const auto& v = f.second.value; if (v && v->is_string() && (v->string.rfind("\x1fnift:struct:", 0) == 0 || v->string.rfind("\x1fnift:collection:", 0) == 0)) if (reach(v->string)) return true; }
        } else if (ref.rfind("\x1fnift:collection:", 0) == 0) {
            auto ci = collection_instances_.find(ref.substr(17));
            if (ci != collection_instances_.end()) {
                for (const auto& val : ci->second->values) if (val.is_string() && (val.string.rfind("\x1fnift:struct:", 0) == 0 || val.string.rfind("\x1fnift:collection:", 0) == 0)) if (reach(val.string)) return true;
                for (const auto& e : ci->second->entries) if (e.second.is_string() && (e.second.string.rfind("\x1fnift:struct:", 0) == 0 || e.second.string.rfind("\x1fnift:collection:", 0) == 0)) if (reach(e.second.string)) return true;
            }
        }
        return false;
    };
    return reach(target_ref);
}

bool Parser::eval_expression(const std::string& expression, nift::RuntimeValue& value, std::string& error) {
    standalone_script_host_ = true;
    return evaluate_expression(expression, value, error);
}

bool Parser::scalar_literal(const std::string& text, nift::RuntimeValue& value, std::string& error) const {
    const std::string trimmed = trim_copy(text);
    if (trimmed.empty()) {
        error = "expected a value in @if condition";
        return false;
    }

    if ((trimmed.front() == '"' && trimmed.back() == '"') ||
        (trimmed.front() == '\'' && trimmed.back() == '\'')) {
        // The value is a single quoted string only when the first unescaped
        // closing quote is the final character. Otherwise the surrounding quotes
        // merely delimit the first of several tokens (for example a string
        // concatenation such as "a" + "b") and must not be treated as one literal.
        const char closing_quote = trimmed.front();
        std::size_t closing = std::string::npos;
        for (std::size_t probe = 1; probe < trimmed.size(); ++probe) {
            if (trimmed[probe] == '\\' && probe + 1 < trimmed.size()) { ++probe; continue; }
            if (trimmed[probe] == closing_quote) { closing = probe; break; }
        }
        if (closing == std::string::npos || closing != trimmed.size() - 1) {
            error = "quoted literal contains trailing content or an unclosed quote";
            return false;
        }
        std::string result;
        for (std::size_t i = 1; i + 1 < trimmed.size(); ++i) {
            if (trimmed[i] == '\\' && i + 2 < trimmed.size()) {
                const char escaped = trimmed[++i];
                switch (escaped) {
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    case '\\': result += '\\'; break;
                    case '"': result += '"'; break;
                    case '\'': result += '\''; break;
                    default: result += escaped; break;
                }
            } else {
                result += trimmed[i];
            }
        }
        value = nift::RuntimeValue(result);
        return true;
    }

    if (trimmed == "true") { value = nift::RuntimeValue(true); return true; }
    if (trimmed == "false") { value = nift::RuntimeValue(false); return true; }
    if (trimmed == "null") { value = nift::RuntimeValue(nullptr); return true; }

    const bool floating_literal = trimmed.find('.') != std::string::npos || trimmed.find('e') != std::string::npos || trimmed.find('E') != std::string::npos;
    if (!floating_literal) {
        std::int64_t integer = 0;
        const auto parsed = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), integer);
        if (parsed.ec == std::errc() && parsed.ptr == trimmed.data() + trimmed.size()) {
            value = nift::RuntimeValue(static_cast<double>(integer));
            if (integer > 9007199254740992LL || integer < -9007199254740992LL) {
                value.type = nift::RuntimeType::StrNumber;
                value.string = trimmed;
            }
            return true;
        }
        if (parsed.ec == std::errc::result_out_of_range) { error = "integer literal outside signed 64-bit range"; return false; }
    }
    char* end = nullptr;
    const double number = std::strtod(trimmed.c_str(), &end);
    if (floating_literal && end && *end == '\0' && std::isfinite(number)) {
        if (trimmed.front() != '.' && trimmed.rfind("-.", 0) != 0) {
            std::string parse_error;
            const std::string json_number = trimmed.front() == '+' ? trimmed.substr(1) : trimmed;
            if (parse_runtime_json(json_number, value, parse_error) && value.is_number()) return true;
        }
        value = nift::RuntimeValue(number); return true;
    }

    error = "expected JSON path or scalar literal in @if condition: " + trimmed;
    return false;
}

std::string Parser::render_expression_value(const nift::RuntimeValue& value) const {
    if (value.is_string() && value.string.rfind("\x1fnift:enum:",0)==0) { const auto last=value.string.rfind(':'); const auto prev=last==std::string::npos?last:value.string.rfind(':',last-1); if(prev!=std::string::npos&&last!=std::string::npos)return value.string.substr(prev+1,last-prev-1); }
    if (value.is_string() && value.string.rfind("\x1fnift:atomic:",0)==0) { auto it=atomic_instances_.find(value.string.substr(13)); if(it!=atomic_instances_.end()){ if(it->second->kind==AtomicInstance::Kind::Bool) return it->second->bool_value.load()?"true":"false"; const auto v=it->second->int_value.load(); return std::to_string(v); } }
    if (value.is_timer()) throw std::runtime_error("timer values cannot be rendered as text");
    if (value.is_string()) return value.string;
    if (value.is_bytes()) throw std::runtime_error("bytes values cannot be rendered as text");
    return value.dump(0);
}

bool Parser::serialize_value(const nift::RuntimeValue& value, bool pretty, std::string& output, std::string& error, int depth) const {
    if(value.is_string()&&value.string.rfind("\x1fnift:enum:",0)==0){const auto p=value.string.rfind(':');if(p==std::string::npos){error="invalid enum value";return false;}output+=value.string.substr(p+1);return true;}
    if (depth > 128) { error = "value serialization depth exceeded"; return false; }
    if (value.is_bytes()) { error = "bytes values are not serializable"; return false; }
    if (value.is_timer()) { error = "timer values are not serializable"; return false; }
    const std::string pad(pretty ? static_cast<std::size_t>(depth * 2) : 0, ' ');
    const std::string child_pad(pretty ? static_cast<std::size_t>((depth + 1) * 2) : 0, ' ');
    auto append_sequence = [&](const std::vector<nift::RuntimeValue>& values, const std::string& open, const std::string& close, std::string& dst)->bool {
        dst += open;
        if (pretty && !values.empty()) dst += "\n";
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (pretty) dst += child_pad;
            std::string item;
            if (!serialize_value(values[i], pretty, item, error, depth + 1)) return false;
            dst += item;
            if (i + 1 < values.size()) dst += ",";
            if (pretty) dst += "\n";
        }
        if (pretty && !values.empty()) dst += pad;
        dst += close;
        return true;
    };
    if (value.is_null() || value.is_bool() || value.is_number()) { output += value.dump(0); return true; }
    if (value.is_array()) return append_sequence(value.array, "[", "]", output);
    if (value.is_object()) {
        output += "{";
        if (pretty && !value.object.empty()) output += "\n";
        for (std::size_t i = 0; i < value.object.size(); ++i) {
            if (pretty) output += child_pad;
            output += nift::RuntimeValue(value.object[i].first).dump(0);
            output += pretty ? ": " : ":";
            std::string item;
            if (!serialize_value(value.object[i].second, pretty, item, error, depth + 1)) return false;
            output += item;
            if (i + 1 < value.object.size()) output += ",";
            if (pretty) output += "\n";
        }
        if (pretty && !value.object.empty()) output += pad;
        output += "}";
        return true;
    }
    if (!value.is_string()) { error = "value is not serializable"; return false; }
    const std::string& ref = value.string;
    if (ref.rfind("\x1fnift:stream:", 0) == 0 || ref.rfind("\x1fnift:callable:", 0) == 0 || ref.rfind("\x1fnift:file:", 0) == 0) {
        error = "opaque runtime value is not serializable"; return false;
    }
    if (ref.rfind("\x1fnift:collection:", 0) == 0) {
        auto it = collection_instances_.find(ref.substr(17));
        if (it == collection_instances_.end()) { error = "invalid collection value"; return false; }
        const auto& c = *it->second;
        std::string name;
        switch (c.kind) {
            case CollectionKind::Stack: name="stack"; break; case CollectionKind::Queue: name="queue"; break;
            case CollectionKind::PriQue: name="prique"; break; case CollectionKind::Map: name="map"; break;
            case CollectionKind::SortedMap: name="sorted_map"; break; case CollectionKind::Set: name="set"; break;
            case CollectionKind::SortedSet: name="sorted_set"; break;
        }
        output += name + "(";
        if (c.kind == CollectionKind::Map || c.kind == CollectionKind::SortedMap) {
            output += "[";
            if (pretty && !c.entries.empty()) output += "\n";
            for (std::size_t i=0;i<c.entries.size();++i) {
                if (pretty) output += child_pad;
                output += "[";
                std::string k,v;
                if (!serialize_value(c.entries[i].first, pretty, k, error, depth+1) || !serialize_value(c.entries[i].second, pretty, v, error, depth+1)) return false;
                output += k + (pretty ? ", " : ",") + v + "]";
                if (i+1<c.entries.size()) output += ",";
                if (pretty) output += "\n";
            }
            if (pretty && !c.entries.empty()) output += pad;
            output += "]";
        } else {
            if (!append_sequence(c.values, "[", "]", output)) return false;
        }
        output += ")";
        return true;
    }
    if (ref.rfind("\x1fnift:struct:", 0) == 0) {
        auto it=struct_instances_.find(ref.substr(13));
        if(it==struct_instances_.end()){error="invalid struct value";return false;}
        auto sd=structs_.find(it->second->type_name);
        output += it->second->type_name + "{";
        bool first=true;
        for(const auto& field : sd->second.fields){
            if(field.private_member) continue;
            auto fv=it->second->fields.find(field.name); if(fv==it->second->fields.end()) continue;
            if(pretty){output += first?"\n":""; output += child_pad;} else if(!first) output += ",";
            output += field.name + (pretty?": ":":");
            std::string item; if(!serialize_value(*fv->second.value,pretty,item,error,depth+1))return false; output += item;
            if(pretty) output += ",\n";
            first=false;
        }
        if(pretty&&!first) output += pad;
        output += "}";
        return true;
    }
    output += value.dump(0);
    return true;
}

bool Parser::evaluate_collection_value(const std::string& expression, nift::RuntimeValue& value, std::string& error) {
    const std::string text = trim_copy(expression);
    if (text.empty()) { error = "collection expression cannot be empty"; return false; }
    if (text.front() != '@') return evaluate_expression(text, value, error);

    std::size_t name_end = 1;
    while (name_end < text.size() && std::islower(static_cast<unsigned char>(text[name_end]))) ++name_end;
    if (name_end == 1 || name_end >= text.size() || text[name_end] != '(') { error = "malformed collection operation: " + text; return false; }
    const std::string function = text.substr(1, name_end - 1);
    const bool supported = function == "filter" || function == "map" || function == "sort" || function == "slice" ||
        function == "find" || function == "some" || function == "every" || function == "distinct" || function == "reverse" ||
        function == "sum" || function == "prod" || function == "min" || function == "max" || function == "reduce";
    if (!supported) { error = "unsupported collection operation: @" + function; return false; }

    std::size_t close = 0;
    if (!find_balanced(text, name_end, '(', ')', close) || close + 1 != text.size()) { error = "malformed @" + function + " call"; return false; }
    const std::string body = trim_copy(text.substr(name_end + 1, close - name_end - 1));

    auto truthy = [](const nift::RuntimeValue& d) { return nift::runtime_truthy(d); };
    auto collection_arg = [&](const std::string& raw, nift::RuntimeValue& out) {
        if (!evaluate_collection_value(raw, out, error)) return false;
        if (!out.is_array()) { error = function + ": collection must resolve to an array"; return false; }
        return true;
    };
    auto find_top_level = [&](const std::string& raw, const std::string& needle) -> std::size_t {
        bool quoted = false; char quote = 0; int parens = 0, brackets = 0;
        for (std::size_t i = 0; i + needle.size() <= raw.size(); ++i) {
            const char c = raw[i];
            if (quoted) { if (c == '\\' && i + 1 < raw.size()) ++i; else if (c == quote) quoted = false; continue; }
            if (c == '\'' || c == '"') { quoted = true; quote = c; continue; }
            if (c == '(') { ++parens; continue; }
            if (c == ')') { if (parens) --parens; continue; }
            if (c == '[') { ++brackets; continue; }
            if (c == ']') { if (brackets) --brackets; continue; }
            if (!parens && !brackets && raw.compare(i, needle.size(), needle) == 0) return i;
        }
        return std::string::npos;
    };
    auto split_binding = [&](const std::string& raw, std::string& binding_text, std::string& collection_text) {
        const auto pos = find_top_level(raw, ":");
        if (pos == std::string::npos) return false;
        binding_text = trim_copy(raw.substr(0, pos));
        collection_text = trim_copy(raw.substr(pos + 1));
        return !binding_text.empty() && !collection_text.empty();
    };
    auto parse_bindings = [&](const std::string& raw, std::vector<std::string>& bindings) {
        std::string binding = trim_copy(raw);
        if (binding.size() >= 2 && binding.front() == '(' && binding.back() == ')') {
            bool ok = false;
            bindings = parse_parameters(binding.substr(1, binding.size() - 2), ok);
            if (!ok || bindings.size() < 2) { error = function + ": tuple binding requires at least two identifiers"; return false; }
            for (auto& name : bindings) name = trim_copy(name);
        } else {
            bindings = {binding};
        }
        std::unordered_set<std::string> seen;
        for (const auto& name : bindings) {
            if (!valid_binding_identifier(name)) { error = function + ": binding must be an identifier"; return false; }
            if (reserved_binding_name(name) || host_.is_contract_name(name)) { error = function + ": binding conflicts with a reserved namespace: " + name; return false; }
            if (!seen.insert(name).second) { error = function + ": bindings must be distinct identifiers"; return false; }
        }
        return true;
    };
    auto with_bindings = [&](const std::vector<std::string>& bindings, const std::shared_ptr<const nift::RuntimeValue>& root,
                             const nift::RuntimeValue* element, const std::function<bool()>& action) {
        std::vector<std::pair<bool, std::shared_ptr<const nift::RuntimeValue>>> previous;
        previous.reserve(bindings.size());
        auto restore = [&]() {
            for (std::size_t i = 0; i < bindings.size(); ++i) {
                if (previous[i].first) json_bindings_[bindings[i]] = previous[i].second;
                else json_bindings_.erase(bindings[i]);
            }
        };
        for (const auto& name : bindings) {
            const auto it = json_bindings_.find(name);
            previous.push_back({it != json_bindings_.end(), it != json_bindings_.end() ? it->second : nullptr});
        }
        if (bindings.size() == 1) {
            json_bindings_[bindings[0]] = std::shared_ptr<const nift::RuntimeValue>(root, element);
        } else {
            if (!element->is_array() || element->array.size() != bindings.size()) {
                error = function + ": tuple binding arity must match each array item";
                return false;
            }
            for (std::size_t i = 0; i < bindings.size(); ++i)
                json_bindings_[bindings[i]] = std::shared_ptr<const nift::RuntimeValue>(root, &element->array[i]);
        }
        const bool ok = action();
        restore();
        return ok;
    };
    auto comparable = [&](const nift::RuntimeValue& a, const nift::RuntimeValue& b, int& result) {
        if (!((a.is_number() && b.is_number()) || (a.is_string() && b.is_string()))) {
            error = function + ": values must be numbers or strings of the same type";
            return false;
        }
        if (a.is_number()) result = nift::runtime_compare_numbers(a, b);
        else result = a.string < b.string ? -1 : (a.string > b.string ? 1 : 0);
        return true;
    };

    // Simple forms use the ordinary comma-separated parameter grammar.
    if (function == "slice") {
        bool params_ok = false;
        auto params = parse_parameters(body, params_ok);
        if (!params_ok || params.size() != 3) { error = "slice: expected collection, position and length"; return false; }
        nift::RuntimeValue source; if (!collection_arg(params[0], source)) return false;
        auto index = [&](const std::string& raw, std::size_t& out, const char* label) {
            nift::RuntimeValue v; if (!evaluate_expression(raw, v, error)) return false;
            if (!nift::runtime_number_to_size(v, out)) { error = std::string("slice: ") + label + " must be a non-negative integer"; return false; }
            return true;
        };
        std::size_t pos = 0, len = 0; if (!index(params[1], pos, "position") || !index(params[2], len, "length")) return false;
        value = nift::RuntimeValue::make_array();
        const std::size_t end = std::min(source.array.size(), pos > source.array.size() ? source.array.size() : pos + std::min(len, source.array.size() - pos));
        if (pos < source.array.size()) value.array.insert(value.array.end(), source.array.begin() + pos, source.array.begin() + end);
        return true;
    }
    if (function == "reverse" || function == "distinct") {
        bool params_ok = false; auto params = parse_parameters(body, params_ok);
        if (!params_ok || params.size() != 1) { error = function + ": expected one collection"; return false; }
        nift::RuntimeValue source; if (!collection_arg(params[0], source)) return false;
        value = nift::RuntimeValue::make_array();
        if (function == "reverse") { value.array.assign(source.array.rbegin(), source.array.rend()); return true; }
        std::unordered_map<std::string, std::vector<nift::RuntimeValue>> seen;
        for (const auto& item : source.array) {
            auto& bucket = seen[nift::runtime_fingerprint(item)];
            bool duplicate = false;
            for (const auto& prior : bucket) {
                if (nift::runtime_equal(prior, item)) { duplicate = true; break; }
            }
            if (!duplicate) { bucket.push_back(item); value.array.push_back(item); }
        }
        return true;
    }
    if ((function == "sort" || function == "sum" || function == "prod" || function == "min" || function == "max") && find_top_level(body, "=>") == std::string::npos) {
        bool params_ok = false; auto params = parse_parameters(body, params_ok);
        if (!params_ok || params.size() != 1) { error = function + ": expected one collection or binding : collection => expression"; return false; }
        nift::RuntimeValue source; if (!collection_arg(params[0], source)) return false;
        if (function == "sort") {
            value = source; if (value.array.empty()) return true;
            const bool numeric = value.array.front().is_number();
            if (!numeric && !value.array.front().is_string()) { error = "sort: simple form requires an array of numbers or strings"; return false; }
            for (const auto& item : value.array) if (numeric ? !item.is_number() : !item.is_string()) { error = "sort: values must all have the same sortable type"; return false; }
            std::stable_sort(value.array.begin(), value.array.end(), [](const auto& a, const auto& b) { return a.is_number() ? nift::runtime_compare_numbers(a, b) < 0 : a.string < b.string; });
            return true;
        }
        if (function == "sum" || function == "prod") {
            double aggregate = function == "sum" ? 0.0 : 1.0;
            for (const auto& item : source.array) {
                if (!item.is_number()) { error = function + ": values must be numeric"; return false; }
                aggregate = function == "sum" ? aggregate + item.num : aggregate * item.num;
                if (!std::isfinite(aggregate)) { error = function + ": result is not finite"; return false; }
            }
            value = nift::RuntimeValue(aggregate); return true;
        }
        if (source.array.empty()) { error = function + ": cannot aggregate an empty collection"; return false; }
        value = source.array.front();
        if (!value.is_number() && !value.is_string()) { error = function + ": values must be numbers or strings"; return false; }
        for (std::size_t i = 1; i < source.array.size(); ++i) {
            int ordering = 0; if (!comparable(source.array[i], value, ordering)) return false;
            if ((function == "min" && ordering < 0) || (function == "max" && ordering > 0)) value = source.array[i];
        }
        return true;
    }

    const auto arrow = find_top_level(body, "=>");
    if (arrow == std::string::npos) {
        // Before release the advanced comma form is deliberately rejected rather than retained as an alias.
        error = function + ": advanced form requires '=>' between binding/source and expression";
        return false;
    }
    const std::string left = trim_copy(body.substr(0, arrow));
    std::string expr = trim_copy(body.substr(arrow + 2));
    if (expr.empty()) { error = function + ": expression cannot be empty"; return false; }

    if (function == "reduce") {
        const auto amp = find_top_level(left, "&");
        if (amp == std::string::npos) { error = "reduce: expected binding : collection & accumulator = initial => expression"; return false; }
        std::string binding_text, source_text;
        if (!split_binding(trim_copy(left.substr(0, amp)), binding_text, source_text)) { error = "reduce: expected binding : collection"; return false; }
        const std::string accumulator_clause = trim_copy(left.substr(amp + 1));
        const auto equals = find_top_level(accumulator_clause, "=");
        if (equals == std::string::npos) { error = "reduce: expected accumulator = initial expression"; return false; }
        const std::string accumulator = trim_copy(accumulator_clause.substr(0, equals));
        const std::string initial = trim_copy(accumulator_clause.substr(equals + 1));
        std::vector<std::string> bindings;
        if (!parse_bindings(binding_text, bindings)) return false;
        if (!valid_binding_identifier(accumulator) || reserved_binding_name(accumulator) || host_.is_contract_name(accumulator)) { error = "reduce: accumulator must be a non-reserved identifier"; return false; }
        if (std::find(bindings.begin(), bindings.end(), accumulator) != bindings.end()) { error = "reduce: accumulator must be distinct from item bindings"; return false; }
        nift::RuntimeValue source; if (!collection_arg(source_text, source)) return false;
        nift::RuntimeValue accumulator_value; if (!evaluate_expression(initial, accumulator_value, error)) return false;
        auto root = std::make_shared<const nift::RuntimeValue>(source);
        const auto old_acc = json_bindings_.find(accumulator); const bool had_acc = old_acc != json_bindings_.end(); const auto previous_acc = had_acc ? old_acc->second : nullptr;
        for (std::size_t i = 0; i < root->array.size(); ++i) {
            auto acc_root = std::make_shared<const nift::RuntimeValue>(accumulator_value);
            json_bindings_[accumulator] = acc_root;
            nift::RuntimeValue next;
            const bool ok = with_bindings(bindings, root, &root->array[i], [&] { return evaluate_expression(expr, next, error); });
            if (!ok) { if (had_acc) json_bindings_[accumulator] = previous_acc; else json_bindings_.erase(accumulator); return false; }
            accumulator_value = std::move(next);
        }
        if (had_acc) json_bindings_[accumulator] = previous_acc; else json_bindings_.erase(accumulator);
        value = std::move(accumulator_value); return true;
    }

    std::string binding_text, source_text;
    if (!split_binding(left, binding_text, source_text)) { error = function + ": expected binding : collection => expression"; return false; }
    std::vector<std::string> bindings;
    if (!parse_bindings(binding_text, bindings)) return false;
    nift::RuntimeValue source; if (!collection_arg(source_text, source)) return false;
    auto root = std::make_shared<const nift::RuntimeValue>(source);

    bool descending = false;
    if (function == "sort") {
        if (expr.size() > 5 && expr.compare(expr.size() - 5, 5, " desc") == 0) { descending = true; expr = trim_copy(expr.substr(0, expr.size() - 5)); }
        else if (expr.size() > 4 && expr.compare(expr.size() - 4, 4, " asc") == 0) expr = trim_copy(expr.substr(0, expr.size() - 4));
    }
    if (function == "filter" || function == "map") value = nift::RuntimeValue::make_array();
    if (function == "some") value = nift::RuntimeValue(false);
    if (function == "every") value = nift::RuntimeValue(true);
    if (function == "find") value = nift::RuntimeValue(nullptr);
    if (function == "sum" || function == "prod") value = nift::RuntimeValue(function == "sum" ? 0.0 : 1.0);
    bool have_extreme = false;
    std::vector<nift::RuntimeValue> keys; if (function == "sort") keys.reserve(root->array.size());

    for (std::size_t i = 0; i < root->array.size(); ++i) {
        nift::RuntimeValue evaluated;
        const bool ok = with_bindings(bindings, root, &root->array[i], [&] { return evaluate_expression(expr, evaluated, error); });
        if (!ok) return false;
        if (function == "filter") { if (truthy(evaluated)) value.array.push_back(root->array[i]); }
        else if (function == "map") value.array.push_back(std::move(evaluated));
        else if (function == "find") { if (truthy(evaluated)) { value = root->array[i]; return true; } }
        else if (function == "some") { if (truthy(evaluated)) { value = nift::RuntimeValue(true); return true; } }
        else if (function == "every") { if (!truthy(evaluated)) { value = nift::RuntimeValue(false); return true; } }
        else if (function == "sum" || function == "prod") {
            if (!evaluated.is_number()) { error = function + ": expression must produce numeric values"; return false; }
            const double next = function == "sum" ? value.num + evaluated.num : value.num * evaluated.num;
            if (!std::isfinite(next)) { error = function + ": result is not finite"; return false; }
            value.num = next;
        } else if (function == "min" || function == "max") {
            if (!evaluated.is_number() && !evaluated.is_string()) { error = function + ": expression must produce numbers or strings"; return false; }
            if (!have_extreme) { value = evaluated; have_extreme = true; }
            else { int ordering = 0; if (!comparable(evaluated, value, ordering)) return false; if ((function == "min" && ordering < 0) || (function == "max" && ordering > 0)) value = evaluated; }
        } else keys.push_back(std::move(evaluated));
    }
    if (function != "sort") {
        if ((function == "min" || function == "max") && !have_extreme) { error = function + ": cannot aggregate an empty collection"; return false; }
        return true;
    }
    if (keys.empty()) { value = source; return true; }
    const bool numeric = keys.front().is_number();
    if (!numeric && !keys.front().is_string()) { error = "sort: keys must be numbers or strings"; return false; }
    for (const auto& key : keys) if (numeric ? !key.is_number() : !key.is_string()) { error = "sort: keys must all have the same type"; return false; }
    std::vector<std::size_t> order(keys.size()); for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
        const bool less = numeric ? nift::runtime_compare_numbers(keys[a], keys[b]) < 0 : keys[a].string < keys[b].string;
        const bool greater = numeric ? nift::runtime_compare_numbers(keys[a], keys[b]) > 0 : keys[a].string > keys[b].string;
        return descending ? greater : less;
    });
    value = nift::RuntimeValue::make_array(); for (auto index : order) value.array.push_back(root->array[index]); return true;
}

bool Parser::evaluate_expression(const std::string& expression, nift::RuntimeValue& value, std::string& error) {
    last_expression_mutation_ = false;
    auto resolve_direct = [&](const std::string& raw, nift::RuntimeValue& out) -> bool {
        const std::string text = trim_copy(raw);
        const bool path_only = [&] {
            std::size_t pos = 0;
            while (pos < text.size() &&
                   (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_')) ++pos;
            if (pos == 0) return false;
            while (pos < text.size()) {
                if (text[pos] == '.') {
                    ++pos;
                    const std::size_t member_start = pos;
                    while (pos < text.size() &&
                           (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_')) ++pos;
                    if (member_start == pos) return false;
                } else if (text[pos] == '[') {
                    std::size_t close = 0;
                    if (!find_balanced(text, pos, '[', ']', close)) return false;
                    pos = close + 1;
                } else {
                    return false;
                }
            }
            return true;
        }();
        for (auto scope = variable_scopes_.rbegin(); scope != variable_scopes_.rend(); ++scope) {
            const auto it = scope->find(text); if (it != scope->end()) { it->second.sync(); if(!it->second.value){error="reference target no longer exists: "+text;return false;} out = *it->second.value; return true; }
            std::size_t root_len = 0;
            while (root_len < text.size() &&
                   (std::isalnum(static_cast<unsigned char>(text[root_len])) || text[root_len] == '_')) ++root_len;
            if (root_len == 0 || root_len == text.size()) continue;
            auto root_it = scope->find(text.substr(0, root_len));
            if (root_it == scope->end()) continue;
            root_it->second.sync(); if(!root_it->second.value){error="reference target no longer exists: "+text;return false;}
            if (root_it->second.value && root_it->second.value->is_string() && root_it->second.value->string.rfind("\x1fnift:struct:",0)==0 && text[root_len]=='.') {
                nift::RuntimeValue current=*root_it->second.value; std::size_t mp=root_len+1;
                while(mp<text.size()){
                    std::size_t me=mp;while(me<text.size()&&(std::isalnum((unsigned char)text[me])||text[me]=='_'))++me;
                    const std::string member=text.substr(mp,me-mp);
                    if(member.empty()||!current.is_string()||current.string.rfind("\x1fnift:struct:",0)!=0){error="invalid struct member path: "+text;return false;}
                    auto inst=struct_instances_.find(current.string.substr(13));
                    if(inst==struct_instances_.end()){error="invalid struct instance";return false;}
                    auto fit=inst->second->fields.find(member);
                    if(fit==inst->second->fields.end()){
                        auto sd2=structs_.find(inst->second->type_name);
                        if(sd2!=structs_.end()&&sd2->second.methods.count(member)) return false;
                        error="struct has no field: "+member;return false;
                    }
                    auto sd=structs_.find(inst->second->type_name);bool priv=false;
                    if(sd!=structs_.end())for(const auto& f:sd->second.fields)if(f.name==member)priv=f.private_member;
                    if(priv&&(receiver_stack_.empty()||receiver_stack_.back()!=inst->second)){error="private struct field: "+member;return false;}
                    current=*fit->second.value;
                    if(me==text.size()){out=current;return true;}
                    if(text[me]!='.'){break;}
                    mp=me+1;
                }
            }
            if (root_it->second.value && root_it->second.value->is_string() && root_it->second.value->string.rfind("\x1fnift:page:",0)==0 && text[root_len]=='.') {
                // Hierarchy page-reference member path: page.parent,
                // page.children[0].url, page.ancestors.map(...). Only the
                // dot/index walk happens here; method calls on the resulting
                // collections are dispatched by the postfix machinery.
                nift::RuntimeValue current=*root_it->second.value; std::size_t mp=root_len+1;
                while(mp<text.size()){
                    std::size_t me=mp; while(me<text.size()&&(std::isalnum((unsigned char)text[me])||text[me]=='_'))++me;
                    if(me==mp){ if(text[mp]=='.'||text[mp]=='['){break;} error="invalid page member path: "+text;return false; }
                    const std::string member=text.substr(mp,me-mp);
                    if(current.is_string()&&current.string.rfind("\x1fnift:page:",0)==0){
                        result_.dependencies.insert(host_.relative(host_.root() / ".nift/hierarchy.fingerprint"));
                        nift::RuntimeValue resolved; std::string perr;
                        if(!host_.resolve_page_member(current.string.substr(11),member,resolved,perr)){error=perr.empty()?("page has no member: "+member):perr;return false;}
                        current=std::move(resolved);
                    } else if(current.is_object()){
                        if(!current.has(member)){error="page value '"+text+"' has no member '"+member+"'";return false;}
                        current=current[member];
                    } else if(current.is_array()){
                        error="cannot access member '"+member+"' on a page collection; select an element first";return false;
                    } else if(current.is_null()){
                        error="cannot access member '"+member+"' on null (no parent/ancestors)";return false;
                    } else {
                        error="cannot access member '"+member+"' on this page value";return false;
                    }
                    if(me==text.size()){out=std::move(current);return true;}
                    if(text[me]=='.'){mp=me+1;continue;}
                    if(text[me]=='['){
                        ++me; const std::size_t index_start=me;
                        while(me<text.size()&&std::isdigit((unsigned char)text[me]))++me;
                        if(index_start==me||me>=text.size()||text[me]!=']'){error="page index is malformed in '"+text+"'";return false;}
                        std::size_t index=0;try{index=(std::size_t)std::stoull(text.substr(index_start,me-index_start));}catch(...){error="page index is out of range in '"+text+"'";return false;}
                        ++me;
                        if(!current.is_array()||index>=current.array.size()){error="page index "+std::to_string(index)+" is out of range in '"+text+"'";return false;}
                        current=current.array[index];
                        if(me==text.size()){out=std::move(current);return true;}
                        if(text[me]=='.'){mp=me+1;continue;}
                        // A following operator/expression makes this a compound
                        // expression; fall through so the operator machinery
                        // evaluates the member chain and the rest separately.
                        return false;
                    }
                    // A following operator (e.g. `c.title != "x"`) makes this a
                    // compound expression; fall through rather than claiming the
                    // whole text as a page member path.
                    return false;
                }
                break;
            }
            root_it->second.sync();
            if(!root_it->second.value){ error="reference target no longer exists: "+text.substr(0,root_len); return false; }
            const nift::RuntimeValue* cur = root_it->second.value.get();
            std::size_t pos = root_len;
            bool walk_ok = true;
            while (pos < text.size()) {
                if (text[pos] == '.') {
                    ++pos; const std::size_t member_start = pos;
                    while (pos < text.size() &&
                           (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_')) ++pos;
                    if (member_start == pos) { walk_ok = false; break; }
                    const std::string member = text.substr(member_start, pos - member_start);
                    if (!cur->is_object() || !cur->has(member)) {
                        // Defer compound expressions to the operator/postfix
                        // machinery, which will resolve this member operand and
                        // produce the normal missing-member diagnostic.
                        if (path_only) error = "value has no member: " + member;
                        walk_ok = false;
                        break;
                    }
                    cur = &(*cur)[member];
                } else if (text[pos] == '[') {
                    if (!cur->is_array() && !cur->is_object() && !cur->is_bytes()) {
                        // Compound expressions retry the indexed operand on its
                        // own; pure paths should match the prepared AST error.
                        if (path_only) error = "invalid index";
                        walk_ok = false;
                        break;
                    }
                    ++pos; const std::size_t index_start = pos;
                    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
                    if (index_start == pos || pos >= text.size() || text[pos] != ']') { walk_ok = false; break; }
                    std::size_t index = 0;
                    try { index = static_cast<std::size_t>(std::stoull(text.substr(index_start, pos - index_start))); }
                    catch (...) { walk_ok = false; break; }
                    ++pos;
                    if (cur->is_bytes()) {
                        if (!cur->bytes || index >= cur->bytes->size() || pos != text.size()) { walk_ok = false; break; }
                        out = nift::RuntimeValue(static_cast<int>((*cur->bytes)[index])); return true;
                    }
                    if (!cur->is_array() || index >= cur->array.size()) { walk_ok = false; break; }
                    cur = &(*cur)[index];
                } else { walk_ok = false; break; }
            }
            if (walk_ok) { out = *cur; return true; }
            if (error.empty() && path_only && root_it->second.value &&
                (root_it->second.value->is_object() || root_it->second.value->is_array())) {
                std::shared_ptr<const nift::RuntimeValue> resolved;
                std::string path_error;
                if (resolve_json_value(text, resolved, path_error)) {
                    if (!path_error.empty()) { error = std::move(path_error); return false; }
                    out = *resolved;
                    return true;
                }
            }
            // Finding the root in this scope is authoritative even when the
            // remainder needs compound/postfix evaluation. Do not continue to
            // an outer scope, implicit receiver field, or another name source.
            return false;
        }
        // Inside a method, a bare name that matches a receiver field resolves to
        // that field (live instance storage, not a snapshot), including member
        // paths through struct/object/array field values.
        if (!receiver_stack_.empty()) {
            const auto rec = receiver_stack_.back()->fields.find(text);
            if (rec != receiver_stack_.back()->fields.end()) { out = *rec->second.value; return true; }
            std::size_t root_len = 0;
            while (root_len < text.size() &&
                   (std::isalnum(static_cast<unsigned char>(text[root_len])) || text[root_len] == '_')) ++root_len;
            if (root_len > 0 && root_len < text.size()) {
                auto rf = receiver_stack_.back()->fields.find(text.substr(0, root_len));
                if (rf != receiver_stack_.back()->fields.end() && rf->second.value) {
                    nift::RuntimeValue current = *rf->second.value;
                    std::size_t pos = root_len;
                    bool walk_ok = true;
                    while (pos < text.size()) {
                        if (current.is_string() && current.string.rfind("\x1fnift:struct:", 0) == 0) {
                            auto inst = struct_instances_.find(current.string.substr(13));
                            if (inst == struct_instances_.end() || text[pos] != '.') { error = "invalid struct member path: " + text; return false; }
                            ++pos; const std::size_t member_start = pos;
                            while (pos < text.size() &&
                                   (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_')) ++pos;
                            const std::string member = text.substr(member_start, pos - member_start);
                            if (member_start == pos) { error = "invalid struct member path: " + text; return false; }
                            auto fit = inst->second->fields.find(member);
                            if (fit == inst->second->fields.end()) { error = "struct has no field: " + member; return false; }
                            auto sd = structs_.find(inst->second->type_name);
                            bool priv = false;
                            if (sd != structs_.end())
                                for (const auto& f : sd->second.fields)
                                    if (f.name == member) priv = f.private_member;
                            if (priv && (receiver_stack_.empty() || receiver_stack_.back() != inst->second)) {
                                error = "private struct field: " + member;
                                return false;
                            }
                            current = *fit->second.value;
                        } else if (text[pos] == '.') {
                            ++pos; const std::size_t member_start = pos;
                            while (pos < text.size() &&
                                   (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_')) ++pos;
                            if (member_start == pos || !current.is_object() ||
                                !current.has(text.substr(member_start, pos - member_start))) { walk_ok = false; break; }
                            current = current[text.substr(member_start, pos - member_start)];
                        } else if (text[pos] == '[') {
                            ++pos; const std::size_t index_start = pos;
                            while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
                            if (index_start == pos || pos >= text.size() || text[pos] != ']') { walk_ok = false; break; }
                            std::size_t index = 0;
                            try { index = static_cast<std::size_t>(std::stoull(text.substr(index_start, pos - index_start))); }
                            catch (...) { walk_ok = false; break; }
                            ++pos;
                            if (!current.is_array() || index >= current.array.size()) { walk_ok = false; break; }
                            current = current.array[index];
                        } else { walk_ok = false; break; }
                    }
                    if (walk_ok) { out = current; return true; }
                }
            }
        }
        std::shared_ptr<const nift::RuntimeValue> document;
        if (resolve_pagination_value(text, document)) { out = *document; return true; }
        std::string local_error;
        if (resolve_json_value(text, document, local_error)) {
            if (!local_error.empty()) {
                const bool compound = text.find("&&") != std::string::npos || text.find("||") != std::string::npos ||
                    text.find("==") != std::string::npos || text.find("!=") != std::string::npos ||
                    text.find("??") != std::string::npos ||
                    text.find("<=") != std::string::npos || text.find(">=") != std::string::npos ||
                    text.find(" + ") != std::string::npos || text.find(" - ") != std::string::npos ||
                    text.find(" * ") != std::string::npos || text.find(" / ") != std::string::npos ||
                    text.find(" % ") != std::string::npos || text.find(" < ") != std::string::npos || text.find(" > ") != std::string::npos;
                if (!compound) { error = local_error; return false; }
            } else { out = *document; return true; }
        }
        if (built_in_metadata_name(text)) { out = nift::RuntimeValue(metadata(text)); return true; }
        nift::RuntimeValue literal;
        std::string literal_error;
        if (!text.empty() && (text.front() == '{' || text.front() == '[')) {
            // Only treat a self-contained balanced literal as a structured
            // literal. A '{'/'['-prefixed compound expression (e.g.
            // {"a":x}.a + "|" + {"a":x}.a) must fall through to the operator
            // machinery rather than raise a spurious JSON parse error.
            const char openc = text.front();
            const char closec = openc == '{' ? '}' : ']';
            std::size_t balanced = 0;
            if (find_balanced(text, 0, openc, closec, balanced) && balanced == text.size() - 1) {
                if (parse_runtime_json(text, literal, literal_error)) { out = std::move(literal); return true; }
                error = "invalid structured literal: " + literal_error;
                return false;
            }
            return false;
        }
        if (scalar_literal(text, literal, literal_error)) { if(literal.is_string()&&literal.string.find("$[")!=std::string::npos){std::string r,e;if(!interpolate_parameter(literal.string,r,e)){error=e;return false;}literal=nift::RuntimeValue(r);} out = std::move(literal); return true; }
        // A numeric-looking literal that failed only because its magnitude is
        // outside the signed 64-bit range deserves its precise diagnostic rather
        // than the generic unknown-expression fallback.
        if (literal_error.rfind("integer literal outside signed 64-bit range", 0) == 0) { error = literal_error; return false; }
        return false;
    };

    auto encloses = [](const std::string& text) {
        if (text.size() < 2 || text.front() != '(' || text.back() != ')') return false;
        bool quoted=false; char quote=0; int parens=0; int brackets=0;
        for (std::size_t i=0;i<text.size();++i) {
            char c=text[i];
            if (quoted) { if (c=='\\' && i+1<text.size()) ++i; else if (c==quote) quoted=false; continue; }
            if (c=='\'' || c=='"') { quoted=true; quote=c; continue; }
            if (c=='[') { ++brackets; continue; }
            if (c==']') { if (brackets) --brackets; continue; }
            if (brackets) continue;
            if (c=='(') ++parens;
            else if (c==')') { --parens; if (parens==0 && i+1!=text.size()) return false; if (parens<0) return false; }
        }
        return parens==0 && !quoted;
    };

    std::function<bool(const std::string&, nift::RuntimeValue&, int)> eval;
    eval = [&](const std::string& raw, nift::RuntimeValue& out, int depth) -> bool {
        std::string text=trim_copy(raw);
        if (text.empty()) { error="expression cannot be empty"; return false; }
        while (encloses(text)) text=trim_copy(text.substr(1,text.size()-2));

        auto find_binding = [&](const std::string& name) -> VariableBinding* {
            for (auto scope=variable_scopes_.rbegin(); scope!=variable_scopes_.rend(); ++scope) { auto it=scope->find(name); if(it!=scope->end()) { it->second.sync(); return &it->second; } }
            if(!receiver_stack_.empty()){auto it=receiver_stack_.back()->fields.find(name);if(it!=receiver_stack_.back()->fields.end())return &it->second;}
            return nullptr;
        };
        if (text.size() > 1 && (text.front() == '+' || text.front() == '-')) {
            const std::string operand_name = trim_copy(text.substr(1));
            if (valid_binding_identifier(operand_name)) {
                if (auto* binding = find_binding(operand_name); binding && binding->value && binding->value->is_number()) {
                    out = text.front() == '-' ? nift::runtime_number_negate(*binding->value) : *binding->value;
                    return true;
                }
            }
        }

        auto bind_temporary = [&](nift::RuntimeValue value) {
            if (variable_scopes_.empty()) variable_scopes_.emplace_back();
            std::string name;
            do { name = "__nift_runtime_value_" + std::to_string(next_runtime_temporary_id_++); }
            while (find_binding(name));
            auto stored = std::make_shared<nift::RuntimeValue>(std::move(value));
            variable_scopes_.back().emplace(name, VariableBinding{stored, nift_binding_type(*stored), false, false});
            return name;
        };

        auto try_location_ref = [&](const std::string& source, VariableBinding& dst) -> bool {
            const std::string ref_source=trim_copy(source);auto parsed=nift::ast::parse_expression(ref_source);if(!parsed.supported||!parsed.expr)return false;
            std::shared_ptr<std::shared_ptr<nift::RuntimeValue>> root;std::vector<PathComponent> path;
            std::function<bool(const nift::ast::Expr&)> walk;
            walk=[&](const nift::ast::Expr& ex)->bool{
                if(ex.kind==nift::ast::Kind::Binding){auto* b=find_binding(ex.name);if(!b||!b->value)return false;if(b->is_location_ref()){root=b->ref_root_slot;path=b->ref_path;}else root=b->slot;return true;}
                if(ex.kind==nift::ast::Kind::Member){if(!ex.left||!walk(*ex.left))return false;path.push_back(PathComponent::member(ex.name));return true;}
                if(ex.kind==nift::ast::Kind::Index){if(!ex.left||!walk(*ex.left))return false;nift::RuntimeValue idx;if(!eval(ex.right->text.empty()?ref_source.substr(ex.right->span.begin,ex.right->span.end-ex.right->span.begin):ex.right->text,idx,depth+1))return false;std::size_t index=0;if(nift::runtime_number_to_size(idx,index))path.push_back(PathComponent::at(index));else if(idx.is_string())path.push_back(PathComponent::member(idx.string));else return false;return true;}
                return false;
            };
            if(!walk(*parsed.expr)||path.empty())return false;
            dst.ref_root_slot=std::move(root);dst.ref_path=std::move(path);dst.sync();if(!dst.value)return false;
            if(!(dst.value->is_array()||dst.value->is_object()||(dst.value->is_string()&&dst.value->string.rfind("\x1fnift:",0)==0))){dst.ref_root_slot.reset();dst.ref_path.clear();dst.sync();return false;}
            dst.type=nift_binding_type(*dst.value);return true;
        };

        // Bind a callable parameter: preserve location identity when the argument
        // denotes an aggregate location (root+path), otherwise bind an independent
        // value copy. Shared by the named-callable and lambda call paths so both
        // implement identical function-boundary location semantics.
        auto bind_call_param = [&](const std::string& arg_text, bool quoted, nift::RuntimeValue& value) -> VariableBinding {
            if (!quoted) {
                VariableBinding loc;
                if (try_location_ref(arg_text, loc) && loc.is_location_ref() && loc.value) {
                    loc.mutable_binding = true;
                    loc.deep_readonly = false;
                    return loc;
                }
            }
            auto sp = std::make_shared<nift::RuntimeValue>(std::move(value));
            return VariableBinding{sp, nift_binding_type_from_text(arg_text, *sp), true, false};
        };

        auto truthy_value = [&](const nift::RuntimeValue& document) {
            if(document.is_string()&&document.string.rfind("\x1fnift:atomic:",0)==0){auto it=atomic_instances_.find(document.string.substr(13));if(it!=atomic_instances_.end())return it->second->kind==AtomicInstance::Kind::Bool?it->second->bool_value.load():it->second->int_value.load()!=0;}
            if (document.is_bool()) return document.boolean;
            if (document.is_null()) return false;
            if (document.is_number()) return nift::runtime_truthy(document);
            if (document.is_string()) return !document.string.empty();
            if (document.is_bytes()) return nift::runtime_truthy(document);
            if (document.is_array()) return !document.array.empty();
            if (document.is_object()) return !document.object.empty();
            return false;
        };
        auto find_top_level_op = [&](const std::string& op) -> std::size_t {
            bool quoted=false; char quote=0; int parens=0; int brackets=0; int braces=0;
            for (std::size_t i=0; i+op.size()<=text.size(); ++i) {
                char c=text[i];
                if (quoted) { if (c=='\\' && i+1<text.size()) ++i; else if (c==quote) quoted=false; continue; }
                if (c=='\'' || c=='"') { quoted=true; quote=c; continue; }
                if (c=='[') { ++brackets; continue; }
                if (c==']') { if (brackets) --brackets; continue; }
                if (brackets) continue;
                if (c=='{') { ++braces; continue; }
                if (c=='}') { if (braces) --braces; continue; }
                if (braces) continue;
                if (c=='(') { ++parens; continue; }
                if (c==')') { if (parens) --parens; continue; }
                if (!parens && text.compare(i,op.size(),op)==0) return i;
            }
            return std::string::npos;
        };
        // Exact int64 extraction for atomic operations; general numeric equality
        // and ordering use RuntimeValue's arbitrary-decimal comparison below.
        auto exact_i64 = [](const nift::RuntimeValue& d, std::int64_t& v)->bool {
            return nift::runtime_number_to_i64(d, v);
        };
        auto structural_equal = [&](const nift::RuntimeValue& a, const nift::RuntimeValue& b) -> bool {
            std::function<bool(const nift::RuntimeValue&,const nift::RuntimeValue&)> eq;
            eq = [&](const nift::RuntimeValue& x,const nift::RuntimeValue& y)->bool {
                if(x.is_number() && y.is_number()) return nift::runtime_numbers_equal(x,y);
                if(x.type!=y.type)return false;
                if(x.is_null())return true;
                if(x.is_bool())return x.boolean==y.boolean;
                if(x.is_string()){
                    const bool xs=x.string.rfind("\x1fnift:struct:",0)==0, ys=y.string.rfind("\x1fnift:struct:",0)==0;
                    const bool xc=x.string.rfind("\x1fnift:callable:",0)==0, yc=y.string.rfind("\x1fnift:callable:",0)==0;
                    const bool xk=x.string.rfind("\x1fnift:collection:",0)==0, yk=y.string.rfind("\x1fnift:collection:",0)==0;
                    if(xs||ys||xc||yc) return (xs&&ys)||(xc&&yc) ? x.string==y.string : false;
                    if(xk||yk){
                        if(!(xk&&yk))return false;
                        auto xi=collection_instances_.find(x.string.substr(17)), yi=collection_instances_.find(y.string.substr(17));
                        if(xi==collection_instances_.end()||yi==collection_instances_.end()||xi->second->kind!=yi->second->kind)return false;
                        auto a=xi->second,b=yi->second; if(a->kind==CollectionKind::PriQue)return x.string==y.string;
                        if(a->kind==CollectionKind::Stack||a->kind==CollectionKind::Queue){if(a->values.size()!=b->values.size())return false;for(size_t i=0;i<a->values.size();++i)if(!eq(a->values[i],b->values[i]))return false;return true;}
                        if(a->kind==CollectionKind::Set||a->kind==CollectionKind::SortedSet){if(a->values.size()!=b->values.size())return false;for(const auto& v:a->values){bool found=false;for(const auto& w:b->values)if(eq(v,w)){found=true;break;}if(!found)return false;}return true;}
                        if(a->entries.size()!=b->entries.size())return false;
                        for(const auto& e:a->entries){bool found=false;for(const auto& f:b->entries)if(eq(e.first,f.first)&&eq(e.second,f.second)){found=true;break;}if(!found)return false;}return true;
                    }
                    return x.string==y.string;
                }
                if(x.is_bytes()) return nift::runtime_equal(x,y);
                if(x.is_timer()) return nift::runtime_equal(x,y);
                if(x.is_array()){if(x.array.size()!=y.array.size())return false;for(size_t i=0;i<x.array.size();++i)if(!eq(x.array[i],y.array[i]))return false;return true;}
                if(x.is_object()){if(x.object.size()!=y.object.size())return false;std::vector<bool> matched(y.object.size(),false);for(const auto& e:x.object){bool found=false;for(std::size_t i=0;i<y.object.size();++i)if(!matched[i]&&e.first==y.object[i].first&&eq(e.second,y.object[i].second)){matched[i]=true;found=true;break;}if(!found)return false;}return true;}
                return false;
            }; return eq(a,b);
        };


        auto spawn_future = [&](const nift::RuntimeValue& cb, const std::vector<nift::RuntimeValue>& av, nift::RuntimeValue& future_out)->bool {
            if(!cb.is_string()||cb.string.rfind("\x1fnift:callable:",0)!=0){error="async function is not callable";return false;}
            auto transferable=[&](const nift::RuntimeValue& root){std::function<bool(const nift::RuntimeValue&)> ok;ok=[&](const nift::RuntimeValue& v){if(v.is_timer())return false;if(v.is_string()&&v.string.rfind("\x1fnift:",0)==0&&v.string.rfind("\x1fnift:timer:",0)!=0&&v.string.rfind("\x1fnift:enum:",0)!=0&&v.string.rfind("\x1fnift:callable:",0)!=0&&v.string.rfind("\x1fnift:thread:",0)!=0&&v.string.rfind("\x1fnift:mutex:",0)!=0&&v.string.rfind("\x1fnift:atomic:",0)!=0&&v.string.rfind("\x1fnift:async:",0)!=0)return false;if(v.is_array())for(const auto&x:v.array)if(!ok(x))return false;if(v.is_object())for(const auto&kv:v.object)if(!ok(kv.second))return false;return true;};return ok(root);};
            for(const auto& v:av){if(contains_timer_resource(v)){error="async function argument contains a non-transferable timer";return false;}if(!transferable(v)){error="async function argument contains a non-transferable resource";return false;}}
            if(callable_contains_timer_resource(cb)){error="async function capture contains a non-transferable timer";return false;}
            auto inherited_asyncs=async_instances_;auto state=std::make_shared<AsyncInstance>();static std::atomic<std::uint64_t> async_ids{1};const std::string id=std::to_string(async_ids.fetch_add(1));async_instances_[id]=state;owned_async_instances_.push_back(state);
            WorkerCloneMemo clone_memo;std::unordered_map<std::string,VariableBinding> vars;for(const auto& scope:variable_scopes_)for(const auto&kv:scope)if(kv.second.value&&transferable(*kv.second.value))vars[kv.first]=clone_worker_binding(kv.second,clone_memo);
            std::unordered_map<std::string,Callable> funcs;std::unordered_map<std::uint64_t,std::shared_ptr<ModuleEnv>> modules;clone_worker_module_graph(funcs,modules,clone_memo);const auto next_module_identity=next_module_identity_;auto threads=thread_instances_;auto mutexes=mutex_instances_;auto atomics=atomic_instances_;auto asyncs=std::move(inherited_asyncs);
            std::unordered_map<std::string,std::shared_ptr<LambdaInstance>> lambdas;for(const auto&kv:lambda_instances_){auto li=std::make_shared<LambdaInstance>(*kv.second);li->captures.clear();for(const auto&cv:kv.second->captures)if(cv.second.value&&transferable(*cv.second.value))li->captures[cv.first]=clone_worker_binding(cv.second,clone_memo);if(li->module_env){auto owner=modules.find(li->module_env->identity);li->module_env=owner==modules.end()?std::shared_ptr<ModuleEnv>{}:owner->second;}lambdas[kv.first]=std::move(li);}
            const std::string callable_tag=cb.string;
            if(callable_tag.rfind("\x1fnift:callable:lambda:",0)==0){auto it=lambdas.find(callable_tag.substr(22));if(it!=lambdas.end())it->second->async=false;}
            RenderHost* hp=&host_;TrackedInfo* tp=&tracked_info_;
            auto execution_output=execution_output_;
            nift::detail::NiftAsyncPool::instance().submit([state,hp,tp,execution_output=std::move(execution_output),vars=std::move(vars),funcs=std::move(funcs),modules=std::move(modules),next_module_identity,lambdas=std::move(lambdas),threads=std::move(threads),mutexes=std::move(mutexes),atomics=std::move(atomics),asyncs=std::move(asyncs),callable_tag,av]() mutable {
                nift::RuntimeValue result;std::string e;try{Parser worker(*hp,*tp,std::move(execution_output));worker.standalone_script_host_=true;worker.strict_script_mode_=true;worker.callables_=std::move(funcs);worker.module_envs_=std::move(modules);worker.next_module_identity_=next_module_identity;worker.lambda_instances_=std::move(lambdas);worker.thread_instances_=std::move(threads);worker.mutex_instances_=std::move(mutexes);worker.atomic_instances_=std::move(atomics);worker.async_instances_=std::move(asyncs);worker.variable_scopes_.back()=std::move(vars);if(callable_tag.rfind("\x1fnift:callable:named:",0)==0&&!worker.set_named_callable_async(callable_tag,false))e="invalid async callable";auto csp=std::make_shared<nift::RuntimeValue>(callable_tag);worker.variable_scopes_.back()["__future_callable"]=VariableBinding{csp,nift_binding_type(*csp),false,false};std::string expr="__future_callable(";for(size_t i=0;e.empty()&&i<av.size();++i){auto sp=std::make_shared<nift::RuntimeValue>(av[i]);std::string n="__future_arg"+std::to_string(i);worker.variable_scopes_.back()[n]=VariableBinding{sp,nift_binding_type(*sp),false,false};if(i)expr+=",";expr+=n;}expr+=")";if(e.empty()&&!worker.evaluate_expression(expr,result,e)&&e.empty())e="async function failed";if(e.empty()&&worker.contains_timer_resource(result))e="async function result contains a non-transferable timer";}catch(const std::exception& ex){e=ex.what();}catch(...){e="unknown async worker exception";}
                {std::lock_guard<std::mutex> lk(state->mutex);state->result=std::make_shared<nift::RuntimeValue>(std::move(result));state->error=std::move(e);state->done=true;}state->cv.notify_all();
            });
            future_out=nift::RuntimeValue(std::string("\x1fnift:async:")+id);return true;
        };

        auto await_future = [&](const nift::RuntimeValue& h, nift::RuntimeValue& result)->bool {
            if(!h.is_string()||h.string.rfind("\x1fnift:async:",0)!=0){error="await: expected future";return false;}auto ai=async_instances_.find(h.string.substr(12));if(ai==async_instances_.end()){error="await: invalid future";return false;}auto st=ai->second;std::unique_lock<std::mutex> lk(st->mutex);while(!st->done){lk.unlock();if(nift::detail::NiftAsyncPool::is_worker_thread()&&nift::detail::NiftAsyncPool::instance().run_one()){lk.lock();continue;}lk.lock();st->cv.wait_for(lk,std::chrono::milliseconds(1),[&]{return st->done;});}if(!st->error.empty()){error="future: "+st->error;return false;}result=st->result?*st->result:nift::RuntimeValue(nullptr);return true;
        };

        if(text.rfind("await ",0)==0){nift::RuntimeValue f;if(!eval(trim_copy(text.substr(6)),f,depth+1))return false;return await_future(f,out);}

        // v4.3 first-class callable values and lambda expressions.
        const Callable* named_callable = nullptr;
        std::shared_ptr<ModuleEnv> named_owner;
        if (active_module_env_) {
            auto found = active_module_env_->callables.find(text);
            if (found != active_module_env_->callables.end()) { named_callable=&found->second; named_owner=found->second.module_env ? found->second.module_env : active_module_env_; }
        }
        if (!named_callable) {
            auto found = callables_.find(text);
            if (found != callables_.end()) { named_callable=&found->second; named_owner=found->second.module_env; }
        }
        if (named_callable) {
            if (!named_owner && loading_module_env_) named_owner=loading_module_env_;
            out = nift::RuntimeValue(named_callable_tag(text,named_owner));
            return true;
        }
        {
            std::size_t arrow = std::string::npos; int pd=0, bd=0, cd=0; bool iq=false; char qc=0;
            for(std::size_t ai=0;ai+1<text.size();++ai){char ch=text[ai];if(iq){if(ch=='\\')++ai;else if(ch==qc)iq=false;continue;}if(ch=='\"'||ch=='\''){iq=true;qc=ch;continue;}if(ch=='(')++pd;else if(ch==')')--pd;else if(ch=='[')++bd;else if(ch==']')--bd;else if(ch=='{')++cd;else if(ch=='}')--cd;else if(ch=='='&&text[ai+1]=='>'&&pd==0&&bd==0&&cd==0){arrow=ai;break;}}
            if (arrow != std::string::npos && text.substr(0, arrow).find(":=") == std::string::npos && text.substr(0, arrow).find(" = ") == std::string::npos) {
                std::string lhs = trim_copy(text.substr(0, arrow));
                std::string rhs = trim_copy(text.substr(arrow + 2));
                bool async_lambda=false;
                if(lhs.rfind("async ",0)==0){async_lambda=true;lhs=trim_copy(lhs.substr(6));}
                std::vector<std::string> params;
                std::string variadic_param;
                if (!lhs.empty() && lhs.front() == '(' && lhs.back() == ')') {
                    if(!parse_callable_parameters(lhs.substr(1,lhs.size()-2),params,variadic_param)){error="lambda: malformed parameter list";return false;}
                } else if (valid_binding_identifier(lhs)) params.push_back(lhs);
                else { error="lambda: expected identifier or parenthesized parameter list"; return false; }
                auto li=std::make_shared<LambdaInstance>(); li->params=params; li->variadic_param=variadic_param; li->async=async_lambda;
                li->block=rhs.size()>=2&&rhs.front()=='{'&&rhs.back()=='}';
                li->body=li->block?rhs.substr(1,rhs.size()-2):rhs;
                if(!source_path_stack_.empty()) li->source_path=source_path_stack_.back();
                for(const auto& scope:variable_scopes_) for(const auto& kv:scope) li->captures[kv.first]=kv.second;
                li->module_env=active_module_env_ ? active_module_env_ : loading_module_env_;
                const std::string id=std::to_string(next_lambda_instance_id_++); lambda_instances_[id]=li;
                out=nift::RuntimeValue(std::string("\x1fnift:callable:lambda:")+id); return true;
            }
        }

        // v4.3 native scripting filesystem, console and stream primitives.
        {
            std::vector<nift::RuntimeValue> spread_values;
            auto call_args=[&](const std::string& name,std::vector<std::string>& args,std::vector<bool>& quoted)->bool{
                if(text.rfind(name+"(",0)!=0||text.back()!=')')return false;
                std::size_t close=0;if(!find_balanced(text,name.size(),'(',')',close)||close!=text.size()-1)return false;
                bool ok=false;args=parse_parameters(text.substr(name.size()+1,text.size()-name.size()-2),ok,&quoted);
                if(!ok){error=name+": malformed arguments";return false;}
                std::vector<std::string> expanded; std::vector<bool> expanded_q;
                for(std::size_t ai=0;ai<args.size();++ai){
                    if(!(ai<quoted.size()&&quoted[ai])&&trim_copy(args[ai]).rfind("...",0)==0){nift::RuntimeValue sv;if(!eval(trim_copy(args[ai]).substr(3),sv,depth+1))return false;if(!sv.is_array()){error=name+": spread value must be an array";return false;}for(const auto& item:sv.array){expanded.push_back("\x1fnift:spread:"+std::to_string(spread_values.size()));spread_values.push_back(item);expanded_q.push_back(false);}}
                    else {expanded.push_back(args[ai]);expanded_q.push_back(ai<quoted.size()&&quoted[ai]);}
                }
                args.swap(expanded);quoted.swap(expanded_q);return true;
            };
            auto arg_value=[&](const std::vector<std::string>& args,const std::vector<bool>& q,size_t i,nift::RuntimeValue& v)->bool{
                if(i>=args.size())return false;
                if(i<q.size()&&q[i]){v=nift::RuntimeValue(args[i]);return true;}
                if(args[i].rfind("\x1fnift:spread:",0)==0){const auto index=static_cast<std::size_t>(std::stoull(args[i].substr(13)));if(index>=spread_values.size())return false;v=spread_values[index];return true;}
                return eval(args[i],v,depth+1);
            };
            auto string_arg=[&](const std::string& name,const std::vector<std::string>& args,const std::vector<bool>& q,size_t i,std::string& v)->bool{
                nift::RuntimeValue d;if(!arg_value(args,q,i,d)||!d.is_string()){error=name+": expected string path";return false;}v=d.string;return true;
            };
            auto resolve_path=[&](const std::string& raw)->fs::path{std::string expanded=raw;if(standalone_script_host_&&!expanded.empty()&&expanded[0]=='~'&&(expanded.size()==1||expanded[1]=='/'||expanded[1]=='\\')){const char* home=std::getenv("HOME");
#ifdef _WIN32
if(!home)home=std::getenv("USERPROFILE");
#endif
if(home)expanded=std::string(home)+expanded.substr(1);}fs::path p(expanded);if(p.is_relative())p=(standalone_script_host_?fs::current_path():host_.root())/p;return fs::absolute(p).lexically_normal();};
            auto checked_path=[&](const std::string& name,const std::vector<std::string>& aa,const std::vector<bool>& qq,size_t i,fs::path& p)->bool{std::string raw;if(!string_arg(name,aa,qq,i,raw))return false;p=resolve_path(raw);if(!standalone_script_host_&&!host_.root().empty()&&!filesystem::path_within(fs::absolute(host_.root()).lexically_normal(),p)){error=name+": path must stay inside the Nift project";return false;}if(!nift_fs_root_allowed(p,standalone_script_host_,error)){error=name+": "+error;return false;}return true;};
            std::vector<std::string> args;std::vector<bool> q;
            if(call_args("cmd",args,q)){
                if(args.empty()){error="cmd: expected executable and optional arguments";return false;}std::vector<std::string> vals;
                for(std::size_t ai=0;ai<args.size();++ai){nift::RuntimeValue v;if(!arg_value(args,q,ai,v))return false;if(v.is_array()){for(const auto&x:v.array){if(!(x.is_string()||x.is_number()||x.is_bool())){error="cmd: arguments must be scalar";return false;}vals.push_back(render_expression_value(x));}}else if(v.is_string()||v.is_number()||v.is_bool())vals.push_back(render_expression_value(v));else{error="cmd: arguments must be scalar";return false;}}
                auto c=std::make_shared<CommandInstance>();ProcessSpec ps;ps.program=vals.front();ps.args.assign(vals.begin()+1,vals.end());c->stages.push_back(std::move(ps));auto id=std::to_string(next_command_instance_id_++);command_instances_[id]=c;out=nift::RuntimeValue(std::string("\x1fnift:cmd:")+id);return true;
            }
            if(call_args("run",args,q)){
                if(std::getenv("NIFT_NO_PROCESS")){error="run: external process execution disabled";return false;}
                if(args.empty()){error="run: expected executable and optional arguments";return false;}
                std::vector<std::string> vals;
                for(std::size_t ai=0;ai<args.size();++ai){nift::RuntimeValue v;if(!arg_value(args,q,ai,v))return false;if(v.is_array()){for(const auto& x:v.array){if(!x.is_string()&&!x.is_number()&&!x.is_bool()){error="run: arguments must be scalar";return false;}vals.push_back(render_expression_value(x));}}else if(v.is_string()||v.is_number()||v.is_bool())vals.push_back(render_expression_value(v));else{error="run: arguments must be scalar";return false;}}
                ProcessSpec spec;spec.program=vals.front();spec.args.assign(vals.begin()+1,vals.end());auto pr=nift_run_process(spec,true,false);
                out=nift::RuntimeValue::make_object();out["exit_code"]=nift::RuntimeValue((double)pr.exit_code);out["stdout"]=nift::RuntimeValue(pr.out);out["stderr"]=nift::RuntimeValue(pr.err);out["launched"]=nift::RuntimeValue(pr.launched);if(!pr.error.empty())out["error"]=nift::RuntimeValue(pr.error);return true;
            }
            if(call_args("which",args,q)){if(args.size()!=1){error="which: expected command name";return false;}std::string n;if(!string_arg("which",args,q,0,n))return false;static const std::unordered_set<std::string> builtins={"ls","cd","pwd","cat","touch","exists","cp","mv","rm","mkdir","copy","move","remove","make_dir","run","cmd"};if(builtins.count(n)){out=nift::RuntimeValue("nift:"+n);return true;}std::string p;if(nift_find_executable(n,p)){out=nift::RuntimeValue(p);return true;}out=nift::RuntimeValue(nullptr);return true;}
            // Native project/build automation (v4.4 CP23/CP24). Direct ProjectInfo
            // calls, never a subprocess. Script-land only.
            auto automation_result=[&](NiftOperationResult r)->nift::RuntimeValue{nift::RuntimeValue d=nift::RuntimeValue::make_object();d["ok"]=nift::RuntimeValue(r.ok);d["exit_code"]=nift::RuntimeValue((double)r.exit_code);nift::RuntimeValue aff=nift::RuntimeValue::make_array();for(const auto& a:r.affected)aff.array.emplace_back(a);d["affected"]=std::move(aff);nift::RuntimeValue errs=nift::RuntimeValue::make_array();for(const auto& e:r.errors)errs.array.emplace_back(e);d["errors"]=std::move(errs);d["duration_seconds"]=nift::RuntimeValue(r.duration_seconds);return d;};
            auto ensure_project=[&](const std::string& fn,ProjectInfo*& proj)->bool{if(!standalone_script_host_){error=fn+": only available in standalone Nift scripts/shell";return false;}const fs::path cwd=fs::absolute(fs::current_path()).lexically_normal();if(!automation_project_||automation_root_.empty()||!filesystem::path_within(automation_root_,cwd)){auto p=std::make_shared<ProjectInfo>();std::string oe;if(!automation_open_project(cwd,*p,oe)){automation_project_.reset();automation_root_.clear();error=fn+": "+oe;return false;}automation_project_=p;automation_root_=fs::absolute(automation_project_->root).lexically_normal();}proj=automation_project_.get();return true;};
            if(call_args("build",args,q)||call_args("build_all",args,q)||call_args("build_repair",args,q)){const std::string fn=text.rfind("build_all",0)==0?"build_all":(text.rfind("build_repair",0)==0?"build_repair":"build");if(!args.empty()){error=fn+": expected no arguments";return false;}ProjectInfo* proj=nullptr;if(!ensure_project(fn,proj))return false;NiftBuildRequest req;req.mode=fn=="build_all"?NiftBuildRequest::Mode::All:(fn=="build_repair"?NiftBuildRequest::Mode::Repair:NiftBuildRequest::Mode::Updated);out=automation_result(automation_build(*proj,req));return true;}
            if(call_args("build_names",args,q)){if(args.empty()){error="build_names: expected at least one name";return false;}ProjectInfo* proj=nullptr;if(!ensure_project("build_names",proj))return false;NiftBuildRequest req;req.mode=NiftBuildRequest::Mode::Names;for(size_t ai=0;ai<args.size();++ai){std::string n;if(!string_arg("build_names",args,q,ai,n))return false;req.names.push_back(n);}out=automation_result(automation_build(*proj,req));return true;}
            if(call_args("track",args,q)){if(args.size()<1||args.size()>3){error="track: expected name, optional title and template";return false;}std::string name,title,template_path;if(!string_arg("track",args,q,0,name))return false;if(args.size()>=2&&!string_arg("track",args,q,1,title))return false;if(args.size()>=3&&!string_arg("track",args,q,2,template_path))return false;ProjectInfo* proj=nullptr;if(!ensure_project("track",proj))return false;out=automation_result(automation_track(*proj,name,title,template_path));return true;}
            if(call_args("untrack",args,q)){if(args.empty()){error="untrack: expected at least one name";return false;}ProjectInfo* proj=nullptr;if(!ensure_project("untrack",proj))return false;std::vector<std::string> names;for(size_t ai=0;ai<args.size();++ai){std::string n;if(!string_arg("untrack",args,q,ai,n))return false;names.push_back(n);}out=automation_result(automation_untrack(*proj,names,false));return true;}
            if(call_args("status",args,q)){if(!args.empty()){error="status: expected no arguments";return false;}ProjectInfo* proj=nullptr;if(!ensure_project("status",proj))return false;auto names=automation_status(*proj);out=nift::RuntimeValue::make_array();for(const auto& n:names)out.array.emplace_back(n);return true;}
            if(call_args("tracked",args,q)){if(!args.empty()){error="tracked: expected no arguments";return false;}ProjectInfo* proj=nullptr;if(!ensure_project("tracked",proj))return false;auto names=automation_tracked_names(*proj);out=nift::RuntimeValue::make_array();for(const auto& n:names)out.array.emplace_back(n);return true;}
            if(call_args("project_root",args,q)){if(!args.empty()){error="project_root: expected no arguments";return false;}ProjectInfo* proj=nullptr;if(!ensure_project("project_root",proj))return false;out=nift::RuntimeValue(fs::absolute(proj->root).generic_string());return true;}
            if(call_args("getenv",args,q)){if(args.size()!=1){error="getenv: expected name";return false;}std::string k;if(!string_arg("getenv",args,q,0,k))return false;auto v=host_.environment(k);if(v.status==nift::HostStatus::Error){error="getenv: "+v.error;return false;}out=v.status==nift::HostStatus::Found?nift::RuntimeValue(v.value):nift::RuntimeValue(nullptr);return true;}
            if(call_args("env",args,q)){if(!args.empty()){error="env: expected no arguments";return false;}return host_.environment_snapshot(out,error);}
            if(call_args("os",args,q)){if(!args.empty()){error="os: expected no arguments";return false;}out=nift::RuntimeValue(std::string(nift_environment::host_os()));return true;}
            if(call_args("arch",args,q)){if(!args.empty()){error="arch: expected no arguments";return false;}out=nift::RuntimeValue(std::string(nift_environment::host_arch()));return true;}
            if(call_args("platform",args,q)){if(!args.empty()){error="platform: expected no arguments";return false;}out=nift::RuntimeValue(host_.platform());return true;}
            if(call_args("epoch",args,q)){if(!args.empty()){error="epoch: expected no arguments";return false;}std::int64_t ms=0;if(!nift::detail::nift_unix_epoch_milliseconds(ms,error))return false;out=nift::runtime_integer(ms);return true;}
            if(call_args("sleep",args,q)){if(args.size()!=1){error="sleep: expected one millisecond duration";return false;}nift::RuntimeValue duration;std::int64_t ms=0;if(!arg_value(args,q,0,duration)||!nift::runtime_number_to_i64(duration,ms)||ms<0){error="sleep: milliseconds must be a non-negative signed 64-bit integer";return false;}using Rep=std::chrono::milliseconds::rep;if(static_cast<std::uint64_t>(ms)>static_cast<std::uint64_t>(std::numeric_limits<Rep>::max())){error="sleep: millisecond duration is unsupported on this platform";return false;}std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<Rep>(ms)));out=nift::RuntimeValue(nullptr);return true;}
            if(call_args("timer",args,q)){std::vector<nift::RuntimeValue> values;for(std::size_t i=0;i<args.size();++i){nift::RuntimeValue value;if(!arg_value(args,q,i,value))return false;values.push_back(std::move(value));}return make_timer(values,out,error);}
            if(call_args("secure_random_bytes",args,q)){if(args.size()!=1){error="secure_random_bytes: expected one byte count";return false;}nift::RuntimeValue count_value;std::size_t count=0;if(!arg_value(args,q,0,count_value)||!nift::runtime_number_to_size(count_value,count)){error="secure_random_bytes: byte count must be a non-negative integer";return false;}if(count>10000000){error="secure_random_bytes: result exceeds 10000000 bytes";return false;}return nift::detail::nift_secure_random_bytes(count,out,error);}
            if(call_args("hardware_concurrency",args,q)){if(!args.empty()){error="hardware_concurrency: expected no arguments";return false;}out=nift::RuntimeValue((double)std::max(1u,std::thread::hardware_concurrency()));return true;}
            const bool atomic_int_ctor=call_args("atomic<int>",args,q);
            const bool atomic_bool_ctor=!atomic_int_ctor&&call_args("atomic<bool>",args,q);
            if(atomic_int_ctor||atomic_bool_ctor){
                const std::string name=atomic_int_ctor?"atomic<int>":"atomic<bool>";
                if(args.size()!=1){error=name+": expected one initial value";return false;}
                nift::RuntimeValue initial;if(!arg_value(args,q,0,initial))return false;
                auto st=std::make_shared<AtomicInstance>();
                if(atomic_int_ctor){std::int64_t v=0;if(!exact_i64(initial,v)){error=name+": initial value must be a signed 64-bit integer";return false;}st->kind=AtomicInstance::Kind::Int;st->int_value.store(v);}
                else {if(!initial.is_bool()){error=name+": initial value must be bool";return false;}st->kind=AtomicInstance::Kind::Bool;st->bool_value.store(initial.boolean);}
                static std::atomic<std::uint64_t> atomic_ids{1};const std::string id=std::to_string(atomic_ids.fetch_add(1));atomic_instances_[id]=st;out=nift::RuntimeValue(std::string("\x1fnift:atomic:")+id);return true;
            }
            if(call_args("thread",args,q)){
                if(args.empty()){error="thread: expected callable and optional arguments";return false;}
                nift::RuntimeValue cb;if((q.size()>0&&q[0])||!eval(args[0],cb,depth+1)||!cb.is_string()||cb.string.rfind("\x1fnift:callable:",0)!=0){error="thread: first argument must be callable";return false;}
                auto transferable=[&](const nift::RuntimeValue& root){std::function<bool(const nift::RuntimeValue&)> ok;ok=[&](const nift::RuntimeValue& v){if(v.is_timer())return false;if(v.is_string()&&v.string.rfind("\x1fnift:",0)==0&&v.string.rfind("\x1fnift:timer:",0)!=0&&v.string.rfind("\x1fnift:enum:",0)!=0&&v.string.rfind("\x1fnift:callable:",0)!=0&&v.string.rfind("\x1fnift:thread:",0)!=0&&v.string.rfind("\x1fnift:mutex:",0)!=0&&v.string.rfind("\x1fnift:atomic:",0)!=0&&v.string.rfind("\x1fnift:async:",0)!=0)return false;if(v.is_array())for(const auto&x:v.array)if(!ok(x))return false;if(v.is_object())for(const auto&kv:v.object)if(!ok(kv.second))return false;return true;};return ok(root);};
                std::vector<nift::RuntimeValue> av;for(size_t ai=1;ai<args.size();++ai){nift::RuntimeValue v;if(ai<q.size()&&q[ai])v=nift::RuntimeValue(args[ai]);else if(!eval(args[ai],v,depth+1))return false;if(contains_timer_resource(v)){error="thread: argument contains a non-transferable timer";return false;}if(!transferable(v)){error="thread: argument contains a non-transferable resource";return false;}av.push_back(v);}
                if(callable_contains_timer_resource(cb)){error="thread: capture contains a non-transferable timer";return false;}
                auto state=std::make_shared<ThreadInstance>();static std::atomic<std::uint64_t> thread_ids{1};const std::string id=std::to_string(thread_ids.fetch_add(1));thread_instances_[id]=state;owned_thread_instances_.push_back(state);
                // Snapshot ordinary root bindings and callable/lambda definitions. Opaque
                // values are intentionally omitted from the worker environment.
                WorkerCloneMemo clone_memo;std::unordered_map<std::string,VariableBinding> vars;
                for(const auto& scope:variable_scopes_)for(const auto&kv:scope)if(kv.second.value&&transferable(*kv.second.value))vars[kv.first]=clone_worker_binding(kv.second,clone_memo);
                std::unordered_map<std::string,Callable> funcs;std::unordered_map<std::uint64_t,std::shared_ptr<ModuleEnv>> modules;clone_worker_module_graph(funcs,modules,clone_memo);const auto next_module_identity=next_module_identity_;auto threads=thread_instances_;auto mutexes=mutex_instances_;auto atomics=atomic_instances_;auto asyncs=async_instances_;
                std::unordered_map<std::string,std::shared_ptr<LambdaInstance>> lambdas;
                for(const auto&kv:lambda_instances_){auto li=std::make_shared<LambdaInstance>(*kv.second);li->captures.clear();for(const auto&cv:kv.second->captures)if(cv.second.value&&transferable(*cv.second.value))li->captures[cv.first]=clone_worker_binding(cv.second,clone_memo);if(li->module_env){auto owner=modules.find(li->module_env->identity);li->module_env=owner==modules.end()?std::shared_ptr<ModuleEnv>{}:owner->second;}lambdas[kv.first]=std::move(li);}
                RenderHost* hp=&host_;TrackedInfo* tp=&tracked_info_;const std::string callable_tag=cb.string;
                auto execution_output=execution_output_;
                state->worker=std::thread([state,hp,tp,execution_output=std::move(execution_output),vars=std::move(vars),funcs=std::move(funcs),modules=std::move(modules),next_module_identity,lambdas=std::move(lambdas),threads=std::move(threads),mutexes=std::move(mutexes),atomics=std::move(atomics),asyncs=std::move(asyncs),callable_tag,av=std::move(av)]() mutable {
                    nift::RuntimeValue result;std::string e;
                    try{Parser worker(*hp,*tp,std::move(execution_output));worker.standalone_script_host_=true;worker.strict_script_mode_=true;worker.callables_=std::move(funcs);worker.module_envs_=std::move(modules);worker.next_module_identity_=next_module_identity;worker.lambda_instances_=std::move(lambdas);worker.thread_instances_=std::move(threads);worker.mutex_instances_=std::move(mutexes);worker.atomic_instances_=std::move(atomics);worker.async_instances_=std::move(asyncs);worker.variable_scopes_.back()=std::move(vars);auto csp=std::make_shared<nift::RuntimeValue>(callable_tag);worker.variable_scopes_.back()["__thread_callable"]=VariableBinding{csp,nift_binding_type(*csp),false,false};std::string expr="__thread_callable(";for(size_t i=0;i<av.size();++i){auto sp=std::make_shared<nift::RuntimeValue>(av[i]);std::string n="__thread_arg"+std::to_string(i);worker.variable_scopes_.back()[n]=VariableBinding{sp,nift_binding_type(*sp),false,false};if(i)expr+=",";expr+=n;}expr+=")";if(!worker.evaluate_expression(expr,result,e)&&e.empty())e="thread callable failed";if(e.empty()&&worker.contains_timer_resource(result))e="thread result contains a non-transferable timer";}catch(const std::exception& ex){e=ex.what();}catch(...){e="unknown worker exception";}
                    std::lock_guard<std::mutex> lock(state->mutex);state->result=std::make_shared<nift::RuntimeValue>(std::move(result));state->error=std::move(e);state->done=true;
                });
                out=nift::RuntimeValue(std::string("\x1fnift:thread:")+id);return true;
            }
            if(call_args("async",args,q)){error="async(callable, ...) was removed; declare an async function/lambda and call it directly";return false;}
            if(call_args("await",args,q)){error="await(...) was removed; use 'await future' or 'await async_fn(args)'";return false;}
            if(call_args("mutex",args,q)){
                if(args.size()>1){error="mutex: expected zero or one initial value";return false;}
                nift::RuntimeValue initial(nullptr);if(args.size()==1&&!arg_value(args,q,0,initial))return false;
                if(contains_timer_resource(initial)){error="mutex: initial value contains a non-transferable timer";return false;}
                std::function<bool(const nift::RuntimeValue&)> ok;ok=[&](const nift::RuntimeValue& v){if(v.is_string()&&v.string.rfind("\x1fnift:",0)==0&&v.string.rfind("\x1fnift:enum:",0)!=0&&v.string.rfind("\x1fnift:callable:",0)!=0&&v.string.rfind("\x1fnift:thread:",0)!=0&&v.string.rfind("\x1fnift:mutex:",0)!=0&&v.string.rfind("\x1fnift:atomic:",0)!=0&&v.string.rfind("\x1fnift:async:",0)!=0)return false;if(v.is_array())for(const auto&x:v.array)if(!ok(x))return false;if(v.is_object())for(const auto&kv:v.object)if(!ok(kv.second))return false;return true;};
                if(!ok(initial)){error="mutex: initial value contains a non-transferable resource";return false;}
                auto st=std::make_shared<MutexInstance>();st->value=std::move(initial);static std::atomic<std::uint64_t> mutex_ids{1};const std::string id=std::to_string(mutex_ids.fetch_add(1));mutex_instances_[id]=st;out=nift::RuntimeValue(std::string("\x1fnift:mutex:")+id);return true;
            }
            if(call_args("ffi_open",args,q)){
                if(args.size()!=1){error="ffi_open: expected library path";return false;}
                std::string path;if(!string_arg("ffi_open",args,q,0,path))return false;
                void* handle=nullptr;
#ifdef _WIN32
                int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,nullptr,0);if(n<=0){error="ffi_open: invalid UTF-8 path";return false;}std::wstring w(static_cast<size_t>(n),L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,w.data(),n);handle=reinterpret_cast<void*>(LoadLibraryW(w.c_str()));
#else
                dlerror();handle=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
#endif
                if(!handle){
#ifdef _WIN32
                    error="ffi_open: failed to load library";
#else
                    const char* e=dlerror();error=std::string("ffi_open: ")+(e?e:"failed to load library");
#endif
                    return false;
                }
                auto st=std::make_shared<FfiLibraryInstance>();st->handle=handle;const std::string id=std::to_string(next_ffi_library_id_++);ffi_libraries_[id]=st;out=nift::RuntimeValue(std::string("\x1fnift:ffi-lib:")+id);return true;
            }
            if(call_args("ffi_close",args,q)){
                if(args.size()!=1){error="ffi_close: expected library handle";return false;}nift::RuntimeValue h;if(!arg_value(args,q,0,h)||!h.is_string()||h.string.rfind("\x1fnift:ffi-lib:",0)!=0){error="ffi_close: expected library handle";return false;}auto it=ffi_libraries_.find(h.string.substr(14));if(it==ffi_libraries_.end()||it->second->closed){error="ffi_close: invalid or closed library handle";return false;}
#ifdef _WIN32
                if(!FreeLibrary(static_cast<HMODULE>(it->second->handle))){error="ffi_close: unload failed";return false;}
#else
                if(dlclose(it->second->handle)!=0){const char* e=dlerror();error=std::string("ffi_close: ")+(e?e:"unload failed");return false;}
#endif
                it->second->handle=nullptr;it->second->closed=true;out=nift::RuntimeValue(nullptr);return true;
            }
            if(call_args("ffi_buffer",args,q)){
                if(args.size()!=1){error="ffi_buffer: expected string, bytes, or byte array";return false;}nift::RuntimeValue d;if(!arg_value(args,q,0,d))return false;auto st=std::make_shared<FfiBufferInstance>();if(d.is_string())st->bytes.assign(d.string.begin(),d.string.end());else if(d.is_bytes())st->bytes.assign(d.bytes->begin(),d.bytes->end());else if(d.is_array()){for(const auto&x:d.array){std::size_t byte=0;if(!nift::runtime_number_to_size(x,byte)||byte>255){error="ffi_buffer: array values must be bytes";return false;}st->bytes.push_back(static_cast<unsigned char>(byte));}}else{error="ffi_buffer: expected string, bytes, or byte array";return false;}const std::string id=std::to_string(next_ffi_buffer_id_++);ffi_buffers_[id]=st;out=nift::RuntimeValue(std::string("\x1fnift:ffi-buffer:")+id);return true;
            }
            if(call_args("ffi_bytes",args,q)){
                if(args.size()!=1){error="ffi_bytes: expected buffer handle";return false;}nift::RuntimeValue h;if(!arg_value(args,q,0,h)||!h.is_string()||h.string.rfind("\x1fnift:ffi-buffer:",0)!=0){error="ffi_bytes: expected buffer handle";return false;}auto bi=ffi_buffers_.find(h.string.substr(17));if(bi==ffi_buffers_.end()){error="ffi_bytes: invalid buffer handle";return false;}out=nift::RuntimeValue::make_array();for(unsigned char b:bi->second->bytes)out.array.emplace_back(static_cast<int>(b));return true;
            }
            if(call_args("ffi_snapshot_bytes",args,q)){
                if(args.size()!=1){error="ffi_snapshot_bytes: expected buffer handle";return false;}nift::RuntimeValue h;if(!arg_value(args,q,0,h)||!h.is_string()||h.string.rfind("\x1fnift:ffi-buffer:",0)!=0){error="ffi_snapshot_bytes: expected buffer handle";return false;}auto bi=ffi_buffers_.find(h.string.substr(17));if(bi==ffi_buffers_.end()){error="ffi_snapshot_bytes: invalid buffer handle";return false;}out=nift::RuntimeValue(nift::RuntimeBytes(bi->second->bytes.begin(),bi->second->bytes.end()));return true;
            }
            if(call_args("ffi_sizeof",args,q)||call_args("ffi_struct",args,q)){
                const bool make_struct=text.rfind("ffi_struct(",0)==0;if((make_struct&&args.size()!=2)||(!make_struct&&args.size()!=1)){error=make_struct?"ffi_struct: expected layout and values":"ffi_sizeof: expected layout";return false;}std::string layout;if(!string_arg(make_struct?"ffi_struct":"ffi_sizeof",args,q,0,layout))return false;
                std::vector<std::string> fields;std::size_t pos=0;while(pos<=layout.size()){auto c=layout.find(',',pos);auto t=trim_copy(layout.substr(pos,c==std::string::npos?std::string::npos:c-pos));if(t.empty()){error="ffi struct: empty field type";return false;}fields.push_back(t);if(c==std::string::npos)break;pos=c+1;}
                auto valid_field=[](const std::string&t){static const std::unordered_set<std::string> ok={"bool","i8","i16","i32","i64","u8","u16","u32","u64","f32","f64","ptr","cstr"};return ok.count(t)!=0;};
                std::vector<ffi_type*> elements;elements.reserve(fields.size());for(const auto&t:fields){if(!valid_field(t)){error="ffi struct: unsupported field type '"+t+"'";return false;}elements.push_back(nift_ffi_type(t));}std::vector<std::size_t> offs;std::size_t total=0;if(nift_ffi_struct_layout(elements,offs,total)!=FFI_OK){error="ffi struct: libffi could not compute layout";return false;}
                if(!make_struct){out=nift::RuntimeValue(static_cast<double>(total));return true;}nift::RuntimeValue vals;if(!arg_value(args,q,1,vals)||!vals.is_array()||vals.array.size()!=fields.size()){error="ffi_struct: values must be array matching layout";return false;}auto st=std::make_shared<FfiBufferInstance>();st->bytes.assign(total,0);
                for(size_t i=0;i<fields.size();++i){const auto&t=fields[i];const auto&v=vals.array[i];unsigned char* p=st->bytes.data()+offs[i];if(t=="f32"||t=="f64"){if(!v.is_number()){error="ffi_struct: floating field must be numeric";return false;}if(t=="f32"){float x=static_cast<float>(v.num);std::memcpy(p,&x,sizeof(x));}else{double x=v.num;std::memcpy(p,&x,sizeof(x));}continue;}if(t=="ptr"||t=="cstr"){error="ffi_struct: pointer fields are not supported in v4.5";return false;}if(t=="bool"){if(!v.is_bool()){error="ffi_struct: bool field must be bool";return false;}const std::uint8_t x=v.boolean?1:0;std::memcpy(p,&x,sizeof(x));continue;}const bool uns=t[0]=='u';const unsigned bits=(t=="i8"||t=="u8")?8:(t=="i16"||t=="u16")?16:(t=="i32"||t=="u32")?32:64;std::uint64_t uv=0;std::int64_t sv=0;if((uns&&!nift::runtime_number_to_unsigned(v,bits,uv))||(!uns&&!nift::runtime_number_to_signed(v,bits,sv))){error="ffi_struct: integer field out of range for "+t;return false;}if(uns){if(bits==8){const auto x=static_cast<std::uint8_t>(uv);std::memcpy(p,&x,sizeof(x));}else if(bits==16){const auto x=static_cast<std::uint16_t>(uv);std::memcpy(p,&x,sizeof(x));}else if(bits==32){const auto x=static_cast<std::uint32_t>(uv);std::memcpy(p,&x,sizeof(x));}else std::memcpy(p,&uv,sizeof(uv));}else{if(bits==8){const auto x=static_cast<std::int8_t>(sv);std::memcpy(p,&x,sizeof(x));}else if(bits==16){const auto x=static_cast<std::int16_t>(sv);std::memcpy(p,&x,sizeof(x));}else if(bits==32){const auto x=static_cast<std::int32_t>(sv);std::memcpy(p,&x,sizeof(x));}else std::memcpy(p,&sv,sizeof(sv));}}
                const std::string id=std::to_string(next_ffi_buffer_id_++);ffi_buffers_[id]=st;out=nift::RuntimeValue(std::string("\x1fnift:ffi-buffer:")+id);return true;
            }
            if(call_args("ffi_callback",args,q)){
                if(args.size()!=2){error="ffi_callback: expected callable and signature";return false;}nift::RuntimeValue cb;if(!arg_value(args,q,0,cb)||!cb.is_string()||cb.string.rfind("\x1fnift:callable:",0)!=0){error="ffi_callback: first argument must be callable";return false;}std::string sig;if(!string_arg("ffi_callback",args,q,1,sig))return false;sig.erase(std::remove_if(sig.begin(),sig.end(),[](unsigned char c){return std::isspace(c);}),sig.end());if(sig!="i64(i64)"){error="ffi_callback: v4.5 supports only i64(i64) callbacks";return false;}const std::string id=std::to_string(next_ffi_callback_id_++);ffi_callbacks_i64_[id]=cb.string;out=nift::RuntimeValue(std::string("\x1fnift:ffi-callback-i64:")+id);return true;
            }
            if(call_args("ffi_call",args,q)){
                if(args.size()<3){error="ffi_call: expected library, symbol, signature and optional arguments";return false;}
                nift::RuntimeValue lh;if(!arg_value(args,q,0,lh)||!lh.is_string()||lh.string.rfind("\x1fnift:ffi-lib:",0)!=0){error="ffi_call: expected library handle";return false;}auto li=ffi_libraries_.find(lh.string.substr(14));if(li==ffi_libraries_.end()||li->second->closed||!li->second->handle){error="ffi_call: invalid or closed library handle";return false;}
                std::string symbol,signature;if(!string_arg("ffi_call",args,q,1,symbol)||!string_arg("ffi_call",args,q,2,signature))return false;if(symbol.empty()){error="ffi_call: symbol must not be empty";return false;}
                signature.erase(std::remove_if(signature.begin(),signature.end(),[](unsigned char c){return std::isspace(c);}),signature.end());auto lp=signature.find('(');if(lp==std::string::npos||signature.empty()||signature.back()!=')'||signature.find('(',lp+1)!=std::string::npos){error="ffi_call: malformed signature";return false;}std::string rt=signature.substr(0,lp),inside=signature.substr(lp+1,signature.size()-lp-2);std::vector<std::string> types;if(!inside.empty()){std::size_t p=0;while(p<=inside.size()){auto c=inside.find(',',p);types.push_back(inside.substr(p,c==std::string::npos?std::string::npos:c-p));if(c==std::string::npos)break;p=c+1;}}
                auto valid_type=[](const std::string&t){static const std::unordered_set<std::string> ok={"void","bool","i8","i16","i32","i64","u8","u16","u32","u64","f32","f64","ptr","cstr","buffer","callback_i64"};return ok.count(t)!=0;};if(!valid_type(rt)){error="ffi_call: unsupported return type";return false;}if(rt=="buffer"||rt=="callback_i64"){error="ffi_call: unsupported return type";return false;}for(const auto&t:types)if(!valid_type(t)||t=="void"){error="ffi_call: unsupported argument type '"+t+"'";return false;}if(types.size()!=args.size()-3){error="ffi_call: signature argument count mismatch";return false;}if(types.size()>6){error="ffi_call: v4.5 supports at most 6 arguments";return false;}
                auto is_float=[](const std::string&t){return t=="f32"||t=="f64";};bool any_float=is_float(rt);for(const auto&t:types)any_float=any_float||is_float(t);if(any_float){bool all32=(rt=="f32");bool all64=(rt=="f64");for(const auto&t:types){all32=all32&&t=="f32";all64=all64&&t=="f64";}if(!(all32||all64)){error="ffi_call: mixed floating/integer signatures are not supported by the built-in v4.5 dispatcher";return false;}if(types.size()>4){error=all64?"ffi_call: f64 dispatcher supports up to 4 arguments":"ffi_call: f32 dispatcher supports up to 4 arguments";return false;}}
                NiftFfiFunction sym=nullptr;
#ifdef _WIN32
                // GetProcAddress returns FARPROC, so Windows explicitly permits a
                // function-pointer conversion without an intermediate data pointer.
                sym=reinterpret_cast<NiftFfiFunction>(GetProcAddress(static_cast<HMODULE>(li->second->handle),symbol.c_str()));
#else
                // POSIX specifies that a successful dlsym result can represent a
                // function pointer. ISO C++ has no direct API for that boundary, so
                // copy the POSIX-provided representation into libffi's exact type.
                static_assert(sizeof(NiftFfiFunction)==sizeof(void*),"dlsym function pointer representation is unsupported");void* address=nullptr;dlerror();address=dlsym(li->second->handle,symbol.c_str());const char* dlerr=dlerror();if(dlerr){error=std::string("ffi_call: symbol lookup failed: ")+dlerr;return false;}std::memcpy(&sym,&address,sizeof(sym));
#endif
                if(!sym){error="ffi_call: symbol not found: "+symbol;return false;}

                std::vector<NiftFfiValue> values(types.size());
                std::vector<ffi_type*> ffi_types;ffi_types.reserve(types.size());
                std::vector<void*> ffi_values;ffi_values.reserve(types.size());
                std::vector<std::string> string_storage;string_storage.reserve(types.size());
                NiftFfiCallbackActivation callback_activation;
                for(size_t i=0;i<types.size();++i){
                    nift::RuntimeValue d;if(!arg_value(args,q,i+3,d))return false;const auto&t=types[i];auto& v=values[i];ffi_types.push_back(nift_ffi_type(t));
                    if(t=="cstr"){if(d.is_null())v.pointer=nullptr;else{if(!d.is_string()){error="ffi_call: cstr argument must be string or null";return false;}if(d.string.find('\0')!=std::string::npos){error="ffi_call: cstr contains embedded NUL";return false;}string_storage.push_back(d.string);v.pointer=const_cast<char*>(string_storage.back().c_str());}ffi_values.push_back(&v.pointer);continue;}
                    if(t=="buffer"){if(!d.is_string()||d.string.rfind("\x1fnift:ffi-buffer:",0)!=0){error="ffi_call: buffer argument must be FFI buffer handle";return false;}auto bi=ffi_buffers_.find(d.string.substr(17));if(bi==ffi_buffers_.end()){error="ffi_call: invalid buffer handle";return false;}v.pointer=bi->second->bytes.empty()?nullptr:bi->second->bytes.data();ffi_values.push_back(&v.pointer);continue;}
                    if(t=="callback_i64"){if(!d.is_string()||d.string.rfind("\x1fnift:ffi-callback-i64:",0)!=0){error="ffi_call: callback_i64 argument must be callback handle";return false;}auto ci=ffi_callbacks_i64_.find(d.string.substr(23));if(ci==ffi_callbacks_i64_.end()){error="ffi_call: invalid callback handle";return false;}if(!callback_activation.activate(this,ci->second,error))return false;v.callback_i64=&nift_ffi_callback_i64_trampoline;ffi_values.push_back(&v.callback_i64);continue;}
                    if(t=="ptr"){if(d.is_null())v.pointer=nullptr;else{if(!d.is_string()||d.string.rfind("\x1fnift:ffi-ptr:",0)!=0){error="ffi_call: ptr argument must be pointer handle or null";return false;}auto pi=ffi_pointers_.find(d.string.substr(14));if(pi==ffi_pointers_.end()){error="ffi_call: invalid pointer handle";return false;}v.pointer=pi->second;}ffi_values.push_back(&v.pointer);continue;}
                    if(t=="bool"){if(!d.is_bool()){error="ffi_call: bool argument must be bool";return false;}v.u8=d.boolean?1:0;ffi_values.push_back(&v.u8);continue;}
                    if(t=="f32"||t=="f64"){if(!d.is_number()){error="ffi_call: "+t+" argument must be numeric";return false;}if(t=="f32"){v.f32=static_cast<float>(d.num);ffi_values.push_back(&v.f32);}else{v.f64=d.num;ffi_values.push_back(&v.f64);}continue;}
                    const bool uns=t[0]=='u';const unsigned bits=(t=="i8"||t=="u8")?8:(t=="i16"||t=="u16")?16:(t=="i32"||t=="u32")?32:64;std::uint64_t uv=0;std::int64_t sv=0;if((uns&&!nift::runtime_number_to_unsigned(d,bits,uv))||(!uns&&!nift::runtime_number_to_signed(d,bits,sv))){error="ffi_call: integer argument out of range for "+t;return false;}if(uns){if(bits==8){v.u8=static_cast<std::uint8_t>(uv);ffi_values.push_back(&v.u8);}else if(bits==16){v.u16=static_cast<std::uint16_t>(uv);ffi_values.push_back(&v.u16);}else if(bits==32){v.u32=static_cast<std::uint32_t>(uv);ffi_values.push_back(&v.u32);}else{v.u64=uv;ffi_values.push_back(&v.u64);}}else{if(bits==8){v.i8=static_cast<std::int8_t>(sv);ffi_values.push_back(&v.i8);}else if(bits==16){v.i16=static_cast<std::int16_t>(sv);ffi_values.push_back(&v.i16);}else if(bits==32){v.i32=static_cast<std::int32_t>(sv);ffi_values.push_back(&v.i32);}else{v.i64=sv;ffi_values.push_back(&v.i64);}}
                }
                ffi_cif cif;if(ffi_prep_cif(&cif,FFI_DEFAULT_ABI,static_cast<unsigned>(ffi_types.size()),nift_ffi_type(rt),ffi_types.empty()?nullptr:ffi_types.data())!=FFI_OK){error="ffi_call: libffi could not prepare signature";return false;}NiftFfiValue result{};const bool integer_result=rt=="bool"||rt=="i8"||rt=="i16"||rt=="i32"||rt=="i64"||rt=="u8"||rt=="u16"||rt=="u32"||rt=="u64";void* result_storage=nullptr;if(integer_result)result_storage=nift_ffi_integer_return_storage(result,nift_ffi_integer_type(rt));else if(rt=="f32")result_storage=&result.f32;else if(rt=="f64")result_storage=&result.f64;else if(rt=="ptr"||rt=="cstr")result_storage=&result.pointer;ffi_call(&cif,sym,result_storage,ffi_values.empty()?nullptr:ffi_values.data());if(!callback_activation.error().empty()){error="ffi callback: "+callback_activation.error();return false;}
                if(rt=="void"){out=nift::RuntimeValue(nullptr);return true;}if(rt=="cstr"){const char* p=static_cast<const char*>(result.pointer);out=p?nift::RuntimeValue(std::string(p)):nift::RuntimeValue(nullptr);return true;}if(rt=="ptr"){if(!result.pointer){out=nift::RuntimeValue(nullptr);return true;}const std::string id=std::to_string(next_ffi_pointer_id_++);ffi_pointers_[id]=result.pointer;out=nift::RuntimeValue(std::string("\x1fnift:ffi-ptr:")+id);return true;}if(rt=="f32"){out=nift::RuntimeValue(static_cast<double>(result.f32));return true;}if(rt=="f64"){out=nift::RuntimeValue(result.f64);return true;}const auto integer_type=nift_ffi_integer_type(rt);if(rt=="bool"){out=nift::RuntimeValue(nift_ffi_read_unsigned_return(result,integer_type)!=0);return true;}if(!rt.empty()&&rt[0]=='u')out=nift::runtime_unsigned_integer(nift_ffi_read_unsigned_return(result,integer_type));else out=nift::runtime_integer(nift_ffi_read_signed_return(result,integer_type));return true;
            }
            if(call_args("setenv",args,q)){if(!standalone_script_host_){error="setenv: only available in standalone Nift scripts/shell";return false;}if(args.size()!=2){error="setenv: expected name and value";return false;}std::string k,v;if(!string_arg("setenv",args,q,0,k)||!string_arg("setenv",args,q,1,v))return false;
#ifdef _WIN32
                if(_putenv_s(k.c_str(),v.c_str())!=0){error="setenv: failed";return false;}
#else
                if(::setenv(k.c_str(),v.c_str(),1)!=0){error="setenv: failed";return false;}
#endif
                out=nift::RuntimeValue(nullptr);return true;}
            if(call_args("unsetenv",args,q)){if(!standalone_script_host_){error="unsetenv: only available in standalone Nift scripts/shell";return false;}if(args.size()!=1){error="unsetenv: expected name";return false;}std::string k;if(!string_arg("unsetenv",args,q,0,k))return false;
#ifdef _WIN32
                _putenv_s(k.c_str(),"");
#else
                ::unsetenv(k.c_str());
#endif
                out=nift::RuntimeValue(nullptr);return true;}
            if(call_args("pwd",args,q)){if(!args.empty()){error="pwd: expected no arguments";return false;}out=nift::RuntimeValue((standalone_script_host_?fs::current_path():host_.root()).generic_string());return true;}
            if(call_args("cd",args,q)){if(!standalone_script_host_){error="cd: only available in standalone Nift scripts/shell";return false;}fs::path p;if(args.size()!=1||!checked_path("cd",args,q,0,p))return false;std::error_code ec;fs::current_path(p,ec);if(ec){error="cd: "+ec.message();return false;}out=nift::RuntimeValue(nullptr);return true;}
            if(call_args("exists",args,q)){fs::path p;if(args.size()!=1||!checked_path("exists",args,q,0,p))return false;std::error_code ec;out=nift::RuntimeValue(fs::exists(p,ec)&&!ec);return true;}
            if(call_args("make_dir",args,q)||call_args("mkdir",args,q)){fs::path p;if(args.size()!=1||!checked_path("make_dir",args,q,0,p))return false;std::error_code ec;fs::create_directories(p,ec);if(ec){error="make_dir: "+ec.message();return false;}out=nift::RuntimeValue(nullptr);return true;}
            if(call_args("touch",args,q)){fs::path p;if(args.size()!=1||!checked_path("touch",args,q,0,p))return false;std::ofstream f(p,std::ios::app);if(!f){error="touch: cannot open path";return false;}out=nift::RuntimeValue(nullptr);return true;}
            auto path_values=[&](const std::string& name,const std::vector<std::string>& aa,const std::vector<bool>& qq,std::size_t begin,std::size_t end,std::vector<fs::path>& paths)->bool{for(std::size_t ai=begin;ai<end;++ai){nift::RuntimeValue v;if(!arg_value(aa,qq,ai,v))return false;std::vector<std::string> raws;if(v.is_string())raws.push_back(v.string);else if(v.is_array()){for(const auto& x:v.array){if(!x.is_string()){error=name+": path arrays must contain strings";return false;}raws.push_back(x.string);}}else{error=name+": expected string path or array of paths";return false;}for(const auto& raw:raws){fs::path p=resolve_path(raw);if(!standalone_script_host_&&!host_.root().empty()&&!filesystem::path_within(fs::absolute(host_.root()).lexically_normal(),p)){error=name+": path must stay inside the Nift project";return false;}if(!nift_fs_root_allowed(p,standalone_script_host_,error)){error=name+": "+error;return false;}if(glob_has_magic(raw)){auto matches=glob_expand(p);paths.insert(paths.end(),matches.begin(),matches.end());}else paths.push_back(std::move(p));}}return true;};
            if(call_args("remove",args,q)||call_args("rm",args,q)){if(args.empty()){error="remove: expected at least one path";return false;}std::vector<fs::path> paths;if(!path_values("remove",args,q,0,args.size(),paths))return false;for(const auto& rp:paths){std::error_code ec;if(fs::is_directory(rp,ec)){error="remove: directories are not removed recursively";return false;}if(!fs::remove(rp,ec)&&ec){error="remove: "+ec.message();return false;}}out=nift::RuntimeValue(nullptr);return true;}
            if(call_args("copy",args,q)||call_args("cp",args,q)||call_args("move",args,q)||call_args("mv",args,q)){const bool mv=text.rfind("move(",0)==0||text.rfind("mv(",0)==0;const std::string name=mv?"move":"copy";if(text.rfind("copy(",0)==0&&args.size()<2){args.clear();q.clear();}else if(args.size()<2){error=name+": expected source(s) and destination";return false;}else{std::vector<fs::path> sources;if(!path_values(name,args,q,0,args.size()-1,sources))return false;if(sources.empty()){error=name+": glob matched no source files";return false;}std::vector<fs::path> dests;if(!path_values(name,args,q,args.size()-1,args.size(),dests)||dests.size()!=1){error=name+": destination must be one path";return false;}fs::path dest=dests.front();std::error_code dec;const bool dest_dir=fs::is_directory(dest,dec);if(sources.size()>1&&!dest_dir){error=name+": destination must be an existing directory for multiple sources";return false;}for(const auto& src:sources){fs::path target=dest_dir?dest/src.filename():dest;std::error_code ec;if(mv)fs::rename(src,target,ec);else fs::copy_file(src,target,fs::copy_options::overwrite_existing,ec);if(ec){error=name+": "+ec.message();return false;}}out=nift::RuntimeValue(nullptr);return true;}}
            if(call_args("cat",args,q)){fs::path p;if(args.size()!=1||!checked_path("cat",args,q,0,p))return false;std::error_code ec;if(fs::is_directory(p,ec)){error="cat: path is a directory";return false;}std::ifstream f(p,std::ios::binary);if(!f){error="cat: cannot open path";return false;}std::ostringstream ss;ss<<f.rdbuf();execution_output_->write_stdout(ss.str());out=nift::RuntimeValue(nullptr);return true;}
            if(call_args("ls",args,q)){if(args.size()>1){error="ls: expected zero or one path/pattern";return false;}out=nift::RuntimeValue::make_array();if(args.empty()){fs::path p=standalone_script_host_?fs::current_path():host_.root();std::error_code ec;std::vector<std::string> names;for(fs::directory_iterator it(p,ec),end;!ec&&it!=end;it.increment(ec))names.push_back(it->path().filename().generic_string());if(ec){error="ls: "+ec.message();return false;}std::sort(names.begin(),names.end());for(const auto& n:names)out.array.emplace_back(n);return true;}std::string raw;if(!string_arg("ls",args,q,0,raw))return false;fs::path p=resolve_path(raw);if(!standalone_script_host_&&!host_.root().empty()&&!filesystem::path_within(fs::absolute(host_.root()).lexically_normal(),p)){error="ls: path must stay inside the Nift project";return false;}if(!nift_fs_root_allowed(p,standalone_script_host_,error)){error="ls: "+error;return false;}if(glob_has_magic(raw)){auto matches=glob_expand(p);const fs::path base=standalone_script_host_?fs::current_path():host_.root();const bool absolute=fs::path(raw).is_absolute();for(const auto&m:matches){std::error_code rec;auto shown=absolute?m:fs::relative(m,base,rec);out.array.emplace_back((rec?m:shown).generic_string());}return true;}std::error_code ec;if(!fs::is_directory(p,ec)||ec){error="ls: path is not a readable directory";return false;}std::vector<std::string> names;for(fs::directory_iterator it(p,ec),end;!ec&&it!=end;it.increment(ec))names.push_back(it->path().filename().generic_string());if(ec){error="ls: "+ec.message();return false;}std::sort(names.begin(),names.end());for(const auto& n:names)out.array.emplace_back(n);return true;}
            if(call_args("open",args,q)){fs::path p;if(args.size()!=1||!checked_path("open",args,q,0,p))return false;std::error_code ec;if(fs::is_directory(p,ec)){error="open: path is a directory";return false;}std::ifstream f(p,std::ios::binary);if(!f){error="open: cannot open path";return false;}std::ostringstream ss;ss<<f.rdbuf();out=nift::RuntimeValue(ss.str());return true;}
            if(call_args("open_bytes",args,q)){fs::path p;if(args.size()!=1||!checked_path("open_bytes",args,q,0,p))return false;std::error_code ec;if(fs::is_directory(p,ec)){error="open_bytes: path is a directory";return false;}std::ifstream f(p,std::ios::binary);if(!f){error="open_bytes: cannot open path";return false;}constexpr std::size_t chunk_size=64*1024;std::vector<char> chunk(chunk_size);nift::RuntimeBytes data;while(f){f.read(chunk.data(),static_cast<std::streamsize>(chunk.size()));const auto received=f.gcount();if(received>0)data.insert(data.end(),chunk.begin(),chunk.begin()+received);if(f.bad()){error="open_bytes: input failure";return false;}if(received<static_cast<std::streamsize>(chunk.size()))break;}out=nift::RuntimeValue(std::move(data));return true;}
            // Native script-land Minify++ (v4.4 package campaign CP19-CP24).
            // Delegates to the same embedded Minify++ the CLI uses; never a
            // subprocess, so it also works under --no-process. Overloads:
            //   minify(source, "js")                     -> string->string
            //   minify("app.js")                         -> app.min.js
            //   minify("app.js", {"output": "out.js"})   -> explicit output
            //   minify("app.css", {"in_place": true})    -> overwrite input
            // Returns {ok, output, error, format, written}; ordinary minify
            // failures are represented in the result, not a process abort.
            auto minify_format_from_name=[&](const std::string& name,minify::Format& f)->bool{
                if(name=="html"){f=minify::Format::Html;return true;}if(name=="css"){f=minify::Format::Css;return true;}
                if(name=="js"){f=minify::Format::JavaScript;return true;}if(name=="jsx"){f=minify::Format::Jsx;return true;}
                if(name=="json"){f=minify::Format::Json;return true;}if(name=="xml"){f=minify::Format::Xml;return true;}
                if(name=="svg"){f=minify::Format::Svg;return true;}return false;};
            auto minify_format_name=[&](minify::Format f)->std::string{
                switch(f){case minify::Format::Html:return "html";case minify::Format::Css:return "css";case minify::Format::JavaScript:return "js";case minify::Format::Jsx:return "jsx";case minify::Format::Json:return "json";case minify::Format::Xml:return "xml";case minify::Format::Svg:return "svg";}return "?";};
            if(call_args("minify",args,q)){
                if(args.empty()||args.size()>2){error="minify: expected source or path plus optional format string or options object";return false;}
                nift::RuntimeValue first;if(!arg_value(args,q,0,first))return false;
                if(!first.is_string()){error="minify: source or path must be a string";return false;}
                std::string format_name;bool explicit_format=false;bool has_opts=false;nift::RuntimeValue opts;
                if(args.size()==2){
                    if(!arg_value(args,q,1,opts))return false;
                    if(opts.is_string()){format_name=opts.string;explicit_format=true;}
                    else if(opts.is_object()){has_opts=true;if(opts.has("format")){const auto& fv=opts["format"];if(!fv.is_string()){error="minify: format must be a string";return false;}format_name=fv.string;explicit_format=true;}}
                    else{error="minify: second argument must be a format string or options object";return false;}
                }
                // Mode rule: an explicit format STRING means the first argument
                // is a source string (string->string). A bare path or an
                // options object means the first argument is a file path.
                const std::string arg0=first.string;
                auto minify_fail=[&](const std::string& msg,const std::string& fmt,const std::string& written)->nift::RuntimeValue{nift::RuntimeValue r=nift::RuntimeValue::make_object();r["ok"]=nift::RuntimeValue(false);r["output"]=nift::RuntimeValue("");r["error"]=nift::RuntimeValue(msg);r["format"]=nift::RuntimeValue(fmt);r["written"]=nift::RuntimeValue(written);return r;};
                if(explicit_format&&!has_opts){
                    minify::Format fmt;if(!minify_format_from_name(format_name,fmt)){out=minify_fail("unsupported format '"+format_name+"'",format_name,"");return true;}
                    std::string out_text,merr;if(!minify::run(fmt,arg0,out_text,merr)){out=minify_fail(merr,format_name,"");return true;}
                    nift::RuntimeValue r=nift::RuntimeValue::make_object();r["ok"]=nift::RuntimeValue(true);r["output"]=nift::RuntimeValue(out_text);r["error"]=nift::RuntimeValue("");r["format"]=nift::RuntimeValue(format_name);r["written"]=nift::RuntimeValue("");out=std::move(r);return true;
                }
                fs::path p0=resolve_path(arg0);
                const bool arg0_is_file=fs::is_regular_file(p0);
                if(!arg0_is_file){out=minify_fail("file does not exist or is not a regular file: "+arg0,"","");return true;}
                fs::path in=p0;
                if(!standalone_script_host_&&!host_.root().empty()&&!filesystem::path_within(fs::absolute(host_.root()).lexically_normal(),in)){error="minify: path must stay inside the Nift project";return false;}
                if(!nift_fs_root_allowed(in,standalone_script_host_,error)){error="minify: "+error;return false;}
                minify::Format fmt;
                if(explicit_format){if(!minify_format_from_name(format_name,fmt)){out=minify_fail("unsupported format '"+format_name+"'",format_name,"");return true;}}
                else{std::string ext=in.extension().string();if(!minify::format_for_extension(ext,fmt)){out=minify_fail("cannot infer format from extension "+ext,ext,"");return true;}format_name=minify_format_name(fmt);}
                const std::string source=filesystem::read_file(in);
                std::string out_text,merr;if(!minify::run(fmt,source,out_text,merr)){out=minify_fail(merr,format_name,"");return true;}
                bool in_place=false;bool want_output=false;fs::path dest;
                if(has_opts){
                    if(opts.has("in_place")){const auto& v=opts["in_place"];if(!v.is_bool()){error="minify: in_place must be a boolean";return false;}in_place=v.boolean;}
                    if(opts.has("output")){const auto& v=opts["output"];if(!v.is_string()){error="minify: output must be a string";return false;}want_output=true;}
                }
                if(in_place&&want_output){error="minify: choose either in_place or output, not both";return false;}
                if(in_place)dest=in;
                else if(want_output){fs::path od=resolve_path(opts["output"].string);if(!standalone_script_host_&&!host_.root().empty()&&!filesystem::path_within(fs::absolute(host_.root()).lexically_normal(),od)){error="minify: output must stay inside the Nift project";return false;}if(!nift_fs_root_allowed(od,standalone_script_host_,error)){error="minify: "+error;return false;}dest=od;}
                else dest=in.parent_path()/(in.stem().string()+".min"+in.extension().string());
                if(!filesystem::write_file(dest,out_text)){out=minify_fail("failed to write "+dest.generic_string(),format_name,"");return true;}
                nift::RuntimeValue r=nift::RuntimeValue::make_object();r["ok"]=nift::RuntimeValue(true);r["output"]=nift::RuntimeValue(out_text);r["error"]=nift::RuntimeValue("");r["format"]=nift::RuntimeValue(format_name);r["written"]=nift::RuntimeValue(dest.generic_string());out=std::move(r);return true;
            }
            if(call_args("file",args,q)){fs::path p;if(args.size()!=1||!checked_path("file",args,q,0,p))return false;auto f=std::make_shared<FileInstance>();f->path=p;auto id=std::to_string(next_file_instance_id_++);file_instances_[id]=f;out=nift::RuntimeValue(std::string("\x1fnift:file:")+id);return true;}
            if(call_args("page",args,q)){if(args.size()!=1){error="page: expected one page name";return false;}nift::RuntimeValue d;if(!arg_value(args,q,0,d)||!d.is_string()){error="page: name must be a string";return false;}std::string ref;if(!host_.page_ref_for(d.string,ref)){error="page: unknown tracked page '"+d.string+"'";return false;}out=nift::RuntimeValue(ref);return true;}
            if(call_args("min",args,q)||call_args("max",args,q)){const bool want_min=text.rfind("min(",0)==0;const std::string name=want_min?"min":"max";std::vector<nift::RuntimeValue> vals;if(args.size()==1){nift::RuntimeValue v;if(!arg_value(args,q,0,v))return false;if(v.is_array())vals=v.array;else vals.push_back(std::move(v));}else for(std::size_t ai=0;ai<args.size();++ai){nift::RuntimeValue v;if(!arg_value(args,q,ai,v))return false;vals.push_back(std::move(v));}if(vals.empty()){error=name+": expected at least one value";return false;}out=vals.front();for(std::size_t ai=1;ai<vals.size();++ai){const auto& v=vals[ai];bool take=false;if(out.is_number()&&v.is_number()){const int cmp=nift::runtime_compare_numbers(v,out);take=want_min?cmp<0:cmp>0;}else if(out.is_string()&&v.is_string())take=want_min?v.string<out.string:v.string>out.string;else{error=name+": values must be comparable and homogeneous";return false;}if(take)out=v;}return true;}
            // CP204-205: stable public runtime introspection. Internal marker
            // encodings are mapped to public language categories, not exposed.
            auto public_type=[&](const nift::RuntimeValue& v)->std::string{
                if(v.is_null()) return "null";
                if(v.is_bool()) return "bool";
                if(v.is_number()) return nift::runtime_number_is_integer(v) ? "int" : "float";
                if(v.is_array()) return "array";
                if(v.is_object()) return "object";
                if(v.is_bytes()) return "bytes";
                if(v.is_timer()) return "timer";
                if(v.is_string()){
                    if(v.string.rfind("\x1fnift:struct:",0)==0) return "struct";
                    if(v.string.rfind("\x1fnift:callable:",0)==0) return "function";
                    if(v.string.rfind("\x1fnift:thread:",0)==0) return "thread";
                    if(v.string.rfind("\x1fnift:mutex:",0)==0) return "mutex";
                    if(v.string.rfind("\x1fnift:atomic:",0)==0){auto ai=atomic_instances_.find(v.string.substr(13));if(ai!=atomic_instances_.end())return ai->second->kind==AtomicInstance::Kind::Int?"atomic<int>":"atomic<bool>";return "atomic";}
                    if(v.string.rfind("\x1fnift:async:",0)==0) return "future";
                    if(v.string.rfind("\x1fnift:collection:",0)==0) return "collection";
                    if(v.string.rfind("\x1fnift:file:",0)==0) return "file";
                    if(v.string.rfind("\x1fnift:stream:",0)==0) return "stream";
                    if(v.string.rfind("\x1fnift:page:",0)==0) return "page";
                    if(v.string.rfind("\x1fnift:enum:",0)==0) return "enum";
                    return "string";
                }
                return "object";
            };
            if(call_args("type",args,q)){
                if(args.size()!=1){error="type: expected one value";return false;}
                nift::RuntimeValue v;if(!arg_value(args,q,0,v))return false;out=nift::RuntimeValue(public_type(v));return true;
            }
            for(const std::string pred:{"is_null","is_bool","is_number","is_int","is_float","is_string","is_bytes","is_array","is_object","is_struct","is_function","is_collection","is_enum"}){
                if(call_args(pred,args,q)){
                    if(args.size()!=1){error=pred+": expected one value";return false;}
                    nift::RuntimeValue v;if(!arg_value(args,q,0,v))return false;const std::string t=public_type(v);
                    bool yes=pred=="is_null"?t=="null":pred=="is_bool"?t=="bool":pred=="is_number"?(t=="int"||t=="float"):pred=="is_int"?t=="int":pred=="is_float"?t=="float":pred=="is_string"?t=="string":pred=="is_bytes"?t=="bytes":pred=="is_array"?t=="array":pred=="is_object"?t=="object":pred=="is_struct"?t=="struct":pred=="is_function"?t=="function":pred=="is_collection"?t=="collection":t=="enum";
                    out=nift::RuntimeValue(yes);return true;
                }
            }
            // CP210: Python-style integer ranges, stop-exclusive and materialized.
            if(call_args("range",args,q)){
                if(args.empty()||args.size()>3){error="range: expected stop, start/stop, or start/stop/step";return false;}
                std::vector<std::int64_t> n; for(std::size_t ai=0;ai<args.size();++ai){nift::RuntimeValue v;std::int64_t converted=0;if(!arg_value(args,q,ai,v)||!nift::runtime_number_to_i64(v,converted)){error="range: arguments must be signed 64-bit integers";return false;}n.push_back(converted);}
                std::int64_t start=args.size()==1?0:n[0], stop=args.size()==1?n[0]:n[1], step=args.size()==3?n[2]:1;
                if(step==0){error="range: step must not be zero";return false;}
                out=nift::RuntimeValue::make_array(); constexpr std::size_t limit=10000000;
                for(std::int64_t x=start; step>0?x<stop:x>stop;){if(out.array.size()>=limit){error="range: result exceeds 10000000 items";return false;}out.array.emplace_back((double)x);if((step>0&&x>std::numeric_limits<std::int64_t>::max()-step)||(step<0&&x<std::numeric_limits<std::int64_t>::min()-step)){error="range: integer overflow";return false;}x+=step;}
                return true;
            }
            if(call_args("bytes",args,q)){
                if(args.size()>1){error="bytes: expected zero arguments or one byte array";return false;}
                nift::RuntimeBytes data;
                if(args.size()==1){nift::RuntimeValue source;if(!arg_value(args,q,0,source))return false;if(!source.is_array()){error="bytes: expected an array of integers";return false;}data.reserve(source.array.size());for(const auto& item:source.array){std::uint64_t byte=0;if(!nift::runtime_number_to_unsigned(item,8,byte)){error="bytes: array values must be integers from 0 to 255";return false;}data.push_back(static_cast<std::uint8_t>(byte));}}
                out=nift::RuntimeValue(std::move(data));return true;
            }
            if(call_args("html_escape",args,q)||call_args("attr_escape",args,q)||call_args("url_encode",args,q)){
                const bool is_attr=text.rfind("attr_escape(",0)==0; const bool is_url=text.rfind("url_encode(",0)==0; const char* fname=is_attr?"attr_escape":(is_url?"url_encode":"html_escape");
                if(args.size()!=1){error=std::string(fname)+": expected one value";return false;}
                nift::RuntimeValue v;if(!arg_value(args,q,0,v))return false;
                std::string s;
                if(v.is_string())s=v.string;
                else if(v.is_number()||v.is_bool())s=render_expression_value(v);
                else if(v.is_null())s="null";
                else{error=std::string(fname)+": value must be a string or scalar";return false;}
                std::string r; r.reserve(s.size()+8);
                if(is_url){
                    // RFC 3986 unreserved characters pass through; everything else is
                    // percent-encoded byte-wise (URL component encoding, not
                    // application/x-www-form-urlencoded: space -> %20).
                    static const char* hex="0123456789ABCDEF";
                    for(unsigned char c : s){
                        if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~'){r+=(char)c;}
                        else{r+='%';r+=hex[c>>4];r+=hex[c&0xF];}
                    }
                } else {
                    for(char c : s){
                        if(c=='&')r+="&amp;"; else if(c=='<')r+="&lt;"; else if(c=='>')r+="&gt;";
                        else if(is_attr && c=='"')r+="&quot;"; else if(is_attr && c=='\'')r+="&#39;";
                        else r+=c;
                    }
                }
                out=nift::RuntimeValue(std::move(r));return true;
            }
            if(call_args("ifstream",args,q)||call_args("ofstream",args,q)){const bool output=text.rfind("ofstream(",0)==0;fs::path p;if(args.size()!=1||!checked_path(output?"ofstream":"ifstream",args,q,0,p))return false;auto st=std::make_shared<StreamInstance>();st->kind=output?StreamInstance::Kind::Output:StreamInstance::Kind::Input;if(output){st->output=std::make_shared<std::ofstream>(p,std::ios::binary|std::ios::trunc);if(!*st->output){error="ofstream: cannot open path";return false;}}else{st->input=std::make_shared<std::ifstream>(p,std::ios::binary);if(!*st->input){error="ifstream: cannot open path";return false;}}auto id=std::to_string(next_stream_instance_id_++);stream_instances_[id]=st;out=nift::RuntimeValue(std::string("\x1fnift:stream:")+id);return true;}
            if(call_args("close",args,q)){if(args.size()!=1){error="close: expected stream";return false;}nift::RuntimeValue d;if(!arg_value(args,q,0,d)||!d.is_string()||d.string.rfind("\x1fnift:stream:",0)!=0){error="close: expected stream";return false;}auto it=stream_instances_.find(d.string.substr(13));if(it==stream_instances_.end()){error="close: invalid stream";return false;}if(it->second->closed){error="close: stream already closed";return false;}if(it->second->input)it->second->input->close();if(it->second->output)it->second->output->close();it->second->closed=true;out=nift::RuntimeValue(nullptr);return true;}
            const bool print_call=call_args("print",args,q);
            const bool err_call=!print_call&&call_args("err",args,q);
            if(print_call||err_call){const std::string name=err_call?"err":"print";if(args.size()!=1){error=name+": expected one value";return false;}nift::RuntimeValue d;if(!arg_value(args,q,0,d))return false;if(d.is_string()&&q.size()>0&&q[0]&&d.string.find("$[")!=std::string::npos){std::string r,e;if(!interpolate_parameter(d.string,r,e)){error=name+": "+e;return false;}d=nift::RuntimeValue(r);}if(d.is_bytes()){error=name+": bytes values cannot be rendered as text";return false;}if(d.is_timer()||d.is_array()||d.is_object()||(d.is_string()&&d.string.rfind("\x1fnift:",0)==0&&d.string.rfind("\x1fnift:timer:",0)!=0&&d.string.rfind("\x1fnift:enum:",0)!=0&&d.string.rfind("\x1fnift:atomic:",0)!=0)){error=name+": value is not directly renderable";return false;}const std::string rendered=render_expression_value(d)+'\n';if(err_call)execution_output_->write_stderr(rendered);else execution_output_->write_stdout(rendered);out=nift::RuntimeValue(nullptr);return true;}
            if(call_args("read",args,q)){if(!args.empty()){error="read: expected no arguments";return false;}if(!standalone_script_host_){error="read: interactive input is only available in standalone Nift scripts/shell";return false;}std::string line;if(!std::getline(std::cin,line)){if(std::cin.eof()){std::cin.clear();out=nift::RuntimeValue(nullptr);return true;}error="read: input failure";return false;}out=nift::RuntimeValue(line);return true;}
        }

        // v4.3 canonical value presentation. Presentation modifiers compose over the
        // original value rather than serializing each other's string output. Thus
        // x.prettify().highlight() and x.highlight().prettify() both mean
        // "pretty + highlighted when directly inspected by the REPL". ANSI remains
        // a presentation concern; expression evaluation always returns a plain string.
        {
            std::string target; bool pretty=false, highlight=false;
            if (strip_presentation_chain(text, target, pretty, highlight)) {
                nift::RuntimeValue v;if(!eval(target,v,depth+1))return false;
                std::string rendered;if(!serialize_value(v,pretty,rendered,error))return false;
                out=nift::RuntimeValue(rendered);return true;
            }
        }

        // v4.3 CP100/CP102 scalar/string and read-only postfix ergonomics.
        // Parse only the final .method(...) postfix here; recursively evaluating the
        // receiver makes literals and call/expression results chain naturally without
        // teaching each primitive about every possible trailing method.
        {
            auto final_postfix = [&](std::string& receiver, std::string& method, std::string& arg_text)->bool {
                if (text.empty() || text.back() != ')') return false;
                bool quoted=false; char quote=0; int parens=0;
                std::size_t lp=std::string::npos;
                for (std::size_t i=text.size(); i-- > 0;) {
                    const char c=text[i];
                    if (quoted) { if (c==quote && (i==0 || text[i-1]!='\\')) quoted=false; continue; }
                    if (c=='\'' || c=='"') { quoted=true; quote=c; continue; }
                    if (c==')') { ++parens; continue; }
                    if (c=='(') { if (--parens==0) { lp=i; break; } continue; }
                }
                if (lp==std::string::npos) return false;
                std::size_t end=lp;
                while (end>0 && std::isspace((unsigned char)text[end-1])) --end;
                std::size_t begin=end;
                while (begin>0 && (std::isalnum((unsigned char)text[begin-1]) || text[begin-1]=='_')) --begin;
                if (begin==end || begin==0 || text[begin-1]!='.') return false;
                receiver=trim_copy(text.substr(0,begin-1));
                method=text.substr(begin,end-begin);
                arg_text=text.substr(lp+1,text.size()-lp-2);
                if(receiver.find(":=")!=std::string::npos) return false;
                { bool q=false; char qc=0; int pa=0,br=0,bc=0; for(std::size_t j=0;j<receiver.size();++j){ char c=receiver[j]; if(q){ if(c=='\\') ++j; else if(c==qc) q=false; continue; } if(c=='\'' || c=='"'){ q=true; qc=c; continue; } if(c=='(')++pa; else if(c==')')--pa; else if(c=='[')++br; else if(c==']')--br; else if(c=='{')++bc; else if(c=='}')--bc; else if(!pa&&!br&&!bc&&std::string("+-*/%<>=!&|?:,").find(c)!=std::string::npos) return false; } }
                return !receiver.empty();
            };
            std::string receiver, method, arg_text;
            if (final_postfix(receiver,method,arg_text)) {
                const bool known = method=="length"||method=="split"||method=="index_of"||method=="last_index_of"||
                    method=="contains"||method=="starts_with"||method=="ends_with"||method=="trim"||method=="encode"||method=="decode"||
                    method=="trim_start"||method=="trim_end"||method=="to_lower"||method=="to_upper"||
                    method=="replace"||method=="to_int"||method=="to_double"||method=="to_string"||method=="abs"||method=="floor"||method=="ceil"||method=="round"||
                    method=="substr"||method=="size"||method=="empty"||method=="first"||method=="last"||
                    method=="join"||method=="done"||method=="status"||method=="start"||method=="elapsed"||method=="pause"||method=="resume"||method=="stop"||method=="reset"||method=="running"||method=="paused"||method=="load"||method=="store"||method=="exchange"||method=="fetch_add"||method=="fetch_sub"||method=="compare_exchange"||method=="lock"||method=="try_lock"||method=="unlock"||method=="locked"||method=="set"||method=="slice"||method=="indexOf"||method=="path"||method=="exists"||method=="open"||
                    method=="close"||method=="read"||method=="read_line"||method=="read_all"||method=="read_bytes"||method=="read_all_bytes"||method=="read_val"||
                    method=="eof"||method=="write"||method=="write_line"||method=="write_val"||method=="flush"||method=="tell"||
                    method=="seek"||method=="modified"||method=="save"||method=="revert"||method=="replace_once"||
                    method=="insert"||method=="insert_before"||method=="insert_after"||method=="prepend"||method=="append"||
                    method=="copy"||method=="move"||method=="remove"||method=="cat"||
                    method=="keys"||method=="values"||method=="entries"||method=="has"||method=="get"||method=="merge"||
                    method=="map"||method=="filter"||method=="reduce"||method=="any"||method=="all"||method=="find"||method=="find_index"||method=="count"||method=="sort_by"||method=="group_by"||method=="unique"||method=="flatten"||method=="sum"||method=="min"||method=="max"||method=="from_entries"||method=="index_by"||method=="pick"||method=="omit"||method=="merge_deep"||method=="partition"||method=="unique_by"||method=="min_by"||method=="max_by"||method=="count_by"||method=="take"||method=="drop"||method=="chunk"||method=="group_by_each"||method=="pipe"||method=="stdin"||method=="stdout"||method=="stderr"||method=="cwd"||method=="env"||method=="run";
                if (!known) {
                    // Module-style value: a JSON object member holding a callable is
                    // invoked as a method (e.g. vips.resize(...) for a plain-object
                    // module), keeping any captured package-private context.
                    // Only a plain-object (or struct) receiver can be a module, so
                    // skip this path for an array receiver: evaluating the receiver
                    // would deep-copy the entire array on every call, turning
                    // mutation methods such as a.push(...) into O(n) per call.
                    const std::size_t rdot = receiver.find_first_of(".[(");
                    const std::string rroot = trim_copy(receiver.substr(0, rdot == std::string::npos ? receiver.size() : rdot));
                    VariableBinding* rb2 = nullptr;
                    for (auto scope = variable_scopes_.rbegin(); scope != variable_scopes_.rend(); ++scope) {
                        const auto it = scope->find(rroot);
                        if (it != scope->end()) { rb2 = &it->second; break; }
                    }
                    const bool receiver_is_module = !rb2 || (rb2->value &&
                        (rb2->value->is_object() ||
                         (rb2->value->is_string() && rb2->value->string.rfind("\x1fnift:struct:", 0) == 0)));
                    if (!receiver_is_module) { /* array/collection/scalar receiver cannot be a module */ }
                    else {
                    nift::RuntimeValue base;
                    if (eval(receiver, base, depth + 1) && base.is_object()) {
                        for (const auto& kv : base.object) {
                            if (kv.first == method && kv.second.is_string() && kv.second.string.rfind("\x1fnift:callable:",0)==0) {
                                push_variable_scope(); auto& sc=variable_scopes_.back();
                                auto csp=std::make_shared<nift::RuntimeValue>(kv.second);
                                sc["__nift_module_field"]=VariableBinding{csp,nift_binding_type(*csp),false,false};
                                bool ok=eval("__nift_module_field("+arg_text+")",out,depth+1);
                                pop_variable_scope();
                                return ok;
                            }
                        }
                    }
                    }
                }
                if (known) {
                    // Fast path: read-only container methods on a pure JSON-path
                    // receiver (e.g. project.files.size()) resolve through the
                    // aliasing JSON walker instead of deep-copying the receiver,
                    // so large host-binding collections are not copied per page.
                    if (method=="size"||method=="length"||method=="empty"||method=="first"||method=="last"||
                        method=="keys"||method=="values"||method=="entries") {
                        std::shared_ptr<const nift::RuntimeValue> path_value;
                        std::string path_error;
                        if (resolve_json_value(receiver, path_value, path_error) && path_error.empty() && path_value) {
                            const nift::RuntimeValue& pv = *path_value;
                            if (pv.is_array()) {
                                if (method=="size"||method=="length"){out=nift::RuntimeValue((double)pv.array.size());return true;}
                                if (method=="empty"){out=nift::RuntimeValue(pv.array.empty());return true;}
                                if (method=="first"||method=="last"){if(pv.array.empty()){error=method+": array is empty";return false;}out=method=="first"?pv.array.front():pv.array.back();return true;}
                            } else if (pv.is_object()) {
                                if (method=="size"){out=nift::RuntimeValue((double)pv.object.size());return true;}
                                if (method=="empty"){out=nift::RuntimeValue(pv.object.empty());return true;}
                                if (method=="keys"||method=="values"||method=="entries"){out=nift::RuntimeValue::make_array();for(const auto&kv:pv.object){if(method=="keys")out.array.emplace_back(kv.first);else if(method=="values")out.array.push_back(kv.second);else{nift::RuntimeValue e=nift::RuntimeValue::make_object();e["key"]=nift::RuntimeValue(kv.first);e["value"]=kv.second;out.array.push_back(std::move(e));}}return true;}
                            }
                        }
                    }
                    nift::RuntimeValue base;
                    if (!eval(receiver,base,depth+1)) return false;
                    bool aok=false; std::vector<bool> aq;
                    auto args=parse_parameters(arg_text,aok,&aq);
                    if(!aok){error=method+": malformed arguments";return false;}
                    auto eval_arg=[&](std::size_t i,nift::RuntimeValue& v)->bool{
                        if(i>=args.size())return false;
                        if(i<aq.size()&&aq[i]){v=nift::RuntimeValue(args[i]);return true;}
                        return eval(args[i],v,depth+1);
                    };
                    auto no_args=[&]()->bool{if(!args.empty()){error=method+": expected no arguments";return false;}return true;};
                    if(base.is_timer()){std::vector<nift::RuntimeValue> values;for(std::size_t i=0;i<args.size();++i){nift::RuntimeValue value;if(!eval_arg(i,value))return false;values.push_back(std::move(value));}return call_timer_method(base,method,values,out,error);}
                    if(base.is_bytes()){
                        const nift::RuntimeBytes empty;
                        const auto& data=base.bytes?*base.bytes:empty;
                        if(method=="length"||method=="size"){if(!no_args())return false;out=nift::RuntimeValue(static_cast<double>(data.size()));return true;}
                        if(method=="empty"){if(!no_args())return false;out=nift::RuntimeValue(data.empty());return true;}
                        if(method=="slice"){if(args.empty()||args.size()>2){error="slice: expected start and optional end";return false;}nift::RuntimeValue st,en;std::size_t begin=0,end=data.size();if(!eval_arg(0,st)||!nift::runtime_number_to_size(st,begin)){error="slice: invalid start";return false;}if(args.size()==2&&(!eval_arg(1,en)||!nift::runtime_number_to_size(en,end))){error="slice: invalid end";return false;}begin=std::min(begin,data.size());end=std::min(end,data.size());if(end<begin)end=begin;out=nift::RuntimeValue(nift::RuntimeBytes(data.begin()+begin,data.begin()+end));return true;}
                        if(method=="decode"){if(args.size()!=1){error="decode: expected encoding";return false;}nift::RuntimeValue encoding;if(!eval_arg(0,encoding)||!encoding.is_string()||encoding.string!="utf-8"){error="decode: encoding must be exactly 'utf-8'";return false;}std::string decoded=runtime_bytes_string(base);if(!nift::runtime_valid_utf8(decoded)){error="decode: invalid UTF-8";return false;}out=nift::RuntimeValue(std::move(decoded));return true;}
                    }
                    if(base.is_string() && base.string.rfind("\x1fnift:thread:",0)==0){
                        auto ti=thread_instances_.find(base.string.substr(13));if(ti==thread_instances_.end()){error="thread: invalid handle";return false;}auto st=ti->second;
                        if(method=="done"){if(!no_args())return false;std::lock_guard<std::mutex> lock(st->mutex);out=nift::RuntimeValue(st->done);return true;}
                        if(method=="status"){if(!no_args())return false;std::lock_guard<std::mutex> lock(st->mutex);out=nift::RuntimeValue(st->done?(st->error.empty()?"done":"error"):"running");return true;}
                        if(method=="join"){if(!no_args())return false;{std::lock_guard<std::mutex> guard(st->join_mutex);if(st->worker.joinable())st->worker.join();}std::lock_guard<std::mutex> lock(st->mutex);st->joined=true;if(!st->error.empty()){error="thread: "+st->error;return false;}out=st->result?*st->result:nift::RuntimeValue(nullptr);return true;}
                    }
                    if(base.is_string() && base.string.rfind("\x1fnift:async:",0)==0){
                        auto ai=async_instances_.find(base.string.substr(12));if(ai==async_instances_.end()){error="future: invalid handle";return false;}auto st=ai->second;
                        if(method=="done"){if(!no_args())return false;std::lock_guard<std::mutex> lk(st->mutex);out=nift::RuntimeValue(st->done);return true;}
                        if(method=="status"){if(!no_args())return false;std::lock_guard<std::mutex> lk(st->mutex);out=nift::RuntimeValue(st->done?(st->error.empty()?"done":"error"):"running");return true;}
                    }
                    if(base.is_string() && base.string.rfind("\x1fnift:atomic:",0)==0){
                        auto ai=atomic_instances_.find(base.string.substr(13));if(ai==atomic_instances_.end()){error="atomic: invalid handle";return false;}auto st=ai->second;
                        auto int_doc=[](std::int64_t v){nift::RuntimeValue d(static_cast<double>(v));if(v>9007199254740992LL||v<-9007199254740992LL){d.type=nift::RuntimeType::StrNumber;d.string=std::to_string(v);}return d;};
                        if(method=="load"){if(!no_args())return false;if(st->kind==AtomicInstance::Kind::Int)out=int_doc(st->int_value.load());else out=nift::RuntimeValue(st->bool_value.load());return true;}
                        if(method=="store"||method=="exchange"){if(args.size()!=1){error=method+": expected one value";return false;}nift::RuntimeValue v;if(!eval_arg(0,v))return false;if(st->kind==AtomicInstance::Kind::Int){std::int64_t x=0;if(!exact_i64(v,x)){error=method+": atomic<int> requires a signed 64-bit integer";return false;}if(method=="store"){st->int_value.store(x);out=nift::RuntimeValue(nullptr);}else out=int_doc(st->int_value.exchange(x));}else{if(!v.is_bool()){error=method+": atomic<bool> requires bool";return false;}if(method=="store"){st->bool_value.store(v.boolean);out=nift::RuntimeValue(nullptr);}else out=nift::RuntimeValue(st->bool_value.exchange(v.boolean));}return true;}
                        if(method=="fetch_add"||method=="fetch_sub"){if(st->kind!=AtomicInstance::Kind::Int){error=method+": only valid for atomic<int>";return false;}if(args.size()!=1){error=method+": expected one integer";return false;}nift::RuntimeValue v;if(!eval_arg(0,v))return false;std::int64_t x=0;if(!exact_i64(v,x)){error=method+": expected signed 64-bit integer";return false;}std::int64_t before=0,after=0;if(!nift_atomic_add_sub_checked(st->int_value,x,method=="fetch_sub",before,after)){error=method+": integer overflow";return false;}out=int_doc(before);return true;}
                        if(method=="compare_exchange"){if(args.size()!=2){error="compare_exchange: expected expected and desired values";return false;}nift::RuntimeValue ev,dv;if(!eval_arg(0,ev)||!eval_arg(1,dv))return false;if(st->kind==AtomicInstance::Kind::Int){std::int64_t e=0,d=0;if(!exact_i64(ev,e)||!exact_i64(dv,d)){error="compare_exchange: atomic<int> requires signed 64-bit integers";return false;}out=nift::RuntimeValue(st->int_value.compare_exchange_strong(e,d));}else{if(!ev.is_bool()||!dv.is_bool()){error="compare_exchange: atomic<bool> requires bool values";return false;}bool e=ev.boolean;out=nift::RuntimeValue(st->bool_value.compare_exchange_strong(e,dv.boolean));}return true;}
                    }
                    if(base.is_string() && base.string.rfind("\x1fnift:mutex:",0)==0){
                        auto mi=mutex_instances_.find(base.string.substr(12));if(mi==mutex_instances_.end()){error="mutex: invalid handle";return false;}auto st=mi->second;const auto self=std::this_thread::get_id();
                        if(method=="lock"){if(!no_args())return false;std::unique_lock<std::mutex> lk(st->state_mutex);if(st->locked&&st->owner==self){error="mutex: recursive lock is not supported";return false;}st->cv.wait(lk,[&]{return !st->locked;});st->locked=true;st->owner=self;out=nift::RuntimeValue(nullptr);return true;}
                        if(method=="try_lock"){if(!no_args())return false;std::lock_guard<std::mutex> lk(st->state_mutex);if(st->locked){out=nift::RuntimeValue(false);return true;}st->locked=true;st->owner=self;out=nift::RuntimeValue(true);return true;}
                        if(method=="unlock"){if(!no_args())return false;{std::lock_guard<std::mutex> lk(st->state_mutex);if(!st->locked){error="mutex: unlock of unlocked mutex";return false;}if(st->owner!=self){error="mutex: unlock by non-owner";return false;}st->locked=false;st->owner=std::thread::id{};}st->cv.notify_one();out=nift::RuntimeValue(nullptr);return true;}
                        if(method=="locked"){if(!no_args())return false;std::lock_guard<std::mutex> lk(st->state_mutex);out=nift::RuntimeValue(st->locked);return true;}
                        if(method=="get"){if(!no_args())return false;std::lock_guard<std::mutex> lk(st->state_mutex);if(!st->locked||st->owner!=self){error="mutex: get requires the current thread to hold the lock";return false;}out=st->value;return true;}
                        if(method=="set"){if(args.size()!=1){error="set: expected one value";return false;}nift::RuntimeValue v;if(!eval_arg(0,v))return false;if(contains_timer_resource(v)){error="mutex: set value contains a non-transferable timer";return false;}std::function<bool(const nift::RuntimeValue&)> ok;ok=[&](const nift::RuntimeValue& x){if(x.is_string()&&x.string.rfind("\x1fnift:",0)==0&&x.string.rfind("\x1fnift:enum:",0)!=0&&x.string.rfind("\x1fnift:callable:",0)!=0&&x.string.rfind("\x1fnift:thread:",0)!=0&&x.string.rfind("\x1fnift:mutex:",0)!=0&&x.string.rfind("\x1fnift:async:",0)!=0)return false;if(x.is_array())for(const auto&e:x.array)if(!ok(e))return false;if(x.is_object())for(const auto&kv:x.object)if(!ok(kv.second))return false;return true;};if(!ok(v)){error="mutex: set value contains a non-transferable resource";return false;}std::lock_guard<std::mutex> lk(st->state_mutex);if(!st->locked||st->owner!=self){error="mutex: set requires the current thread to hold the lock";return false;}st->value=std::move(v);out=nift::RuntimeValue(nullptr);return true;}
                    }
                    if(base.is_string()&&base.string.rfind("\x1fnift:enum:",0)==0&&(method=="to_int"||method=="to_string")){
                        if(!no_args())return false;
                        const auto last=base.string.rfind(':');const auto prev=last==std::string::npos?last:base.string.rfind(':',last-1);if(last==std::string::npos||prev==std::string::npos){error="invalid enum value";return false;}
                        if(method=="to_string"){out=nift::RuntimeValue(base.string.substr(prev+1,last-prev-1));return true;}
                        std::int64_t v=0;auto r=std::from_chars(base.string.data()+last+1,base.string.data()+base.string.size(),v);if(r.ec!=std::errc()){error="invalid enum backing value";return false;}out=nift::RuntimeValue((double)v);if(v>9007199254740992LL||v<-9007199254740992LL){out.type=nift::RuntimeType::StrNumber;out.string=std::to_string(v);}return true;
                    }
                    // CP171: all operations that construct object keys from values
                    // share one conversion rule. JSON objects ultimately have string
                    // keys, so Nift accepts only renderable scalar key values and
                    // checks uniqueness after canonical rendering.
                    auto generated_object_key=[&](const nift::RuntimeValue& key,std::string& rendered)->bool{
                        if(!(key.is_string()||key.is_number()||key.is_bool())){error=method+": generated key must be string, number, or bool";return false;}
                        rendered=key.is_string()?key.string:render_expression_value(key);
                        return true;
                    };
                    if(base.is_string() && base.string.rfind("\x1fnift:cmd:",0)==0) {
                        auto ci=command_instances_.find(base.string.substr(10));if(ci==command_instances_.end()){error="invalid command";return false;}auto c=ci->second;
                        auto& aa=args; auto& qq=aq;
                        auto sval=[&](size_t i,std::string&v){if(i>=aa.size())return false;nift::RuntimeValue d;if(i<qq.size()&&qq[i])d=nift::RuntimeValue(aa[i]);else if(!eval(aa[i],d,depth+1))return false;if(!d.is_string()){error=method+": expected string";return false;}v=d.string;return true;};
                        if(method=="pipe"){if(aa.size()!=1){error="pipe: expected command";return false;}nift::RuntimeValue d;if(!eval(aa[0],d,depth+1)||!d.is_string()||d.string.rfind("\x1fnift:cmd:",0)!=0){error="pipe: expected cmd(...)";return false;}auto oi=command_instances_.find(d.string.substr(10));if(oi==command_instances_.end()){error="pipe: invalid command";return false;}c->stages.insert(c->stages.end(),oi->second->stages.begin(),oi->second->stages.end());out=base;return true;}
                        if(method=="stdin"||method=="stdout"||method=="stderr"||method=="cwd"){std::string v;if((aa.size()<1||aa.size()>2)||!sval(0,v))return false;auto&st=(method=="stdin"?c->stages.front():c->stages.back());if(method=="stdin")st.stdin_path=v;else if(method=="stdout"){st.stdout_path=v;if(aa.size()==2){std::string m;if(!sval(1,m))return false;st.append_stdout=m=="append";}}else if(method=="stderr"){st.stderr_path=v;if(aa.size()==2){std::string m;if(!sval(1,m))return false;st.append_stderr=m=="append";}}else for(auto&x:c->stages)x.cwd=v;out=base;return true;}
                        if(method=="env"){if(aa.size()!=2){error="env: expected name and value";return false;}std::string k,v;if(!sval(0,k)||!sval(1,v))return false;for(auto&x:c->stages)x.env[k]=v;out=base;return true;}
                        if(method=="run"){if(!aa.empty()){error="run: command method takes no arguments";return false;}if(std::getenv("NIFT_NO_PROCESS")){error="run: external process execution disabled";return false;}auto pr=nift_run_pipeline(c->stages,true,false);out=nift::RuntimeValue::make_object();out["exit_code"]=nift::RuntimeValue((double)pr.exit_code);out["stdout"]=nift::RuntimeValue(pr.out);out["stderr"]=nift::RuntimeValue(pr.err);out["launched"]=nift::RuntimeValue(pr.launched);return true;}
                    }
                    if(base.is_string() && base.string.rfind("\x1fnift:file:",0)==0) {
                        auto fit=file_instances_.find(base.string.substr(11)); if(fit==file_instances_.end()){error="file: invalid FileValue";return false;} auto f=fit->second;
                        auto can_read=[&](){return f->mode=="r"||f->mode=="rw";}; auto can_write=[&](){return f->mode=="w"||f->mode=="a"||f->mode=="rw";};
                        auto need_open=[&](){if(!f->open){error=method+": file is not open";return false;}return true;};
                        auto need_read=[&](){if(!need_open())return false;if(!can_read()){error=method+": file is not open for reading";return false;}return true;};
                        auto need_write=[&](){if(!need_open())return false;if(!can_write()){error=method+": file is not open for writing";return false;}return true;};
                        auto sarg=[&](size_t i,std::string& x){nift::RuntimeValue v;if(!eval_arg(i,v)||!v.is_string()){error=method+": expected string argument";return false;}x=v.string;return true;};
                        auto dirty=[&](){f->dirty=f->working!=f->saved;};
                        if(method=="path"){if(!no_args())return false;out=nift::RuntimeValue(f->path.generic_string());return true;}
                        if(method=="exists"){if(!no_args())return false;std::error_code ec;out=nift::RuntimeValue(fs::exists(f->path,ec)&&!ec);return true;}
                        if(method=="open"){
                            if(f->open){error="open: file is already open";return false;}if(args.size()>1){error="open: expected zero or one mode";return false;}std::string m="r";if(args.size()==1&&!sarg(0,m))return false;
                            if(m!="r"&&m!="w"&&m!="a"&&m!="rw"){error="open: mode must be r, w, a or rw";return false;}std::error_code ec;bool ex=fs::exists(f->path,ec)&&!ec;
                            if((m=="r"||m=="rw")&&!ex){error="open: file does not exist";return false;}if(ex&&fs::is_directory(f->path,ec)){error="open: path is a directory";return false;}
                            std::string data;if(ex){std::ifstream in(f->path,std::ios::binary);if(!in){error="open: cannot read path";return false;}std::ostringstream ss;ss<<in.rdbuf();data=ss.str();}
                            f->mode=m;f->existed_at_open=ex;f->saved=data;f->working=m=="w"?std::string():data;f->cursor=m=="a"?f->working.size():0;f->dirty=(m=="w")||(m=="a"&&!ex);f->open=true;out=nift::RuntimeValue(nullptr);return true;
                        }
                        if(method=="close"){if(!no_args()||!need_open())return false;if(f->dirty){error="close: file has unsaved changes; save() or revert() first";return false;}f->open=false;f->mode.clear();f->working.clear();f->saved.clear();f->cursor=0;out=nift::RuntimeValue(nullptr);return true;}
                        if(method=="modified"){if(!no_args()||!need_open())return false;out=nift::RuntimeValue(f->dirty);return true;}
                        if(method=="tell"){if(!no_args()||!need_open())return false;out=nift::RuntimeValue((double)f->cursor);return true;}
                        if(method=="seek"){if(args.size()!=1||!need_open()){if(args.size()!=1&&error.empty())error="seek: expected byte position";return false;}nift::RuntimeValue n;std::size_t position=0;if(!eval_arg(0,n)||!nift::runtime_number_to_size(n,position)||position>f->working.size()){error="seek: byte position out of range";return false;}f->cursor=position;out=nift::RuntimeValue(nullptr);return true;}
                        if(method=="eof"){if(!no_args()||!need_read())return false;out=nift::RuntimeValue(f->cursor>=f->working.size());return true;}
                        if(method=="read"||method=="read_all"){if(!need_read())return false;size_t base=std::min(f->cursor,f->working.size());size_t n=f->working.size()-base;if(method=="read"&&!args.empty()){if(args.size()!=1){error="read: expected zero or one byte count";return false;}nift::RuntimeValue d;std::size_t count=0;if(!eval_arg(0,d)||!nift::runtime_number_to_size(d,count)){error="read: invalid byte count";return false;}n=std::min(n,count);}else if(method=="read_all"&&!args.empty()){error="read_all: expected no arguments";return false;}out=nift::RuntimeValue(f->working.substr(base,n));f->cursor=base+n;return true;}
                        if(method=="read_bytes"||method=="read_all_bytes"){if(!need_read())return false;size_t base=std::min(f->cursor,f->working.size());size_t n=f->working.size()-base;if(method=="read_bytes"){if(args.size()!=1){error="read_bytes: expected one byte count";return false;}nift::RuntimeValue d;std::size_t count=0;if(!eval_arg(0,d)||!nift::runtime_number_to_size(d,count)){error="read_bytes: invalid byte count";return false;}n=std::min(n,count);}else if(!args.empty()){error="read_all_bytes: expected no arguments";return false;}const auto begin=f->working.begin()+static_cast<std::ptrdiff_t>(base);out=nift::RuntimeValue(nift::RuntimeBytes(begin,begin+static_cast<std::ptrdiff_t>(n)));f->cursor=base+n;return true;}
                        if(method=="read_line"){if(!no_args()||!need_read())return false;if(f->cursor>=f->working.size()){out=nift::RuntimeValue(nullptr);return true;}auto e=f->working.find('\n',f->cursor);size_t z=e==std::string::npos?f->working.size():e;std::string line=f->working.substr(f->cursor,z-f->cursor);if(!line.empty()&&line.back()=='\r')line.pop_back();f->cursor=e==std::string::npos?f->working.size():e+1;out=nift::RuntimeValue(line);return true;}
                        if(method=="read_val"){if(!no_args()||!need_read())return false;while(f->cursor<f->working.size()&&std::isspace((unsigned char)f->working[f->cursor]))++f->cursor;if(f->cursor>=f->working.size()){out=nift::RuntimeValue(nullptr);return true;}size_t st=f->cursor;char first=f->working[f->cursor];if(first=='"'||first=='['||first=='{'){char op=first,cl=first=='['?']':first=='{'?'}':'"';int dep=0;bool qd=false,esc=false;while(f->cursor<f->working.size()){char c=f->working[f->cursor++];if(op=='"'){if(esc){esc=false;continue;}if(c=='\\'){esc=true;continue;}if(f->cursor>st+1&&c=='"')break;}else{if(qd){if(esc)esc=false;else if(c=='\\')esc=true;else if(c=='"')qd=false;}else if(c=='"')qd=true;else if(c==op)++dep;else if(c==cl&&--dep==0)break;}}}else while(f->cursor<f->working.size()&&!std::isspace((unsigned char)f->working[f->cursor]))++f->cursor;std::string tok=f->working.substr(st,f->cursor-st);nift::RuntimeValue v;if(!eval(tok,v,depth+1)){error="read_val: "+error;return false;}if(runtime_contains_bytes(v)){error="read_val: bytes values are not serializable";return false;}out=v;return true;}
                        if(method=="write_val"){if(args.size()!=1||!need_write()){if(args.size()!=1&&error.empty())error="write_val: expected one value";return false;}nift::RuntimeValue v;if(!eval_arg(0,v))return false;std::string d;if(!serialize_value(v,false,d,error))return false;d+="\n";size_t base=std::min(f->cursor,f->working.size());size_t ov=std::min(d.size(),f->working.size()-base);f->working.replace(base,ov,d);f->cursor=base+d.size();dirty();out=nift::RuntimeValue(nullptr);return true;}
                        if(method=="write"||method=="write_line"){if(args.size()!=1||!need_write()){if(args.size()!=1&&error.empty())error=method+": expected one value";return false;}nift::RuntimeValue v;if(!eval_arg(0,v))return false;std::string d;if(method=="write"&&v.is_bytes()){const nift::RuntimeBytes empty;const auto& raw=v.bytes?*v.bytes:empty;d.assign(raw.begin(),raw.end());}else{if(v.is_timer()||v.is_bytes()||v.is_array()||v.is_object()||(v.is_string()&&v.string.rfind("\x1fnift:",0)==0&&v.string.rfind("\x1fnift:timer:",0)!=0)){error=method+": value is not directly renderable";return false;}d=render_expression_value(v);if(method=="write_line")d+='\n';}size_t base=std::min(f->cursor,f->working.size());size_t ov=std::min(d.size(),f->working.size()-base);f->working.replace(base,ov,d);f->cursor=base+d.size();dirty();out=nift::RuntimeValue(nullptr);return true;}
                        if(method=="flush"){if(!no_args()||!need_write())return false;out=nift::RuntimeValue(nullptr);return true;}
                        if(method=="replace"||method=="replace_once"){if(args.size()!=2||!need_write()){if(args.size()!=2&&error.empty())error=method+": expected old and replacement strings";return false;}std::string a,b;if(!sarg(0,a)||!sarg(1,b))return false;if(a.empty()){error=method+": old string must not be empty";return false;}size_t count=0,pos=0;while((pos=f->working.find(a,pos))!=std::string::npos){++count;pos+=a.size();}if(method=="replace_once"&&count!=1){error="replace_once: expected exactly one match, found "+std::to_string(count);return false;}pos=0;while((pos=f->working.find(a,pos))!=std::string::npos){f->working.replace(pos,a.size(),b);pos+=b.size();if(method=="replace_once")break;}if(f->cursor>f->working.size())f->cursor=f->working.size();dirty();out=method=="replace"?nift::RuntimeValue((double)count):nift::RuntimeValue(nullptr);return true;}
                        if(method=="insert"||method=="insert"||method=="insert_before"||method=="insert_after"||method=="prepend"||method=="append"){if(!need_write())return false;size_t pos=0;std::string d;if(method=="prepend"||method=="append"){if(args.size()!=1||!sarg(0,d)){if(args.size()!=1&&error.empty())error=method+": expected text";return false;}pos=method=="prepend"?0:f->working.size();}else if(method=="insert"){if(args.size()!=2){error="insert: expected byte position and text";return false;}nift::RuntimeValue n;if(!eval_arg(0,n)||!nift::runtime_number_to_size(n,pos)||pos>f->working.size()||!sarg(1,d)){error="insert: invalid byte position or text";return false;}}else{if(args.size()!=2){error=method+": expected anchor and text";return false;}std::string a;if(!sarg(0,a)||!sarg(1,d))return false;if(a.empty()){error=method+": anchor must not be empty";return false;}auto at=f->working.find(a);if(at==std::string::npos||f->working.find(a,at+a.size())!=std::string::npos){error=method+": expected exactly one anchor";return false;}pos=at+(method=="insert_after"?a.size():0);}f->working.insert(pos,d);if(f->cursor>f->working.size())f->cursor=f->working.size();dirty();out=nift::RuntimeValue(nullptr);return true;}
                        if(method=="revert"){if(!no_args()||!need_write())return false;f->working=f->saved;f->cursor=0;f->dirty=false;out=nift::RuntimeValue(nullptr);return true;}
                        if(method=="save"){if(!no_args()||!need_write())return false;if(!f->dirty){out=nift::RuntimeValue(nullptr);return true;}std::error_code ec;auto parent=f->path.parent_path();if(!parent.empty()&&!fs::exists(parent,ec)){error="save: parent directory does not exist";return false;}fs::perms perms=fs::perms::unknown;if(fs::exists(f->path,ec)&&!ec)perms=fs::status(f->path,ec).permissions();fs::path tmp=f->path;tmp+=".nift-tmp-"+std::to_string((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count());{std::ofstream o(tmp,std::ios::binary|std::ios::trunc);if(!o){error="save: cannot create temporary file";return false;}o.write(f->working.data(),(std::streamsize)f->working.size());o.flush();if(!o){o.close();fs::remove(tmp,ec);error="save: temporary write failed";return false;}}if(perms!=fs::perms::unknown)fs::permissions(tmp,perms,ec);if(!filesystem::replace_file_atomic(tmp,f->path)){error="save: atomic replace failed";return false;}f->saved=f->working;f->dirty=false;f->existed_at_open=true;out=nift::RuntimeValue(nullptr);return true;}
                        if(method=="cat"){if(!no_args()||!need_read())return false;execution_output_->write_stdout(f->working);out=nift::RuntimeValue(nullptr);return true;}
                        if(method=="copy"||method=="move"||method=="remove"){if(f->open){error=method+": file must be closed";return false;}if((method=="remove"&&!args.empty())||(method!="remove"&&args.size()!=1)){error=method+": invalid arguments";return false;}std::error_code ec;if(method=="remove"){if(fs::is_directory(f->path,ec)){error="remove: directories are not removed recursively";return false;}if(!fs::remove(f->path,ec)&&ec){error="remove: "+ec.message();return false;}out=nift::RuntimeValue(nullptr);return true;}std::string raw;if(!sarg(0,raw))return false;fs::path dest(raw);if(dest.is_relative())dest=(standalone_script_host_?fs::current_path():host_.root())/dest;dest=fs::absolute(dest).lexically_normal();if(!standalone_script_host_&&!host_.root().empty()&&!filesystem::path_within(fs::absolute(host_.root()).lexically_normal(),dest)){error=method+": path must stay inside the Nift project";return false;}if(method=="move")fs::rename(f->path,dest,ec);else fs::copy_file(f->path,dest,fs::copy_options::overwrite_existing,ec);if(ec){error=method+": "+ec.message();return false;}if(method=="move")f->path=dest;auto nf=std::make_shared<FileInstance>();nf->path=dest;auto id=std::to_string(next_file_instance_id_++);file_instances_[id]=nf;out=nift::RuntimeValue(std::string("\x1fnift:file:")+id);return true;}
                    }
                    if(base.is_string() && base.string.rfind("\x1fnift:",0)!=0) {
                        const std::string& str=base.string;
                        if(method=="encode"){if(args.size()!=1){error="encode: expected encoding";return false;}nift::RuntimeValue encoding;if(!eval_arg(0,encoding)||!encoding.is_string()||encoding.string!="utf-8"){error="encode: encoding must be exactly 'utf-8'";return false;}if(!nift::runtime_valid_utf8(str)){error="encode: invalid UTF-8 text";return false;}out=nift::RuntimeValue(nift::RuntimeBytes(str.begin(),str.end()));return true;}
                        auto utf8_parts=[&](std::vector<std::string>& parts)->bool{
                            for(std::size_t i=0;i<str.size();){unsigned char c=(unsigned char)str[i];std::size_t n=c<0x80?1:(c>=0xC2&&c<=0xDF?2:(c>=0xE0&&c<=0xEF?3:(c>=0xF0&&c<=0xF4?4:0)));if(!n||i+n>str.size()){error="split: invalid UTF-8";return false;}for(std::size_t j=1;j<n;++j)if(((unsigned char)str[i+j]&0xC0)!=0x80){error="split: invalid UTF-8";return false;}parts.push_back(str.substr(i,n));i+=n;}return true;};
                        auto utf8_length=[&](std::size_t& count)->bool{std::vector<std::string> p;if(!utf8_parts(p))return false;count=p.size();return true;};
                        if(method=="length"){if(!no_args())return false;std::size_t n=0;if(!utf8_length(n))return false;out=nift::RuntimeValue((double)n);return true;}
                        if(method=="empty"){if(!no_args())return false;out=nift::RuntimeValue(str.empty());return true;}
                        if(method=="split"){
                            if(args.size()!=1){error="split: expected one delimiter";return false;}nift::RuntimeValue d;if(!eval_arg(0,d)||!d.is_string()){error="split: delimiter must be a string";return false;}out=nift::RuntimeValue::make_array();
                            if(d.string.empty()){std::vector<std::string> parts;if(!utf8_parts(parts))return false;for(auto& x:parts)out.array.emplace_back(x);return true;}
                            std::size_t pos=0;while(true){auto at=str.find(d.string,pos);if(at==std::string::npos){out.array.emplace_back(str.substr(pos));break;}out.array.emplace_back(str.substr(pos,at-pos));pos=at+d.string.size();}return true;
                        }
                        if(method=="index_of"||method=="last_index_of"||method=="contains"||method=="starts_with"||method=="ends_with"){
                            if(args.size()!=1){error=method+": expected one string";return false;}nift::RuntimeValue n;if(!eval_arg(0,n)||!n.is_string()){error=method+": argument must be a string";return false;}
                            if(method=="contains"){out=nift::RuntimeValue(str.find(n.string)!=std::string::npos);return true;}
                            if(method=="starts_with"){out=nift::RuntimeValue(str.rfind(n.string,0)==0);return true;}
                            if(method=="ends_with"){out=nift::RuntimeValue(n.string.size()<=str.size()&&str.compare(str.size()-n.string.size(),n.string.size(),n.string)==0);return true;}
                            auto at=method=="index_of"?str.find(n.string):str.rfind(n.string);out=nift::RuntimeValue(at==std::string::npos?-1.0:(double)at);return true;
                        }
                        if(method=="trim"||method=="trim_start"||method=="trim_end"){
                            if(!no_args())return false;
                            std::size_t a=0,b=str.size();if(method!="trim_end")while(a<b&&std::isspace((unsigned char)str[a]))++a;if(method!="trim_start")while(b>a&&std::isspace((unsigned char)str[b-1]))--b;out=nift::RuntimeValue(str.substr(a,b-a));return true;
                        }
                        if(method=="to_lower"||method=="to_upper"){
                            if(!no_args())return false;
                            std::string r=str;for(char& c:r)c=(char)(method=="to_lower"?std::tolower((unsigned char)c):std::toupper((unsigned char)c));out=nift::RuntimeValue(r);return true;
                        }
                        if(method=="replace"){
                            if(args.size()!=2){error="replace: expected old and replacement strings";return false;}nift::RuntimeValue a,b;if(!eval_arg(0,a)||!eval_arg(1,b)||!a.is_string()||!b.is_string()){error="replace: arguments must be strings";return false;}if(a.string.empty()){error="replace: old string must not be empty";return false;}std::string r=str;std::size_t pos=0;while((pos=r.find(a.string,pos))!=std::string::npos){r.replace(pos,a.string.size(),b.string);pos+=b.string.size();}out=nift::RuntimeValue(r);return true;
                        }
                        if(method=="to_int"){
                            if(!no_args())return false;
                            if(str.empty()){error="to_int: invalid integer";return false;}const char* data=str.data();std::size_t size=str.size();if(str[0]=='+'){data+=1;size-=1;}if(size==0){error="to_int: invalid integer";return false;}std::int64_t v=0;auto pr=std::from_chars(data,data+size,v);if(pr.ec!=std::errc()||pr.ptr!=data+size){error="to_int: invalid or out-of-range integer";return false;}out=nift::RuntimeValue((double)v);if(v>9007199254740992LL||v<-9007199254740992LL){out.type=nift::RuntimeType::StrNumber;out.string=std::to_string(v);}return true;
                        }
                        if(method=="to_string"){if(!no_args())return false;out=nift::RuntimeValue(str);return true;}
                        if(method=="to_double"){
                            if(!no_args())return false;
                            if(str.empty()||std::isspace((unsigned char)str.front())||std::isspace((unsigned char)str.back())){error="to_double: invalid number";return false;}if(str.find("0x")!=std::string::npos||str.find("0X")!=std::string::npos){error="to_double: invalid number";return false;}char* end=nullptr;errno=0;double v=std::strtod(str.c_str(),&end);if(errno==ERANGE||!end||end!=str.c_str()+str.size()||!std::isfinite(v)){error="to_double: invalid or out-of-range number";return false;}out=nift::RuntimeValue(v);return true;
                        }
                        if(method=="substr"){
                            if(args.empty()||args.size()>2){error="substr: expected pos and optional length";return false;}nift::RuntimeValue p,n;std::size_t at=0;if(!eval_arg(0,p)||!nift::runtime_number_to_size(p,at)){error="substr: invalid pos";return false;}if(at>str.size())at=str.size();std::size_t len=std::string::npos;if(args.size()==2){if(!eval_arg(1,n)||!nift::runtime_number_to_size(n,len)){error="substr: invalid length";return false;}}out=nift::RuntimeValue(str.substr(at,len));return true;
                        }
                    }
                    if(method=="to_string" && base.is_number()) { if(!no_args())return false;out=nift::RuntimeValue(render_expression_value(base));return true; }
                    if(method=="to_string" && base.is_bool()) { if(!no_args())return false;out=nift::RuntimeValue(base.boolean?"true":"false");return true; }
                    if(method=="to_string" && base.is_null()) { if(!no_args())return false;out=nift::RuntimeValue("null");return true; }
                    if(base.is_number()) {
                        if(method=="to_int"){if(!no_args())return false;std::int64_t v=0;if(!nift::runtime_number_to_i64(base,v)){error="to_int: invalid or out-of-range integer";return false;}out=nift::runtime_integer(v);return true;}
                        if(method=="abs"){if(!no_args())return false;out=nift::RuntimeValue(std::fabs(base.num));return true;}
                        if(method=="floor"){if(!no_args())return false;out=nift::RuntimeValue(std::floor(base.num));return true;}
                        if(method=="ceil"){if(!no_args())return false;out=nift::RuntimeValue(std::ceil(base.num));return true;}
                        if(method=="round"){if(!no_args())return false;out=nift::RuntimeValue(std::round(base.num));return true;}
                    }
                    if(base.is_object()) {
                        if(method=="size"||method=="empty"||method=="keys"||method=="values"||method=="entries") {
                            if(!no_args())return false;
                            if(method=="size"){out=nift::RuntimeValue((double)base.object.size());return true;}
                            if(method=="empty"){out=nift::RuntimeValue(base.object.empty());return true;}
                            out=nift::RuntimeValue::make_array();
                            for(const auto& kv:base.object){
                                if(method=="keys")out.array.emplace_back(kv.first);
                                else if(method=="values")out.array.push_back(kv.second);
                                else { nift::RuntimeValue e=nift::RuntimeValue::make_object(); e["key"]=nift::RuntimeValue(kv.first); e["value"]=kv.second; out.array.push_back(std::move(e)); }
                            }
                            return true;
                        }
                        if(method=="pick"||method=="omit") {
                            if(args.size()!=1){error=method+": expected one array of keys";return false;}
                            nift::RuntimeValue keys;if(!eval_arg(0,keys)||!keys.is_array()){error=method+": keys must be an array";return false;}
                            std::vector<std::string> wanted;
                            for(const auto& k:keys.array){if(!k.is_string()){error=method+": keys must be strings";return false;}if(std::find(wanted.begin(),wanted.end(),k.string)==wanted.end())wanted.push_back(k.string);}
                            nift::RuntimeValue result=nift::RuntimeValue::make_object();
                            auto wanted_key=[&](const std::string& key){for(const auto& pat:wanted){if(glob_has_magic(pat)?glob_component_match(pat,key):pat==key)return true;}return false;};
                            if(method=="pick"){std::vector<std::string> picked;for(const auto& pat:wanted){auto pat_match=[&](const std::string& key){return glob_has_magic(pat)?glob_component_match(pat,key):pat==key;};for(const auto& kv:base.object){if(std::find(picked.begin(),picked.end(),kv.first)==picked.end()&&pat_match(kv.first)){result[kv.first]=kv.second;picked.push_back(kv.first);}}}}
                            else {for(const auto& kv:base.object)if(!wanted_key(kv.first))result[kv.first]=kv.second;}
                            out=std::move(result);return true;
                        }
                        if(method=="has"||method=="get") {
                            if((method=="has"&&args.size()!=1)||(method=="get"&&(args.empty()||args.size()>2))){error=method+": expected key"+(method=="get"?" and optional default":"");return false;}
                            nift::RuntimeValue k;if(!eval_arg(0,k)||!k.is_string()){error=method+": key must be a string";return false;}
                            auto it=std::find_if(base.object.begin(),base.object.end(),[&](const auto& kv){return kv.first==k.string;});
                            if(method=="has"){out=nift::RuntimeValue(it!=base.object.end());return true;}
                            if(it!=base.object.end()){out=it->second;return true;}
                            if(args.size()==2)return eval_arg(1,out);
                            out=nift::RuntimeValue(nullptr);return true;
                        }
                        if(method=="merge_deep") {
                            if(args.size()!=1){error="merge_deep: expected one object";return false;}
                            nift::RuntimeValue rhs;if(!eval_arg(0,rhs)||!rhs.is_object()){error="merge_deep: argument must be an object";return false;}
                            std::function<bool(const nift::RuntimeValue&,const nift::RuntimeValue&,nift::RuntimeValue&,int)> merge_rec;
                            merge_rec=[&](const nift::RuntimeValue& lhs,const nift::RuntimeValue& r,nift::RuntimeValue& result,int level)->bool{
                                if(level>64){error="merge_deep: maximum merge depth exceeded";return false;}
                                if(!(lhs.is_object()&&r.is_object())){result=r;return true;}
                                result=lhs;
                                for(const auto& kv:r.object){
                                    auto it=std::find_if(result.object.begin(),result.object.end(),[&](const auto& x){return x.first==kv.first;});
                                    if(it!=result.object.end()&&it->second.is_object()&&kv.second.is_object()){nift::RuntimeValue merged;if(!merge_rec(it->second,kv.second,merged,level+1))return false;it->second=std::move(merged);}
                                    else result[kv.first]=kv.second;
                                }
                                return true;
                            };
                            nift::RuntimeValue result;if(!merge_rec(base,rhs,result,0))return false;out=std::move(result);return true;
                        }
                        if(method=="merge") {
                            if(args.size()!=1){error="merge: expected one object";return false;}nift::RuntimeValue rhs;if(!eval_arg(0,rhs)||!rhs.is_object()){error="merge: argument must be an object";return false;}
                            out=base;for(const auto& kv:rhs.object)out[kv.first]=kv.second;return true;
                        }
                    }
                    if(base.is_array()) {
                        const auto& a=base.array;
                        if(method=="from_entries"){
                            if(!no_args())return false;
                            nift::RuntimeValue result=nift::RuntimeValue::make_object();
                            // O(n) construction: order-preserving vector plus a hash map
                            // for duplicate detection instead of linear object scans.
                            std::unordered_map<std::string,std::size_t> seen_keys; result.object.reserve(a.size());
                            for(const auto& entry:a){
                                if(!entry.is_object()||entry.object.size()!=2||!entry.has("key")||!entry.has("value")){error="from_entries: each entry must be an object containing exactly key and value";return false;}
                                const nift::RuntimeValue *key=nullptr,*value=nullptr;
                                for(const auto& kv:entry.object){if(kv.first=="key")key=&kv.second;else if(kv.first=="value")value=&kv.second;}
                                std::string rendered;if(!key||!value||!generated_object_key(*key,rendered))return false;
                                if(seen_keys.count(rendered)){error="from_entries: duplicate generated key '"+rendered+"'";return false;}
                                seen_keys[rendered]=result.object.size(); result.object.emplace_back(rendered,*value);
                            }
                            out=std::move(result);return true;
                        }
                        auto invoke_value_cb=[&](const nift::RuntimeValue& cb,const std::vector<nift::RuntimeValue>& av,nift::RuntimeValue& result)->bool{
                            if(!cb.is_string()||cb.string.rfind("\x1fnift:callable:",0)!=0){error="transformation: callback must be callable";return false;}
                            const std::string tag=cb.string;
                            if(tag.rfind("\x1fnift:callable:lambda:",0)==0){
                                auto li=lambda_instances_.find(tag.substr(22));if(li==lambda_instances_.end()){error="invalid lambda";return false;}auto fn=li->second;
                                if(callable_call_depth_>=kMaxCallableDepth){error="callable recursion depth exceeded";return false;}++callable_call_depth_;
                                if(fn->variadic_param.empty()?av.size()!=fn->params.size():av.size()<fn->params.size()){--callable_call_depth_;error="callback argument count mismatch";return false;}
                                auto lexical_env=enter_lexical_environment(fn->module_env);
                                push_variable_scope();auto& sc=variable_scopes_.back();for(const auto& kv:fn->captures)sc[kv.first]=kv.second;
                                for(std::size_t ai=0;ai<fn->params.size();++ai){auto sp=std::make_shared<nift::RuntimeValue>(av[ai]);sc[fn->params[ai]]=VariableBinding{sp,nift_binding_type(*sp),true,false};}
                                if(!fn->variadic_param.empty()){nift::RuntimeValue rest=nift::RuntimeValue::make_array();for(std::size_t ai=fn->params.size();ai<av.size();++ai)rest.array.push_back(av[ai]);auto sp=std::make_shared<nift::RuntimeValue>(std::move(rest));sc[fn->variadic_param]=VariableBinding{sp,nift_binding_type(*sp),true,false};}
                                bool okcall=true;
                                if(fn->block){std::string bt=trim_copy(fn->body);const bool rendered=!bt.empty()&&bt.front()=='<';if(rendered){auto nested=parse(fn->body,fn->source_path,1);if(!nested.ok){error=nested.error.message;okcall=false;}else result=nift::RuntimeValue(nested.output);}else{okcall=eval(fn->body,result,depth+1);if(!okcall&&error.rfind("callable recursion depth exceeded",0)!=0)error="lambda body error: "+error;}}
                                else{okcall=eval(fn->body,result,depth+1);if(!okcall&&error.rfind("callable recursion depth exceeded",0)!=0)error="lambda body error: "+error;}
                                pop_variable_scope();leave_lexical_environment(std::move(lexical_env));--callable_call_depth_;return okcall;
                            }
                            if(tag.rfind("\x1fnift:callable:named:",0)==0){
                                const Callable* resolved=nullptr;std::shared_ptr<ModuleEnv> owner;if(!resolve_named_callable(tag,resolved,owner)){error="invalid callable";return false;}const Callable& callee=*resolved;
                                if(callable_call_depth_>=kMaxCallableDepth){error="callable recursion depth exceeded";return false;}++callable_call_depth_;
                                if(callee.variadic_param.empty()?av.size()!=callee.params.size():av.size()<callee.params.size()){--callable_call_depth_;error="callback argument count mismatch";return false;}
                                auto lexical_env=enter_lexical_environment(owner ? owner : callee.module_env);
                                push_variable_scope();auto& scope=variable_scopes_.back();
                                for(std::size_t ai=0;ai<callee.params.size();++ai){auto sp=std::make_shared<nift::RuntimeValue>(av[ai]);scope.emplace(callee.params[ai],VariableBinding{sp,nift_binding_type(*sp),true,false});}
                                if(!callee.variadic_param.empty()){nift::RuntimeValue rest=nift::RuntimeValue::make_array();for(std::size_t ai=callee.params.size();ai<av.size();++ai)rest.array.push_back(av[ai]);auto sp=std::make_shared<nift::RuntimeValue>(std::move(rest));scope.emplace(callee.variadic_param,VariableBinding{sp,nift_binding_type(*sp),true,false});}
                                const int caller_loop_depth=loop_depth_;loop_depth_=0;pending_control_={};
                                bool call_ok=true;std::string call_error;
                                if(callee.fragment){const bool saved_fragment=in_fragment_body_;in_fragment_body_=true;auto nested=parse(callee.body,callee.source_path,1);in_fragment_body_=saved_fragment;if(!nested.ok){call_ok=false;call_error=nested.error.message;}else result=nift::RuntimeValue(nested.output);}
                                else{++function_call_depth_;auto body_result=execute_native_program(callee.body,callee.source_path,1);--function_call_depth_;if(!body_result.ok){call_ok=false;call_error=body_result.error.message;}else if(pending_control_.kind==ControlFlow::None)result=nift::RuntimeValue(nullptr);else consume_return(result);}
                                loop_depth_=caller_loop_depth;pop_variable_scope();leave_lexical_environment(std::move(lexical_env));--callable_call_depth_;
                                if(!call_ok){error=call_error;return false;}return true;
                            }
                            error="callback is not a callable";return false;};
                        auto invoke_value_callback=[&](const std::string& cbexpr,const std::vector<nift::RuntimeValue>& av,nift::RuntimeValue& result)->bool{
                            nift::RuntimeValue cb;if(!eval(cbexpr,cb,depth+1))return false;if(!cb.is_string()||cb.string.rfind("\x1fnift:callable:",0)!=0){error="transformation: callback must be callable";return false;}
                            return invoke_value_cb(cb,av,result);};
                        if(method=="index_by"){
                            if(args.size()!=1){error="index_by: expected one selector";return false;}
                            nift::RuntimeValue result=nift::RuntimeValue::make_object();
                            std::unordered_map<std::string,std::size_t> seen_keys; result.object.reserve(a.size());
                            for(const auto& v:a){
                                nift::RuntimeValue key;if(!invoke_value_callback(args[0],{v},key))return false;
                                std::string rendered;if(!generated_object_key(key,rendered))return false;
                                if(seen_keys.count(rendered)){error="index_by: duplicate generated key '"+rendered+"'";return false;}
                                seen_keys[rendered]=result.object.size(); result.object.emplace_back(rendered,v);
                            }
                            out=std::move(result);return true;
                        }
                        if(method=="partition"){
                            if(args.size()!=1){error="partition: expected one predicate";return false;}
                            nift::RuntimeValue result=nift::RuntimeValue::make_object(); result["matched"]=nift::RuntimeValue::make_array(); result["unmatched"]=nift::RuntimeValue::make_array();
                            for(const auto& v:a){nift::RuntimeValue r;if(!invoke_value_callback(args[0],{v},r))return false;if(!r.is_bool()){error="partition: callback must return bool";return false;}(r.boolean?result["matched"]:result["unmatched"]).push_back(v);}out=std::move(result);return true;
                        }
                        if(method=="unique_by"){
                            if(args.size()!=1){error="unique_by: expected one selector";return false;} out=nift::RuntimeValue::make_array(); std::unordered_map<std::string,std::vector<nift::RuntimeValue>> seen;
                            // Canonical structural form exactly mirroring structural_equal,
                            // so O(n) dedup preserves the same semantics as the previous
                            // linear structural scan (numbers by semantic JSON equality,
                            // object keys order-insensitive, arrays order-sensitive).
                            std::function<void(const nift::RuntimeValue&,std::string&)> canonical_key;
                            canonical_key=[&](const nift::RuntimeValue& v,std::string& out){
                                if(v.is_null()){out+="z";return;}
                                if(v.is_bool()){out+=v.boolean?"bt":"bf";return;}
                                if(v.is_number()){out+="n";out+=nift::runtime_numeric_fingerprint(v);return;}
                                if(v.is_string()){if(v.string.rfind("\x1fnift:",0)==0){out+="i";out+=v.string;return;}out+="s";out+=std::to_string(v.string.size());out+=':';out+=v.string;return;}
                                if(v.is_array()){out+="a(";for(const auto& e:v.array){canonical_key(e,out);out+=',';}out+=')';return;}
                                if(v.is_object()){
                                    out+="o(";std::vector<std::pair<std::string,const nift::RuntimeValue*>> pairs;
                                    for(const auto& kv:v.object)pairs.push_back({kv.first,&kv.second});
                                    std::sort(pairs.begin(),pairs.end(),[&](const auto& x,const auto& y){return x.first<y.first;});
                                    for(const auto& p:pairs){out+=std::to_string(p.first.size());out+=':';out+=p.first;out+='=';canonical_key(*p.second,out);out+=',';}
                                    out+=')';return;
                                }
                                out+='?';
                            };
                            for(const auto& v:a){nift::RuntimeValue k;if(!invoke_value_callback(args[0],{v},k))return false;std::string ck;canonical_key(k,ck);auto& bucket=seen[ck];bool duplicate=false;for(const auto& prior:bucket)if(structural_equal(prior,k)){duplicate=true;break;}if(!duplicate){bucket.push_back(std::move(k));out.array.push_back(v);}}return true;
                        }
                        if(method=="min_by"||method=="max_by"){
                            if(args.size()!=1){error=method+": expected one selector";return false;}if(a.empty()){error=method+": cannot select from an empty array";return false;}nift::RuntimeValue best_key;if(!invoke_value_callback(args[0],{a.front()},best_key))return false;if(!(best_key.is_number()||best_key.is_string()||best_key.is_bool())){error=method+": key must be scalar";return false;}out=a.front();
                            for(size_t i=1;i<a.size();++i){nift::RuntimeValue k;if(!invoke_value_callback(args[0],{a[i]},k))return false;if(k.type!=best_key.type && !(k.is_number()&&best_key.is_number())){error=method+": keys must be comparable and homogeneous";return false;}bool take=false;if(k.is_number()){const int cmp=nift::runtime_compare_numbers(k,best_key);take=method=="min_by"?cmp<0:cmp>0;}else if(k.is_string())take=method=="min_by"?k.string<best_key.string:k.string>best_key.string;else if(k.is_bool())take=method=="min_by"?k.boolean<best_key.boolean:k.boolean>best_key.boolean;else{error=method+": key must be scalar";return false;}if(take){best_key=k;out=a[i];}}return true;
                        }
                        if(method=="count_by"){
                            if(args.size()!=1){error="count_by: expected one selector";return false;}nift::RuntimeValue result=nift::RuntimeValue::make_object();
                            std::unordered_map<std::string,std::size_t> pos; result.object.reserve(a.size());
                            for(const auto& v:a){nift::RuntimeValue k;if(!invoke_value_callback(args[0],{v},k))return false;std::string rendered;if(!generated_object_key(k,rendered))return false;auto it=pos.find(rendered);if(it==pos.end()){pos[rendered]=result.object.size();result.object.emplace_back(rendered,nift::RuntimeValue(0.0));result.object.back().second.num+=1;}else result.object[it->second].second.num+=1;}
                            out=std::move(result);return true;
                        }
                        if(method=="take"||method=="drop"||method=="chunk"){
                            if(args.size()!=1){error=method+": expected one count";return false;}nift::RuntimeValue n;size_t z=0;if(!eval_arg(0,n)||!nift::runtime_number_to_size(n,z)||(method=="chunk"&&z==0)){error=method+": count must be "+std::string(method=="chunk"?"a positive":"a non-negative")+" integer";return false;}out=nift::RuntimeValue::make_array();if(method=="take"){size_t e=std::min(z,a.size());out.array.assign(a.begin(),a.begin()+e);}else if(method=="drop"){size_t b=std::min(z,a.size());out.array.assign(a.begin()+b,a.end());}else{for(size_t b=0;b<a.size();b+=z){nift::RuntimeValue c=nift::RuntimeValue::make_array();size_t e=std::min(b+z,a.size());c.array.assign(a.begin()+b,a.begin()+e);out.array.push_back(std::move(c));}}return true;
                        }
                        if(method=="group_by_each"){
                            if(args.size()!=1){error="group_by_each: expected one selector";return false;}nift::RuntimeValue result=nift::RuntimeValue::make_object();
                            std::unordered_map<std::string,std::size_t> pos;
                            for(const auto& v:a){nift::RuntimeValue ks;if(!invoke_value_callback(args[0],{v},ks))return false;if(!ks.is_array()){error="group_by_each: selector must return an array";return false;}std::vector<std::string> item_keys;for(const auto& k:ks.array){std::string rendered;if(!generated_object_key(k,rendered))return false;if(std::find(item_keys.begin(),item_keys.end(),rendered)!=item_keys.end())continue;item_keys.push_back(rendered);auto it=pos.find(rendered);if(it==pos.end()){pos[rendered]=result.object.size();result.object.emplace_back(rendered,nift::RuntimeValue::make_array());result.object.back().second.array.push_back(v);}else result.object[it->second].second.array.push_back(v);}}
                            out=std::move(result);return true;
                        }
                        if(method=="sort_by"){
                            if(args.empty() || (args.size()!=1 && args.size()%2!=0)){error="sort_by: expected selector or selector/direction pairs";return false;}
                            struct SortSpec { std::string selector; bool desc=false; }; std::vector<SortSpec> specs;
                            if(args.size()==1) specs.push_back({args[0],false}); else {
                                for(size_t si=0;si<args.size();si+=2){if(si+1>=args.size()||si+1>=aq.size()||!aq[si+1]){error="sort_by: direction must be quoted 'asc' or 'desc'";return false;}std::string d=args[si+1];if(d!="asc"&&d!="desc"){error="sort_by: direction must be 'asc' or 'desc'";return false;}specs.push_back({args[si],d=="desc"});}
                            }
                            struct Decorated { std::vector<nift::RuntimeValue> keys; nift::RuntimeValue value; }; std::vector<Decorated> decorated; decorated.reserve(a.size());
                            for(const auto& v:a){Decorated d;d.value=v;for(const auto& sp:specs){nift::RuntimeValue k;if(!invoke_value_callback(sp.selector,{v},k))return false;if(!(k.is_string()||k.is_number()||k.is_bool())){error="sort_by: key must be scalar";return false;}d.keys.push_back(std::move(k));}decorated.push_back(std::move(d));}
                            auto cmp_key=[&](const nift::RuntimeValue& x,const nift::RuntimeValue& y,int& c)->bool{if(x.is_number()&&y.is_number())c=nift::runtime_compare_numbers(x,y);else if(x.is_string()&&y.is_string())c=x.string<y.string?-1:x.string>y.string?1:0;else if(x.is_bool()&&y.is_bool())c=x.boolean<y.boolean?-1:x.boolean>y.boolean?1:0;else{error="sort_by: keys must be comparable and homogeneous";return false;}return true;};
                            bool compare_error=false;std::stable_sort(decorated.begin(),decorated.end(),[&](const Decorated& x,const Decorated& y){for(size_t si=0;si<specs.size();++si){int c=0;if(!cmp_key(x.keys[si],y.keys[si],c)){compare_error=true;return false;}if(c)return specs[si].desc?c>0:c<0;}return false;});if(compare_error)return false;out=nift::RuntimeValue::make_array();for(auto& d:decorated)out.array.push_back(std::move(d.value));return true;
                        }
                        if(method=="map"||method=="filter"||method=="reduce"||method=="any"||method=="all"||method=="find"||method=="find_index"||method=="count"||method=="group_by"){
                            if((method=="reduce"&&args.size()!=2)||(method!="reduce"&&args.size()!=1)){error=method+": invalid arguments";return false;}nift::RuntimeValue arr=nift::RuntimeValue::make_array(),acc;if(method=="reduce"&&!eval_arg(1,acc))return false;double cnt=0;std::vector<std::pair<nift::RuntimeValue,nift::RuntimeValue>> keyed;
                            nift::RuntimeValue cbv;if(!eval(args[0],cbv,depth+1))return false;if(!cbv.is_string()||cbv.string.rfind("\x1fnift:callable:",0)!=0){error="transformation: callback must be callable";return false;}
                            for(size_t vi=0;vi<a.size();++vi){const auto&v=a[vi];nift::RuntimeValue r;if(!invoke_value_cb(cbv,method=="reduce"?std::vector<nift::RuntimeValue>{acc,v}:std::vector<nift::RuntimeValue>{v},r))return false;if(method=="map")arr.array.push_back(r);else if(method=="filter"){if(!r.is_bool()){error="filter: callback must return bool";return false;}if(r.boolean)arr.array.push_back(v);}else if(method=="reduce")acc=r;else if(method=="group_by"){if(!(r.is_string()||r.is_number()||r.is_bool())){error=method+": key must be scalar";return false;}keyed.push_back({r,v});}else{if(!r.is_bool()){error=method+": callback must return bool";return false;}if(method=="any"&&r.boolean){out=nift::RuntimeValue(true);return true;}if(method=="all"&&!r.boolean){out=nift::RuntimeValue(false);return true;}if(method=="find"&&r.boolean){out=v;return true;}if(method=="find_index"&&r.boolean){out=nift::RuntimeValue((double)vi);return true;}if(method=="count"&&r.boolean)cnt+=1;}}
                            if(method=="group_by"){out=nift::RuntimeValue::make_object();for(auto&kv:keyed){if(kv.first.is_timer()){error="group_by: timer values cannot be object keys";return false;}std::string k=kv.first.is_string()?kv.first.string:render_expression_value(kv.first);if(!out.has(k))out[k]=nift::RuntimeValue::make_array();out[k].push_back(kv.second);}return true;}
                            if(method=="map"||method=="filter")out=arr;else if(method=="reduce")out=acc;else if(method=="any")out=nift::RuntimeValue(false);else if(method=="all")out=nift::RuntimeValue(true);else if(method=="find"||method=="find_index")out=nift::RuntimeValue(method=="find"?nift::RuntimeValue(nullptr):nift::RuntimeValue(-1.0));else out=nift::RuntimeValue(cnt);return true;}
                        if(method=="unique"||method=="flatten"||method=="sum"||method=="min"||method=="max"){if(!args.empty()){error=method+": expected no arguments";return false;}if(method=="unique"){out=nift::RuntimeValue::make_array();for(const auto&v:a){bool seen=false;for(const auto&w:out.array)if(structural_equal(v,w)){seen=true;break;}if(!seen)out.array.push_back(v);}return true;}if(method=="flatten"){out=nift::RuntimeValue::make_array();for(const auto&v:a){if(v.is_array())out.array.insert(out.array.end(),v.array.begin(),v.array.end());else out.array.push_back(v);}return true;}if(a.empty()){if(method=="sum"){out=nift::RuntimeValue(0.0);return true;}error=method+": cannot aggregate an empty array";return false;}if(method=="sum"){double total=0;for(const auto&v:a){if(!v.is_number()){error="sum: values must be numeric";return false;}total+=v.num;}out=nift::RuntimeValue(total);return true;}out=a.front();for(size_t ai=1;ai<a.size();++ai){const auto&v=a[ai];bool take=false;if(out.is_number()&&v.is_number()){const int cmp=nift::runtime_compare_numbers(v,out);take=method=="min"?cmp<0:cmp>0;}else if(out.is_string()&&v.is_string())take=method=="min"?v.string<out.string:v.string>out.string;else{error=method+": values must be comparable and homogeneous";return false;}if(take)out=v;}return true;}
                        if(method=="size"||method=="length"){if(!no_args())return false;out=nift::RuntimeValue((double)a.size());return true;}
                        if(method=="empty"){if(!no_args())return false;out=nift::RuntimeValue(a.empty());return true;}
                        if(method=="first"||method=="last"){if(!no_args())return false;if(a.empty()){error=method+": array is empty";return false;}out=method=="first"?a.front():a.back();return true;}
                        if(method=="indexOf"){if(args.size()!=1){error="indexOf: expected one value";return false;}nift::RuntimeValue v;if(!eval_arg(0,v))return false;std::size_t i=0;for(;i<a.size();++i)if(structural_equal(a[i],v))break;out=nift::RuntimeValue(i<a.size()?static_cast<double>(i):-1.0);return true;}
                        if(method=="contains"){if(args.size()!=1){error="contains: expected one value";return false;}nift::RuntimeValue v;if(!eval_arg(0,v))return false;for(const auto& x:a)if(structural_equal(x,v)){out=nift::RuntimeValue(true);return true;}out=nift::RuntimeValue(false);return true;}
                        if(method=="join"){if(args.size()!=1){error="join: expected separator";return false;}nift::RuntimeValue sep;if(!eval_arg(0,sep)||!sep.is_string()){error="join: separator must be a string";return false;}std::string r;for(std::size_t i=0;i<a.size();++i){if(i)r+=sep.string;if(a[i].is_timer()||a[i].is_bytes()||a[i].is_array()||a[i].is_object()||(a[i].is_string()&&a[i].string.rfind("\x1fnift:",0)==0&&a[i].string.rfind("\x1fnift:timer:",0)!=0)){error="join: elements must be renderable scalar values";return false;}r+=render_expression_value(a[i]);}out=nift::RuntimeValue(r);return true;}
                        if(method=="slice"){if(args.empty()||args.size()>2){error="slice: expected start and optional end";return false;}nift::RuntimeValue st,en;std::size_t b=0,e=a.size();if(!eval_arg(0,st)||!nift::runtime_number_to_size(st,b)){error="slice: invalid start";return false;}if(args.size()==2){if(!eval_arg(1,en)||!nift::runtime_number_to_size(en,e)){error="slice: invalid end";return false;}}b=std::min(b,a.size());e=std::min(e,a.size());if(e<b)e=b;out=nift::RuntimeValue::make_array();out.array.assign(a.begin()+b,a.begin()+e);return true;}
                    }
                    if(method=="to_string" && (base.is_array()||base.is_object())){error="to_string: use stringify() for composite values";return false;}
                    if(method=="to_string" && !(base.is_string() && base.string.rfind("\x1fnift:",0)==0)){error="to_string: expected int, double, bool, null or string";return false;}
                    if((base.is_string() && base.string.rfind("\x1fnift:",0)!=0)||base.is_number()||(base.is_array()&&method!="insert"&&method!="remove")){error=method+": unsupported for this value";return false;}
                }
            }
        }

        // v4.3 collection constructors. Collections are identity-bearing runtime
        // objects while map/set logical equality is implemented by their methods/operators.
        for (const auto& ctor : {std::string("stack"),std::string("queue"),std::string("prique"),std::string("map"),std::string("sorted_map"),std::string("set"),std::string("sorted_set")}) {
            if (text == ctor + "()") {
                auto c=std::make_shared<CollectionInstance>();
                if(ctor=="stack")c->kind=CollectionKind::Stack; else if(ctor=="queue")c->kind=CollectionKind::Queue;
                else if(ctor=="prique")c->kind=CollectionKind::PriQue; else if(ctor=="map")c->kind=CollectionKind::Map;
                else if(ctor=="sorted_map")c->kind=CollectionKind::SortedMap; else if(ctor=="set")c->kind=CollectionKind::Set; else c->kind=CollectionKind::SortedSet;
                const std::string id=std::to_string(next_collection_instance_id_++);collection_instances_[id]=c;
                out=nift::RuntimeValue(std::string("\x1fnift:collection:")+id);return true;
            }
        }
        if (text.rfind("same(",0)==0 && text.back()==')') {
            bool ok=false; auto args=parse_parameters(text.substr(5,text.size()-6),ok); if(!ok||args.size()!=2){error="same: expected two values";return false;}
            const std::string an=trim_copy(args[0]),bn=trim_copy(args[1]);
            VariableBinding la,lb;bool a_loc=false,b_loc=false;
            if(try_location_ref(an,la)&&la.is_location_ref()&&la.value){a_loc=true;}
            else{nift::RuntimeValue av;if(!eval(args[0],av,depth+1))return false;if(last_call_return_loc_root_&&(av.is_array()||av.is_object()||(av.is_string()&&av.string.rfind("\x1fnift:",0)==0))){la.ref_root_slot=last_call_return_loc_root_;la.ref_path=last_call_return_loc_path_;la.sync();if(la.value)a_loc=true;}}
            if(try_location_ref(bn,lb)&&lb.is_location_ref()&&lb.value){b_loc=true;}
            else{nift::RuntimeValue bv;if(!eval(args[1],bv,depth+1))return false;if(last_call_return_loc_root_&&(bv.is_array()||bv.is_object()||(bv.is_string()&&bv.string.rfind("\x1fnift:",0)==0))){lb.ref_root_slot=last_call_return_loc_root_;lb.ref_path=last_call_return_loc_path_;lb.sync();if(lb.value)b_loc=true;}}
            if(a_loc&&b_loc){
                const bool same_root=(la.ref_root_slot&&lb.ref_root_slot&&la.ref_root_slot==lb.ref_root_slot);
                const bool same_path=(la.ref_path==lb.ref_path);
                out=nift::RuntimeValue(same_root&&same_path);return true;
            }
            VariableBinding* ab=find_binding(an);VariableBinding* bb=find_binding(bn);
            if(ab&&bb&&ab->value&&bb->value&&ab->value->is_array()&&bb->value->is_array()){ab->sync();bb->sync();out=nift::RuntimeValue(ab->value==bb->value);return true;}
            nift::RuntimeValue a,b;if(!eval(args[0],a,depth+1)||!eval(args[1],b,depth+1))return false;
            if(a.is_timer()||b.is_timer()){if(!a.is_timer()||!b.is_timer()){error="same: operands must be identity-bearing values";return false;}out=nift::RuntimeValue(nift::runtime_equal(a,b));return true;}
            auto identity=[](const nift::RuntimeValue& v)->std::string{if(!v.is_string())return {}; if(v.string.rfind("\x1fnift:struct:",0)==0||v.string.rfind("\x1fnift:callable:",0)==0||v.string.rfind("\x1fnift:collection:",0)==0||v.string.rfind("\x1fnift:file:",0)==0||v.string.rfind("\x1fnift:stream:",0)==0)return v.string;return {};};
            const auto ai=identity(a),bi=identity(b); if(ai.empty()||bi.empty()){error="same: operands must be identity-bearing values";return false;} out=nift::RuntimeValue(ai==bi);return true;
        }

        {
            const auto lp=text.find('(');
            if(lp!=std::string::npos&&text.back()==')'){std::size_t call_close=0;const std::string target=trim_copy(text.substr(0,lp));const auto dot=target.rfind('.');if(dot!=std::string::npos&&find_balanced(text,lp,'(',')',call_close)&&call_close==text.size()-1){
                const std::string root=target.substr(0,dot), method=target.substr(dot+1); VariableBinding* rb=find_binding(root);
                if(rb&&rb->value&&rb->value->is_string()&&rb->value->string.rfind("\x1fnift:collection:",0)==0){
                    auto ci=collection_instances_.find(rb->value->string.substr(17));if(ci==collection_instances_.end()){error="invalid collection";return false;}auto c=ci->second;
                    bool ok=false;std::vector<bool> quoted_args;auto args=parse_parameters(text.substr(lp+1,text.size()-lp-2),ok,&quoted_args);if(!ok){error="malformed collection method arguments";return false;}
                    auto need_mut=[&](){if(!rb->mutable_binding){error="cannot mutate const collection: "+root;return false;}return true;};
                    auto scalar_order=[](const nift::RuntimeValue&a,const nift::RuntimeValue&b,int& cmp){if(a.is_number()&&b.is_number()){cmp=nift::runtime_compare_numbers(a,b);return true;}if(a.is_string()&&b.is_string()){cmp=a.string<b.string?-1:a.string>b.string?1:0;return true;}return false;};
                    auto invoke_callback=[&](const std::string& cbexpr,const std::vector<nift::RuntimeValue>& av,nift::RuntimeValue& result)->bool{
                        nift::RuntimeValue cb;if(!eval(cbexpr,cb,depth+1))return false;
                        if(!cb.is_string()||cb.string.rfind("\x1fnift:callable:",0)!=0){error="transformation: callback must be callable";return false;}
                        push_variable_scope();auto& sc=variable_scopes_.back();
                        auto csp=std::make_shared<nift::RuntimeValue>(cb);sc["__nift_cb"]=VariableBinding{csp,nift_binding_type(*csp),false,false};
                        std::string call="__nift_cb(";
                        for(size_t i=0;i<av.size();++i){if(i)call+=",";std::string n="__nift_arg"+std::to_string(i);auto sp=std::make_shared<nift::RuntimeValue>(av[i]);sc[n]=VariableBinding{sp,nift_binding_type(*sp),false,false};call+=n;}call+=")";
                        bool r=eval(call,result,depth+1);pop_variable_scope();return r;
                    };
                    if(method=="map"||method=="filter"||method=="reduce"||method=="any"||method=="all"||method=="find"||method=="find_index"||method=="count"||method=="sort_by"||method=="group_by"){
                        if((method=="reduce"&&args.size()!=2)||(method!="reduce"&&args.size()!=1)){error=method+": invalid arguments";return false;}
                        std::vector<std::vector<nift::RuntimeValue>> rows;
                        if(c->kind==CollectionKind::Map||c->kind==CollectionKind::SortedMap){for(const auto&e:c->entries)rows.push_back({e.first,e.second});}
                        else {auto vals=c->values;if(c->kind==CollectionKind::Stack)std::reverse(vals.begin(),vals.end());for(const auto&v:vals)rows.push_back({v});}
                        nift::RuntimeValue arr=nift::RuntimeValue::make_array();nift::RuntimeValue acc;
                        if(method=="reduce"&&!eval(args[1],acc,depth+1))return false;
                        if(method=="all"&&rows.empty()){out=nift::RuntimeValue(true);return true;} if(method=="any"&&rows.empty()){out=nift::RuntimeValue(false);return true;}
                        double cnt=0;
                        for(const auto&row:rows){nift::RuntimeValue r;std::vector<nift::RuntimeValue> ca=row;if(method=="reduce")ca.insert(ca.begin(),acc);if(!invoke_callback(args[0],ca,r))return false;
                            if(method=="map")arr.array.push_back(r);
                            else if(method=="filter"){if(!r.is_bool()){error="filter: callback must return bool";return false;}if(r.boolean)arr.array.push_back(row.size()==1?row[0]:nift::RuntimeValue::make_array());if(r.boolean&&row.size()==2){arr.array.back().array=row;}}
                            else if(method=="reduce")acc=r;
                            else {if(!r.is_bool()){error=method+": callback must return bool";return false;}if(method=="any"&&r.boolean){out=nift::RuntimeValue(true);return true;}if(method=="all"&&!r.boolean){out=nift::RuntimeValue(false);return true;}if(method=="find"&&r.boolean){out=row.size()==1?row[0]:row[1];return true;}if(method=="count"&&r.boolean)cnt+=1;}
                        }
                        if(method=="map"||method=="filter")out=arr;else if(method=="reduce")out=acc;else if(method=="any")out=nift::RuntimeValue(false);else if(method=="all")out=nift::RuntimeValue(true);else if(method=="find")out=nift::RuntimeValue(nullptr);else out=nift::RuntimeValue(cnt);return true;
                    }
                    if(method=="size"||method=="empty"||method=="clear"){if(!args.empty()){error=method+": expected no arguments";return false;}size_t n=(c->kind==CollectionKind::Map||c->kind==CollectionKind::SortedMap)?c->entries.size():c->values.size();if(method=="size")out=nift::RuntimeValue((double)n);else if(method=="empty")out=nift::RuntimeValue(n==0);else{if(!need_mut())return false;c->values.clear();c->entries.clear();c->scalar_keys.clear();c->has_huge_int=false;out=nift::RuntimeValue(nullptr);last_expression_mutation_=true;}return true;}
                    if(c->kind==CollectionKind::Stack||c->kind==CollectionKind::Queue||c->kind==CollectionKind::PriQue){
                        if(method=="push"){if(args.size()!=1){error="push: expected one value";return false;}if(!need_mut())return false;nift::RuntimeValue v;if(quoted_args.size()>0&&quoted_args[0])v=nift::RuntimeValue(args[0]);else if(!eval(args[0],v,depth+1))return false;if(c->kind==CollectionKind::PriQue&&runtime_contains_bytes(v)){error="prique: bytes values are not supported";return false;}if(v.is_string()&&(v.string.rfind("\x1fnift:struct:",0)==0||v.string.rfind("\x1fnift:collection:",0)==0)){if(reference_would_cycle(v.string,rb->value->string)){error="push would create a cyclic reference";return false;}}c->values.push_back(v);if(c->kind==CollectionKind::PriQue){std::stable_sort(c->values.begin(),c->values.end(),[&](const auto&a,const auto&b){int z=0;return scalar_order(a,b,z)&&z<0;});}out=v;last_expression_mutation_=true;return true;}
                        if(method=="pop"||method=="top"||method=="front"){if(!args.empty()){error=method+": expected no arguments";return false;}if(c->values.empty()){error=method+": collection is empty";return false;}size_t i=(c->kind==CollectionKind::Stack&&(method=="pop"||method=="top"))?c->values.size()-1:0;out=c->values[i];if(method=="pop"){if(!need_mut())return false;c->values.erase(c->values.begin()+i);last_expression_mutation_=true;}return true;}
                        if(method=="contains"){if(args.size()!=1){error="contains: expected one value";return false;}nift::RuntimeValue v;if(quoted_args.size()>0&&quoted_args[0])v=nift::RuntimeValue(args[0]);else if(!eval(args[0],v,depth+1))return false;bool f=false;for(auto&w:c->values)if(structural_equal(v,w)){f=true;break;}out=nift::RuntimeValue(f);return true;}
                    }
                    if(c->kind==CollectionKind::Set||c->kind==CollectionKind::SortedSet){
                        if(method=="add"||method=="contains"||method=="remove"){if(args.size()!=1){error=method+": expected one value";return false;}nift::RuntimeValue v;if(quoted_args.size()>0&&quoted_args[0])v=nift::RuntimeValue(args[0]);else if(!eval(args[0],v,depth+1))return false;if(!(v.is_bool()||v.is_number()||v.is_string())){error=method+": set values must be bool, number, or string";return false;}
                         auto set_scalar_key=[&](const nift::RuntimeValue& x)->std::string{return runtime_scalar_key(x);};
                        auto set_indexed=[&](const nift::RuntimeValue& x)->bool{const std::string k=set_scalar_key(x);return !k.empty()&&c->scalar_keys.count(k)!=0;};
                        auto set_find=[&](const nift::RuntimeValue& x)->size_t{size_t j=0;for(;j<c->values.size();++j)if(structural_equal(c->values[j],x))break;return j;};
                         // Fingerprints select candidate buckets; equality still
                         // confirms identity, notably leaving NaNs distinct.
                         const bool indexed=!set_scalar_key(v).empty();
                        const size_t i=indexed?(set_indexed(v)?set_find(v):c->values.size()):set_find(v);
                        if(method=="contains"){out=nift::RuntimeValue(i<c->values.size());return true;}if(!need_mut())return false;
                        if(method=="add"&&i==c->values.size()){c->values.push_back(v);const std::string k=set_scalar_key(v);if(!k.empty())c->scalar_keys.insert(k);if(v.type==nift::RuntimeType::StrNumber)c->has_huge_int=true;if(c->kind==CollectionKind::SortedSet)std::stable_sort(c->values.begin(),c->values.end(),[&](const auto&a,const auto&b){int z=0;if(!scalar_order(a,b,z)){error="sorted_set: incomparable values";return false;}return z<0;});}
                         else if(method=="remove"&&i<c->values.size()){const std::string k=set_scalar_key(v);c->values.erase(c->values.begin()+i);if(!k.empty()&&std::none_of(c->values.begin(),c->values.end(),[&](const auto& item){return set_scalar_key(item)==k;}))c->scalar_keys.erase(k);}
                        out=v;last_expression_mutation_=true;return true;}
                    }
                    if(c->kind==CollectionKind::Map||c->kind==CollectionKind::SortedMap){
                         auto mkey=[&](const nift::RuntimeValue& x)->std::string{return runtime_scalar_key(x);};
                        if(method=="set"){if(args.size()!=2){error="set: expected key and value";return false;}if(!need_mut())return false;nift::RuntimeValue k,v;if(quoted_args.size()>0&&quoted_args[0])k=nift::RuntimeValue(args[0]);else if(!eval(args[0],k,depth+1))return false;if(quoted_args.size()>1&&quoted_args[1])v=nift::RuntimeValue(args[1]);else if(!eval(args[1],v,depth+1))return false;if(!(k.is_bool()||k.is_number()||k.is_string())){error="map: key must be bool, number, or string";return false;}if(v.is_string()&&(v.string.rfind("\x1fnift:struct:",0)==0||v.string.rfind("\x1fnift:collection:",0)==0)){if(reference_would_cycle(v.string,rb->value->string)){error="set would create a cyclic reference";return false;}}
                         const std::string mk=mkey(k);const bool mind=!mk.empty();
                        size_t i;if(mind&&c->scalar_keys.count(mk)){for(i=0;i<c->entries.size();++i)if(structural_equal(c->entries[i].first,k))break;}else if(mind){i=c->entries.size();}else{for(i=0;i<c->entries.size();++i)if(structural_equal(c->entries[i].first,k))break;}
                        if(i<c->entries.size())c->entries[i].second=v;else{c->entries.push_back({k,v});if(mind)c->scalar_keys.insert(mk);}if(k.type==nift::RuntimeType::StrNumber)c->has_huge_int=true;if(c->kind==CollectionKind::SortedMap)std::stable_sort(c->entries.begin(),c->entries.end(),[&](const auto&a,const auto&b){int z=0;if(!scalar_order(a.first,b.first,z)){error="sorted_map: incomparable keys";return false;}return z<0;});out=v;last_expression_mutation_=true;return true;}
                        if(method=="contains"||method=="get"||method=="remove"){if(args.size()!=1){error=method+": expected one key";return false;}nift::RuntimeValue k;if(quoted_args.size()>0&&quoted_args[0])k=nift::RuntimeValue(args[0]);else if(!eval(args[0],k,depth+1))return false;
                         const std::string mk=mkey(k);const bool mind=!mk.empty();
                        size_t i;if(mind&&c->scalar_keys.count(mk)){for(i=0;i<c->entries.size();++i)if(structural_equal(c->entries[i].first,k))break;}else{for(i=0;i<c->entries.size();++i)if(structural_equal(c->entries[i].first,k))break;}
                         if(method=="contains"){out=nift::RuntimeValue(i<c->entries.size());return true;}if(i==c->entries.size()){error=method+": key not found";return false;}out=c->entries[i].second;if(method=="remove"){if(!need_mut())return false;c->entries.erase(c->entries.begin()+i);if(mind&&std::none_of(c->entries.begin(),c->entries.end(),[&](const auto& entry){return mkey(entry.first)==mk;}))c->scalar_keys.erase(mk);last_expression_mutation_=true;}return true;}
                    }
                }
            }}
        }

        {
            const auto lp=text.find('(');
            if(lp!=std::string::npos&&text.back()==')'){std::size_t call_close=0;const std::string target=trim_copy(text.substr(0,lp));const auto dot=target.rfind('.');if(dot!=std::string::npos&&find_balanced(text,lp,'(',')',call_close)&&call_close==text.size()-1){
                const std::string root=target.substr(0,dot), method=target.substr(dot+1); VariableBinding* rb=find_binding(root);
                if(rb&&rb->value&&rb->value->is_array()){bool ok=false;std::vector<bool> quoted_args;auto args=parse_parameters(text.substr(lp+1,text.size()-lp-2),ok,&quoted_args);if(!ok){error="malformed array method arguments";return false;}auto eval_arg=[&](size_t n,nift::RuntimeValue& v){if(n<quoted_args.size()&&quoted_args[n]){v=nift::RuntimeValue(args[n]);return true;}return eval(args[n],v,depth+1);};auto& a=rb->value->array;
                    auto invoke_callback=[&](const std::string& cbexpr,const std::vector<nift::RuntimeValue>& av,nift::RuntimeValue& result)->bool{
                        nift::RuntimeValue cb;if(!eval(cbexpr,cb,depth+1))return false;if(!cb.is_string()||cb.string.rfind("\x1fnift:callable:",0)!=0){error="transformation: callback must be callable";return false;}
                        push_variable_scope();auto& sc=variable_scopes_.back();auto csp=std::make_shared<nift::RuntimeValue>(cb);sc["__nift_cb"]=VariableBinding{csp,nift_binding_type(*csp),false,false};std::string call="__nift_cb(";
                        for(size_t i=0;i<av.size();++i){if(i)call+=",";std::string n="__nift_arg"+std::to_string(i);auto sp=std::make_shared<nift::RuntimeValue>(av[i]);sc[n]=VariableBinding{sp,nift_binding_type(*sp),false,false};call+=n;}call+=")";bool r=eval(call,result,depth+1);pop_variable_scope();return r;};
                    if(method=="map"||method=="filter"||method=="reduce"||method=="any"||method=="all"||method=="find"||method=="find_index"||method=="count"||method=="sort_by"||method=="group_by"){
                        if((method=="reduce"&&args.size()!=2)||(method!="reduce"&&args.size()!=1)){error=method+": invalid arguments";return false;}nift::RuntimeValue arr=nift::RuntimeValue::make_array(),acc;if(method=="reduce"&&!eval(args[1],acc,depth+1))return false;double cnt=0;std::vector<std::pair<nift::RuntimeValue,nift::RuntimeValue>> keyed;
                        for(size_t vi=0;vi<a.size();++vi){const auto&v=a[vi];nift::RuntimeValue r;if(!invoke_callback(args[0],method=="reduce"?std::vector<nift::RuntimeValue>{acc,v}:std::vector<nift::RuntimeValue>{v},r))return false;if(method=="map")arr.array.push_back(r);else if(method=="filter"){if(!r.is_bool()){error="filter: callback must return bool";return false;}if(r.boolean)arr.array.push_back(v);}else if(method=="reduce")acc=r;else if(method=="group_by"){if(!(r.is_string()||r.is_number()||r.is_bool())){error=method+": key must be scalar";return false;}keyed.push_back({r,v});}else{if(!r.is_bool()){error=method+": callback must return bool";return false;}if(method=="any"&&r.boolean){out=nift::RuntimeValue(true);return true;}if(method=="all"&&!r.boolean){out=nift::RuntimeValue(false);return true;}if(method=="find"&&r.boolean){out=v;return true;}if(method=="find_index"&&r.boolean){out=nift::RuntimeValue((double)vi);return true;}if(method=="count"&&r.boolean)cnt+=1;}}
                        if(method=="sort_by"){std::stable_sort(keyed.begin(),keyed.end(),[&](const auto&x,const auto&y){if(x.first.is_number()&&y.first.is_number())return nift::runtime_compare_numbers(x.first,y.first)<0;if(x.first.is_string()&&y.first.is_string())return x.first.string<y.first.string;if(x.first.is_bool()&&y.first.is_bool())return x.first.boolean<y.first.boolean;return (int)x.first.type<(int)y.first.type;});out=nift::RuntimeValue::make_array();for(auto&kv:keyed)out.array.push_back(kv.second);return true;}
                        if(method=="group_by"){out=nift::RuntimeValue::make_object();for(auto&kv:keyed){if(kv.first.is_timer()){error="group_by: timer values cannot be object keys";return false;}std::string k=kv.first.is_string()?kv.first.string:render_expression_value(kv.first);nift::RuntimeValue* bucket=nullptr;for(auto&e:out.object)if(e.first==k){bucket=&e.second;break;}if(!bucket){out[k]=nift::RuntimeValue::make_array();for(auto&e:out.object)if(e.first==k){bucket=&e.second;break;}}bucket->array.push_back(kv.second);}return true;}
                        if(method=="map"||method=="filter")out=arr;else if(method=="reduce")out=acc;else if(method=="any")out=nift::RuntimeValue(false);else if(method=="all")out=nift::RuntimeValue(true);else if(method=="find"||method=="find_index")out=nift::RuntimeValue(method=="find"?nift::RuntimeValue(nullptr):nift::RuntimeValue(-1.0));else out=nift::RuntimeValue(cnt);return true;}
                    if(method=="unique"||method=="flatten"||method=="sum"||method=="min"||method=="max"){
                        if(!args.empty()){error=method+": expected no arguments";return false;}
                        if(method=="unique"){out=nift::RuntimeValue::make_array();for(const auto&v:a){bool seen=false;for(const auto&w:out.array)if(structural_equal(v,w)){seen=true;break;}if(!seen)out.array.push_back(v);}return true;}
                        if(method=="flatten"){out=nift::RuntimeValue::make_array();for(const auto&v:a){if(v.is_array())out.array.insert(out.array.end(),v.array.begin(),v.array.end());else out.array.push_back(v);}return true;}
                        if(a.empty()){if(method=="sum"){out=nift::RuntimeValue(0.0);return true;}error=method+": cannot aggregate an empty array";return false;}
                        if(method=="sum"){double total=0;for(const auto&v:a){if(!v.is_number()){error="sum: values must be numeric";return false;}total+=v.num;}out=nift::RuntimeValue(total);return true;}
                        out=a.front();for(size_t i=1;i<a.size();++i){const auto&v=a[i];bool take=false;if(out.is_number()&&v.is_number()){const int cmp=nift::runtime_compare_numbers(v,out);take=method=="min"?cmp<0:cmp>0;}else if(out.is_string()&&v.is_string())take=method=="min"?v.string<out.string:v.string>out.string;else{error=method+": values must be comparable and homogeneous";return false;}if(take)out=v;}return true;
                    }
                    if(method=="sort"){
                        if(args.size()>1){error="sort: expected zero or one comparator";return false;}if(!rb->mutable_binding){error="cannot mutate const array: "+root;return false;}bool failed=false;std::string ferr;
                        std::stable_sort(a.begin(),a.end(),[&](const auto&x,const auto&y){if(failed)return false;if(args.empty()){if(x.is_number()&&y.is_number())return nift::runtime_compare_numbers(x,y)<0;if(x.is_string()&&y.is_string())return x.string<y.string;failed=true;ferr="sort: incomparable values";return false;}nift::RuntimeValue r;if(!invoke_callback(args[0],{x,y},r)){failed=true;ferr=error;return false;}if(!r.is_bool()){failed=true;ferr="sort: comparator must return bool";return false;}return r.boolean;});if(failed){error=ferr;return false;}out=*rb->value;last_expression_mutation_=true;return true;}
                    if(method=="size"||method=="empty"||method=="first"||method=="last"||method=="pop"||method=="clear"||method=="push"||method=="insert"||method=="remove"||method=="indexOf"||method=="contains"||method=="join"||method=="slice"||method=="splice"||method=="reverse"){
                        if((method=="size"||method=="empty"||method=="first"||method=="last"||method=="pop"||method=="clear")&&!args.empty()){error=method+": expected no arguments";return false;}
                        if(method=="join"){if(args.size()!=1){error="join: expected separator";return false;}nift::RuntimeValue sep;if(!eval_arg(0,sep)||!sep.is_string()){error="join: separator must be a string";return false;}std::string joined;for(size_t j=0;j<a.size();++j){if(j)joined+=sep.string;if(a[j].is_timer()||a[j].is_bytes()||a[j].is_array()||a[j].is_object()||(a[j].is_string()&&a[j].string.rfind("\x1fnift:",0)==0&&a[j].string.rfind("\x1fnift:timer:",0)!=0)){error="join: elements must be renderable scalar values";return false;}joined+=render_expression_value(a[j]);}out=nift::RuntimeValue(joined);return true;}
                        if(method=="slice"){if(args.empty()||args.size()>2){error="slice: expected start and optional end";return false;}nift::RuntimeValue st,en;size_t b=0,e=a.size();if(!eval(args[0],st,depth+1)||!nift::runtime_number_to_size(st,b)){error="slice: invalid start";return false;}if(args.size()==2){if(!eval(args[1],en,depth+1)||!nift::runtime_number_to_size(en,e)){error="slice: invalid end";return false;}}if(b>a.size())b=a.size();if(e>a.size())e=a.size();if(e<b)e=b;out=nift::RuntimeValue::make_array();out.array.assign(a.begin()+b,a.begin()+e);return true;}
                        if(method=="splice"){if(args.size()<2||args.size()>3){error="splice: expected start, delete_count, optional replacement array";return false;}if(!rb->mutable_binding){error="cannot mutate const array: "+root;return false;}nift::RuntimeValue st,dc,repl;size_t b=0,count=0;if(!eval(args[0],st,depth+1)||!eval(args[1],dc,depth+1)||!nift::runtime_number_to_size(st,b)||!nift::runtime_number_to_size(dc,count)){error="splice: invalid index/count";return false;}if(b>a.size())b=a.size();size_t n=std::min(count,a.size()-b);out=nift::RuntimeValue::make_array();out.array.assign(a.begin()+b,a.begin()+b+n);a.erase(a.begin()+b,a.begin()+b+n);if(args.size()==3){if(!eval(args[2],repl,depth+1)||!repl.is_array()){error="splice: replacements must be an array";return false;}a.insert(a.begin()+b,repl.array.begin(),repl.array.end());}last_expression_mutation_=true;return true;}
                        if(method=="reverse"){if(!args.empty()){error="reverse: expected no arguments";return false;}if(!rb->mutable_binding){error="cannot mutate const array: "+root;return false;}std::reverse(a.begin(),a.end());out=*rb->value;last_expression_mutation_=true;return true;}
                        if(method=="size"){out=nift::RuntimeValue(static_cast<double>(a.size()));return true;} if(method=="empty"){out=nift::RuntimeValue(a.empty());return true;}
                        if(method=="first"||method=="last"||method=="pop"){if(a.empty()){error=method+": array is empty";return false;}out=method=="first"?a.front():a.back();if(method=="pop"){if(!rb->mutable_binding){error="cannot mutate const array: "+root;return false;}a.pop_back();last_expression_mutation_=true;}return true;}
                        if(method=="clear"){if(!rb->mutable_binding){error="cannot mutate const array: "+root;return false;}a.clear();out=nift::RuntimeValue(nullptr);last_expression_mutation_=true;return true;}
                        if(method=="push"){if(args.size()!=1){error="push: expected one argument";return false;}if(!rb->mutable_binding){error="cannot mutate const array: "+root;return false;}nift::RuntimeValue v;if(quoted_args.size()>0&&quoted_args[0])v=nift::RuntimeValue(args[0]);else if(!eval(args[0],v,depth+1))return false;a.push_back(v);out=v;last_expression_mutation_=true;return true;}
                        if(method=="insert"){if(args.size()!=2){error="insert: expected index and value";return false;}if(!rb->mutable_binding){error="cannot mutate const array: "+root;return false;}nift::RuntimeValue ix,v;size_t i=0;if(!eval(args[0],ix,depth+1)||!eval(args[1],v,depth+1)||!nift::runtime_number_to_size(ix,i)){error="insert: invalid index";return false;}if(i>a.size()){error="insert: index out of range";return false;}a.insert(a.begin()+i,v);out=v;last_expression_mutation_=true;return true;}
                        if(method=="remove"){if(args.size()!=1){error="remove: expected index";return false;}if(!rb->mutable_binding){error="cannot mutate const array: "+root;return false;}nift::RuntimeValue ix;size_t i=0;if(!eval(args[0],ix,depth+1)||!nift::runtime_number_to_size(ix,i)){error="remove: invalid index";return false;}if(i>=a.size()){error="remove: index out of range";return false;}out=a[i];a.erase(a.begin()+i);last_expression_mutation_=true;return true;}
                        if(method=="indexOf"||method=="contains"){if(args.size()!=1){error=method+": expected one value";return false;}nift::RuntimeValue v;if(!eval(args[0],v,depth+1))return false;std::size_t i=0;for(;i<a.size();++i)if(structural_equal(a[i],v))break;if(method=="contains")out=nift::RuntimeValue(i<a.size());else out=nift::RuntimeValue(i<a.size()?static_cast<double>(i):-1.0);return true;}
                    }
                }
            }}
        }

        {
            const auto lp=text.find('(');
            if(lp!=std::string::npos&&text.back()==')'){std::size_t call_close=0;const std::string target=trim_copy(text.substr(0,lp));const auto dot=target.rfind('.');if(dot!=std::string::npos&&find_balanced(text,lp,'(',')',call_close)&&call_close==text.size()-1){
                const std::string root=target.substr(0,dot),method=target.substr(dot+1);VariableBinding* rb=find_binding(root);
                if(rb&&rb->value&&rb->value->is_string()&&rb->value->string.rfind("\x1fnift:stream:",0)==0){auto it=stream_instances_.find(rb->value->string.substr(13));if(it==stream_instances_.end()||it->second->closed){error="stream is closed or invalid";return false;}auto st=it->second;bool ok=false;std::vector<bool> qq;auto aa=parse_parameters(text.substr(lp+1,text.size()-lp-2),ok,&qq);if(!ok){error="malformed stream method arguments";return false;}
                    if(method=="eof"){if(!aa.empty()||st->kind!=StreamInstance::Kind::Input){error="eof: expected input stream and no arguments";return false;}out=nift::RuntimeValue(st->input->peek()==std::char_traits<char>::eof());return true;}
                    if(method=="read_line"){if(!aa.empty()||st->kind!=StreamInstance::Kind::Input){error="read_line: expected input stream and no arguments";return false;}std::string line;if(!std::getline(*st->input,line)){if(st->input->eof()){st->input->clear();out=nift::RuntimeValue(nullptr);return true;}error="read_line: input failure";return false;}out=nift::RuntimeValue(line);return true;}
                    if(method=="read_all"){if(!aa.empty()||st->kind!=StreamInstance::Kind::Input){error="read_all: expected input stream and no arguments";return false;}std::ostringstream ss;ss<<st->input->rdbuf();out=nift::RuntimeValue(ss.str());return true;}
                    if(method=="read"){if(aa.size()!=1||st->kind!=StreamInstance::Kind::Input){error="read: expected byte count on input stream";return false;}nift::RuntimeValue n;size_t count=0;if(!eval(aa[0],n,depth+1)||!nift::runtime_number_to_size(n,count)){error="read: invalid byte count";return false;}constexpr std::size_t chunk_size=64*1024;std::vector<char> chunk(chunk_size);std::string b;std::size_t remaining=count;while(remaining>0&&*st->input){const auto requested=static_cast<std::streamsize>(std::min(remaining,chunk_size));st->input->read(chunk.data(),requested);const auto received=st->input->gcount();if(received<=0)break;b.append(chunk.data(),static_cast<std::size_t>(received));remaining-=static_cast<std::size_t>(received);if(received<requested)break;}out=nift::RuntimeValue(b);return true;}
                    if(method=="read_bytes"||method=="read_all_bytes"){if(st->kind!=StreamInstance::Kind::Input){error=method+": expected input stream";return false;}if(st->input->bad()){error=method+": input failure";return false;}std::size_t count=std::numeric_limits<std::size_t>::max();if(method=="read_bytes"){if(aa.size()!=1){error="read_bytes: expected byte count on input stream";return false;}nift::RuntimeValue n;if(!eval(aa[0],n,depth+1)||!nift::runtime_number_to_size(n,count)){error="read_bytes: invalid byte count";return false;}}else if(!aa.empty()){error="read_all_bytes: expected input stream and no arguments";return false;}constexpr std::size_t chunk_size=64*1024;std::vector<char> chunk(chunk_size);nift::RuntimeBytes b;while(count>0&&*st->input){const std::size_t wanted=std::min(count,chunk_size);st->input->read(chunk.data(),static_cast<std::streamsize>(wanted));const auto received=st->input->gcount();if(received>0)b.insert(b.end(),chunk.begin(),chunk.begin()+received);if(st->input->bad()){error=method+": input failure";return false;}if(received<=0)break;if(count!=std::numeric_limits<std::size_t>::max())count-=static_cast<std::size_t>(received);if(static_cast<std::size_t>(received)<wanted)break;}if(st->input->bad()){error=method+": input failure";return false;}out=nift::RuntimeValue(std::move(b));return true;}
                    if(method=="read_val"){if(!aa.empty()||st->kind!=StreamInstance::Kind::Input){error="read_val: expected input stream and no arguments";return false;}*st->input>>std::ws;if(st->input->peek()==std::char_traits<char>::eof()){out=nift::RuntimeValue(nullptr);return true;}std::string token;char first=(char)st->input->peek();if(first=='"'||first=='['||first=='{'){char open=first,close=first=='['?']':first=='{'?'}':'"';int dep=0;bool quoted=false,esc=false;char c;while(st->input->get(c)){token+=c;if(open=='"'){if(esc){esc=false;continue;}if(c=='\\'){esc=true;continue;}if(token.size()>1&&c=='"')break;}else{if(quoted){if(esc)esc=false;else if(c=='\\')esc=true;else if(c=='"')quoted=false;}else if(c=='"')quoted=true;else if(c==open)++dep;else if(c==close&&--dep==0)break;}}}else{while(st->input->peek()!=std::char_traits<char>::eof()&&!std::isspace((unsigned char)st->input->peek()))token+=(char)st->input->get();}nift::RuntimeValue v;std::string ee;if(!eval(token,v,depth+1)){error=std::string("read_val: ")+(error.empty()?("cannot parse value '"+token+"'"):error);return false;}if(runtime_contains_bytes(v)){error="read_val: bytes values are not serializable";return false;}out=v;return true;}
                    if(method=="write_val"){if(aa.size()!=1||st->kind!=StreamInstance::Kind::Output){error="write_val: expected one value on output stream";return false;}nift::RuntimeValue v;if(!((!qq.empty()&&qq[0])?(v=nift::RuntimeValue(aa[0]),true):eval(aa[0],v,depth+1)))return false;if(v.is_string()&&!qq.empty()&&qq[0]&&v.string.find("$[")!=std::string::npos){std::string r,e;if(!interpolate_parameter(v.string,r,e)){error="write_val: "+e;return false;}v=nift::RuntimeValue(r);}std::string serialized;if(!serialize_value(v,false,serialized,error))return false;*st->output<<serialized<<'\n';if(!*st->output){error="write_val: output failure";return false;}out=nift::RuntimeValue(nullptr);return true;}
                    if(method=="write"||method=="write_line"){if(aa.size()!=1||st->kind!=StreamInstance::Kind::Output){error=method+": expected one value on output stream";return false;}nift::RuntimeValue v;if(!((!qq.empty()&&qq[0])?(v=nift::RuntimeValue(aa[0]),true):eval(aa[0],v,depth+1)))return false;if(v.is_string()&&!qq.empty()&&qq[0]&&v.string.find("$[")!=std::string::npos){std::string r,e;if(!interpolate_parameter(v.string,r,e)){error=method+": "+e;return false;}v=nift::RuntimeValue(r);}if(method=="write"&&v.is_bytes()){const nift::RuntimeBytes empty;const auto& raw=v.bytes?*v.bytes:empty;st->output->write(reinterpret_cast<const char*>(raw.data()),static_cast<std::streamsize>(raw.size()));}else{if(v.is_timer()||v.is_bytes()||v.is_array()||v.is_object()||(v.is_string()&&v.string.rfind("\x1fnift:",0)==0&&v.string.rfind("\x1fnift:timer:",0)!=0)){error=method+": value is not directly renderable";return false;}*st->output<<render_expression_value(v);if(method=="write_line")*st->output<<'\n';}if(!*st->output){error=method+": output failure";return false;}out=nift::RuntimeValue(nullptr);return true;}
                    if(method=="flush"){if(!aa.empty()||st->kind!=StreamInstance::Kind::Output){error="flush: expected output stream and no arguments";return false;}st->output->flush();if(!*st->output){error="flush: output failure";return false;}out=nift::RuntimeValue(nullptr);return true;}
                }
            }}
            if(lp!=std::string::npos&&text.back()==')'){std::size_t call_close=0;const std::string target=trim_copy(text.substr(0,lp));const auto dot=target.rfind('.');if(dot!=std::string::npos&&find_balanced(text,lp,'(',')',call_close)&&call_close==text.size()-1){
                const std::string root=target.substr(0,dot),method=target.substr(dot+1);VariableBinding* rb=find_binding(root);
                if(rb&&rb->value&&rb->value->is_string()&&method=="substr") { bool ok=false;auto args=parse_parameters(text.substr(lp+1,text.size()-lp-2),ok);if(!ok||args.empty()||args.size()>2){error="substr: expected pos and optional length";return false;}nift::RuntimeValue pos,len;size_t p=0;if(!eval(args[0],pos,depth+1)||!nift::runtime_number_to_size(pos,p)){error="substr: invalid pos";return false;}if(p>rb->value->string.size())p=rb->value->string.size();size_t n=std::string::npos;if(args.size()==2){if(!eval(args[1],len,depth+1)||!nift::runtime_number_to_size(len,n)){error="substr: invalid length";return false;}}out=nift::RuntimeValue(rb->value->string.substr(p,n));return true;}
            }}
        }

        {
            const auto lp = text.find('(');
            std::size_t call_close = 0;
            const bool terminal_call = lp != std::string::npos && text.back() == ')' &&
                find_balanced(text, lp, '(', ')', call_close) && call_close == text.size() - 1;
            auto method_arguments = [&](const StructMethod& method,
                                        std::vector<nift::RuntimeValue>& values,
                                        std::vector<std::string>& sources) -> bool {
                bool args_ok=false; std::vector<bool> quoted;
                auto args=parse_parameters(text.substr(lp+1,text.size()-lp-2),args_ok,&quoted);
                if(!args_ok){error="malformed method arguments";return false;}
                std::vector<nift::RuntimeValue> spread_values;
                std::vector<std::string> expanded;std::vector<bool> expanded_quoted;
                for(std::size_t ai=0;ai<args.size();++ai){
                    if(!(ai<quoted.size()&&quoted[ai])&&trim_copy(args[ai]).rfind("...",0)==0){
                        nift::RuntimeValue spread;
                        if(!eval(trim_copy(args[ai]).substr(3),spread,depth+1))return false;
                        if(!spread.is_array()){error="spread value must be an array";return false;}
                        for(const auto& item:spread.array){expanded.push_back("\x1fnift:spread:"+std::to_string(spread_values.size()));spread_values.push_back(item);expanded_quoted.push_back(false);}
                    }else{expanded.push_back(args[ai]);expanded_quoted.push_back(ai<quoted.size()&&quoted[ai]);}
                }
                args.swap(expanded);quoted.swap(expanded_quoted);
                if(method.callable.variadic_param.empty()?args.size()!=method.callable.params.size():args.size()<method.callable.params.size()){error="struct method argument count mismatch";return false;}
                for(std::size_t ai=0;ai<args.size();++ai){
                    nift::RuntimeValue value;
                    if(ai<quoted.size()&&quoted[ai])value=nift::RuntimeValue(args[ai]);
                    else if(args[ai].rfind("\x1fnift:spread:",0)==0)value=spread_values[static_cast<std::size_t>(std::stoull(args[ai].substr(13)))];
                    else if(!eval(args[ai],value,depth+1))return false;
                    values.push_back(std::move(value));sources.push_back(args[ai].rfind("\x1fnift:spread:",0)==0?std::string():args[ai]);
                }
                return true;
            };
            if (terminal_call && text.substr(0,lp).find('.') != std::string::npos) {
                const std::string target=trim_copy(text.substr(0,lp)); const auto dot=target.rfind('.'); const std::string root=target.substr(0,dot), mn=target.substr(dot+1);
                VariableBinding* rb=nullptr;for(auto scope=variable_scopes_.rbegin();scope!=variable_scopes_.rend();++scope){auto it=scope->find(root);if(it!=scope->end()){rb=&it->second;break;}}
                if(rb&&rb->value->is_string()&&rb->value->string.rfind("\x1fnift:struct:",0)==0){auto inst=struct_instances_.find(rb->value->string.substr(13));if(inst==struct_instances_.end()){error="invalid struct instance";return false;}auto sd=structs_.find(inst->second->type_name);if(sd==structs_.end()){error="struct type is not available in this scope: "+inst->second->type_name;return false;}auto mi=sd->second.methods.find(mn);if((mi==sd->second.methods.end()||mi->second.constructor)){
                // A callable FIELD acts as a method (module-style values such as
                // vips.resize(...)). Invoke the field's callable directly.
                auto ff=inst->second->fields.find(mn);
                if(ff!=inst->second->fields.end()&&ff->second.value&&ff->second.value->is_string()&&ff->second.value->string.rfind("\x1fnift:callable:",0)==0){
                    bool priv=false;
                    for(const auto& field:sd->second.fields)if(field.name==mn){priv=field.private_member;break;}
                    if(priv&&(receiver_stack_.empty()||receiver_stack_.back()!=inst->second)){error="private struct field: "+mn;return false;}
                    // Reconstruct a call that preserves quoted string arguments
                    // (parse_parameters strips quotes and unescapes; re-escape
                    // so inner quotes/backslashes/control characters survive).
                    auto reescape_parameter=[&](const std::string& raw)->std::string{std::string o;o.reserve(raw.size());for(char c:raw){if(c=='\\'){o+="\\\\";}else if(c=='"'){o+="\\\"";}else if(c=='\n'){o+="\\n";}else if(c=='\r'){o+="\\r";}else if(c=='\t'){o+="\\t";}else{o+=c;}}return o;};
                    std::string call = "("; bool ok=false; std::vector<bool> qq; auto ar=parse_parameters(text.substr(lp+1,text.size()-lp-2),ok,&qq); if(!ok){error="malformed arguments";return false;} for(size_t i=0;i<ar.size();++i){if(i)call+=","; if(i<qq.size()&&qq[i])call+="\""+reescape_parameter(ar[i])+"\""; else call+=ar[i];} call+=")";
                    nift::RuntimeValue cb=*ff->second.value; push_variable_scope(); auto& sc=variable_scopes_.back(); auto csp=std::make_shared<nift::RuntimeValue>(std::move(cb)); sc["__nift_module_field"]=VariableBinding{csp,nift_binding_type(*csp),false,false};
                    bool r=eval("__nift_module_field"+call,out,depth+1); pop_variable_scope(); return r;
                }
                error="struct has no method: "+mn;return false;}if(mi->second.private_member&&(receiver_stack_.empty()||receiver_stack_.back()!=inst->second)){error="private struct method: "+mn;return false;}std::vector<nift::RuntimeValue> av;std::vector<std::string> ar;if(!method_arguments(mi->second,av,ar))return false;return invoke_struct_method(inst->second,mi->second,av,ar,out,error);}
            }
            if (terminal_call && valid_binding_identifier(trim_copy(text.substr(0, lp)))) {
                const std::string call_name = trim_copy(text.substr(0, lp));
                auto si = structs_.find(call_name);
                if (si != structs_.end()) {
                    bool args_ok=false; std::vector<bool> q; auto args=parse_parameters(text.substr(lp+1,text.size()-lp-2),args_ok,&q);
                    auto ctor=si->second.methods.find(call_name); const std::size_t expected=ctor==si->second.methods.end()?0:ctor->second.callable.params.size();
                    if(!args_ok||args.size()!=expected){error="struct constructor argument count mismatch: "+call_name;return false;}
                    auto instance=std::make_shared<StructInstance>(); instance->type_name=call_name;
                    auto initializer_env=enter_lexical_environment(si->second.module_env);
                    bool initializers_ok=true;
                    for(const auto& field:si->second.fields){ nift::RuntimeValue fv; if(!eval(field.initializer,fv,depth+1)){initializers_ok=false;break;} auto sp=std::make_shared<nift::RuntimeValue>(std::move(fv)); instance->fields.emplace(field.name,VariableBinding{sp,nift_binding_type_from_text(field.initializer,*sp),true,false}); }
                    leave_lexical_environment(std::move(initializer_env));
                    if(!initializers_ok)return false;
                    const std::string id=std::to_string(next_struct_instance_id_++); struct_instances_[id]=instance;
                    if(ctor!=si->second.methods.end()){std::vector<nift::RuntimeValue> av;for(std::size_t ai=0;ai<args.size();++ai){nift::RuntimeValue v;if(ai<q.size()&&q[ai])v=nift::RuntimeValue(args[ai]);else if(!eval(args[ai],v,depth+1))return false;av.push_back(std::move(v));}nift::RuntimeValue ignored;if(!invoke_struct_method(instance,ctor->second,av,args,ignored,error))return false;}
                    out=nift::RuntimeValue(std::string("\x1fnift:struct:")+id); return true;
                }
                auto ci = callables_.find(call_name);
                const Callable* module_callee=nullptr;std::shared_ptr<ModuleEnv> module_callee_env;if(active_module_env_){auto mi=active_module_env_->callables.find(call_name);if(mi!=active_module_env_->callables.end()){module_callee=&mi->second;module_callee_env=mi->second.module_env ? mi->second.module_env : active_module_env_;}}
                // Indirect first-class callable invocation.
                {
                    VariableBinding* cb=find_binding(call_name);
                    if(cb&&cb->value&&cb->value->is_string()&&cb->value->string.rfind("\x1fnift:callable:",0)==0){
                        const std::string tag=cb->value->string;
                        if(tag.rfind("\x1fnift:callable:named:",0)==0){const Callable* resolved=nullptr;std::shared_ptr<ModuleEnv> owner;if(!resolve_named_callable(tag,resolved,owner)){error="invalid callable";return false;}if(owner){module_callee=resolved;module_callee_env=std::move(owner);}else{ci=callables_.find(tag.substr(21));module_callee=nullptr;module_callee_env.reset();}}
                        else if(tag.rfind("\x1fnift:callable:lambda:",0)==0){
                            auto li=lambda_instances_.find(tag.substr(22));if(li==lambda_instances_.end()){error="invalid lambda";return false;}auto fn=li->second;
                            if (callable_call_depth_ >= kMaxCallableDepth) { error = "callable recursion depth exceeded"; return false; }
                            ++callable_call_depth_;
                            std::vector<nift::RuntimeValue> spread_args;bool aok=false;std::vector<bool> aq;auto ar=parse_parameters(text.substr(lp+1,text.size()-lp-2),aok,&aq);if(aok){std::vector<std::string> ex;std::vector<bool> eq;for(size_t ai=0;ai<ar.size();++ai){if(!(ai<aq.size()&&aq[ai])&&trim_copy(ar[ai]).rfind("...",0)==0){nift::RuntimeValue sv;if(!eval(trim_copy(ar[ai]).substr(3),sv,depth+1)){--callable_call_depth_;return false;}if(!sv.is_array()){error="spread value must be an array";--callable_call_depth_;return false;}for(const auto& item:sv.array){ex.push_back("\x1fnift:spread:"+std::to_string(spread_args.size()));spread_args.push_back(item);eq.push_back(false);}}else{ex.push_back(ar[ai]);eq.push_back(ai<aq.size()&&aq[ai]);}}ar.swap(ex);aq.swap(eq);}if(!aok||(!fn->variadic_param.empty()?ar.size()<fn->params.size():ar.size()!=fn->params.size())){error="lambda argument count mismatch";--callable_call_depth_;return false;}
                            std::vector<nift::RuntimeValue> av;for(size_t ai=0;ai<ar.size();++ai){nift::RuntimeValue v;if(ai<aq.size()&&aq[ai])v=nift::RuntimeValue(ar[ai]);else if(ar[ai].rfind("\x1fnift:spread:",0)==0)v=spread_args[static_cast<std::size_t>(std::stoull(ar[ai].substr(13)))];else if(!eval(ar[ai],v,depth+1)){--callable_call_depth_;return false;}av.push_back(std::move(v));}
                            if(fn->async){for(std::size_t ai=0;ai<ar.size();++ai){const std::string an=trim_copy(ar[ai]);if(valid_binding_identifier(an)){if(auto* ab=find_binding(an)){ab->sync();if(ab->value&&ab->value->is_string()&&ab->value->string.rfind("\x1fnift:atomic:",0)==0)av[ai]=*ab->value;}}}--callable_call_depth_;return spawn_future(*cb->value,av,out);}
                            std::vector<VariableBinding> parameter_bindings;parameter_bindings.reserve(fn->params.size());for(size_t ai=0;ai<fn->params.size();++ai)parameter_bindings.push_back(bind_call_param(ar[ai],ai<aq.size()&&aq[ai],av[ai]));
                            auto lexical_env=enter_lexical_environment(fn->module_env);
                            push_variable_scope();auto& sc=variable_scopes_.back();for(const auto& kv:fn->captures)sc[kv.first]=kv.second;for(size_t ai=0;ai<fn->params.size();++ai)sc[fn->params[ai]]=std::move(parameter_bindings[ai]);if(!fn->variadic_param.empty()){nift::RuntimeValue rest=nift::RuntimeValue::make_array();for(size_t ai=fn->params.size();ai<av.size();++ai)rest.array.push_back(std::move(av[ai]));auto sp=std::make_shared<nift::RuntimeValue>(std::move(rest));sc[fn->variadic_param]=VariableBinding{sp,nift_binding_type(*sp),true,false};}
                            bool okcall=true;
                            if(fn->block){std::string bt=trim_copy(fn->body);const bool rendered=!bt.empty()&&bt.front()=='<';if(rendered){auto nested=parse(fn->body,fn->source_path,1);if(!nested.ok){error=nested.error.message;okcall=false;}else out=nift::RuntimeValue(nested.output);}else{++function_call_depth_;auto nested=execute_native_program(fn->body,fn->source_path,1);--function_call_depth_;if(!nested.ok){error=nested.error.message;okcall=false;}else if(pending_control_.kind==ControlFlow::Return){consume_return(out);}else out=nift::RuntimeValue(nullptr);}}
                            else { okcall=eval(fn->body,out,depth+1); if(!okcall&&error.rfind("callable recursion depth exceeded",0)!=0) error="lambda body error: "+error; VariableBinding lr; if(try_location_ref(fn->body,lr)&&lr.is_location_ref()&&lr.value){last_call_return_loc_root_=lr.ref_root_slot;last_call_return_loc_path_=lr.ref_path;} }
                            pop_variable_scope();leave_lexical_environment(std::move(lexical_env));--callable_call_depth_;return okcall;
                        }
                    }
                }
                if(ci==callables_.end()&&!module_callee&&!receiver_stack_.empty()){auto sd=structs_.find(receiver_stack_.back()->type_name);if(sd!=structs_.end()){auto mi=sd->second.methods.find(call_name);if(mi!=sd->second.methods.end()&&!mi->second.constructor){std::vector<nift::RuntimeValue> av;std::vector<std::string> ar;if(!method_arguments(mi->second,av,ar))return false;return invoke_struct_method(receiver_stack_.back(),mi->second,av,ar,out,error);}}}
                // An active module's callable is the lexical binding. Caller globals
                // remain a fallback, and either still precedes an implicit sibling.
                const Callable* callee = module_callee ? module_callee : ((ci != callables_.end()) ? &ci->second : nullptr);
                if (callee) {
                    const auto callee_env = module_callee ? module_callee_env : callee->module_env;
                    if (callable_call_depth_ >= kMaxCallableDepth) { error = "callable recursion depth exceeded: " + call_name; return false; }
                    std::vector<nift::RuntimeValue> spread_args;bool args_ok=false; std::vector<bool> quoted_args; auto args=parse_parameters(text.substr(lp+1,text.size()-lp-2),args_ok,&quoted_args); if(args_ok){std::vector<std::string> ex;std::vector<bool> eq;for(size_t ai=0;ai<args.size();++ai){if(!(ai<quoted_args.size()&&quoted_args[ai])&&trim_copy(args[ai]).rfind("...",0)==0){nift::RuntimeValue sv;if(!eval(trim_copy(args[ai]).substr(3),sv,depth+1))return false;if(!sv.is_array()){error="spread value must be an array";return false;}for(const auto& item:sv.array){ex.push_back("\x1fnift:spread:"+std::to_string(spread_args.size()));spread_args.push_back(item);eq.push_back(false);}}else{ex.push_back(args[ai]);eq.push_back(ai<quoted_args.size()&&quoted_args[ai]);}}args.swap(ex);quoted_args.swap(eq);}if(!args_ok||(!callee->variadic_param.empty()?args.size()<callee->params.size():args.size()!=callee->params.size())){error="callable argument count mismatch: "+call_name;return false;}
                    std::vector<nift::RuntimeValue> values; for(std::size_t ai=0;ai<args.size();++ai){nift::RuntimeValue v;if(ai<quoted_args.size()&&quoted_args[ai])v=nift::RuntimeValue(args[ai]);else if(args[ai].rfind("\x1fnift:spread:",0)==0)v=spread_args[static_cast<std::size_t>(std::stoull(args[ai].substr(13)))];else if(!eval(args[ai],v,depth+1))return false;values.push_back(std::move(v));}
                    if(callee->async){if(module_callee){error="module-private async callables are unsupported";return false;}for(std::size_t ai=0;ai<args.size();++ai){const std::string an=trim_copy(args[ai]);if(valid_binding_identifier(an)){if(auto* ab=find_binding(an)){ab->sync();if(ab->value&&ab->value->is_string()&&ab->value->string.rfind("\x1fnift:atomic:",0)==0)values[ai]=*ab->value;}}}nift::RuntimeValue cb(named_callable_tag(call_name,callee->module_env));return spawn_future(cb,values,out);}
                    ++callable_call_depth_;
                    const int caller_loop_depth = loop_depth_;
                    loop_depth_ = 0;
                    std::vector<VariableBinding> parameter_bindings;parameter_bindings.reserve(callee->params.size());for(std::size_t ai=0;ai<callee->params.size();++ai)parameter_bindings.push_back(bind_call_param(args[ai],ai<quoted_args.size()&&quoted_args[ai],values[ai]));
                    auto lexical_env=enter_lexical_environment(callee_env);
                    push_variable_scope(); auto& scope=variable_scopes_.back(); for(std::size_t ai=0;ai<callee->params.size();++ai)scope.emplace(callee->params[ai],std::move(parameter_bindings[ai]));if(!callee->variadic_param.empty()){nift::RuntimeValue rest=nift::RuntimeValue::make_array();for(std::size_t ai=callee->params.size();ai<values.size();++ai)rest.array.push_back(std::move(values[ai]));auto sp=std::make_shared<nift::RuntimeValue>(std::move(rest));scope.emplace(callee->variadic_param,VariableBinding{sp,nift_binding_type(*sp),true,false});}
                    const bool saved_mutation = last_expression_mutation_;
                    bool call_ok = true; std::string call_error;
                    if (callee->fragment) {
                        const bool saved_fragment = in_fragment_body_; in_fragment_body_ = true;
                        auto nested = parse(callee->body, callee->source_path, 1);
                        in_fragment_body_ = saved_fragment;
                        if (!nested.ok) { call_ok = false; call_error = nested.error.message; }
                        else { out = nift::RuntimeValue(nested.output); if (pending_control_.kind == ControlFlow::Return) pending_control_ = {}; }
                    } else {
                        ++function_call_depth_;
                        if (pending_control_.kind != ControlFlow::None) { pending_control_ = {}; }
                        auto body_result = execute_native_program(callee->body, callee->source_path, 1);
                        --function_call_depth_;
                        if (!body_result.ok) { call_ok = false; call_error = body_result.error.message; }
                        else if (pending_control_.kind == ControlFlow::None) { out = nift::RuntimeValue(nullptr); }
                        else consume_return(out);
                    }
                    last_expression_mutation_ = saved_mutation;
                    pop_variable_scope();
                    leave_lexical_environment(std::move(lexical_env));
                    loop_depth_ = caller_loop_depth;
                    --callable_call_depth_;
                    if (!call_ok) { error = call_error; return false; }
                    return true;
                }
                if(host_.has_host_callable(call_name)){bool aok=false;std::vector<bool> aq;auto ar=parse_parameters(text.substr(lp+1,text.size()-lp-2),aok,&aq);if(!aok){error="malformed host callable arguments";return false;}std::vector<nift::RuntimeValue> av;for(size_t ai=0;ai<ar.size();++ai){nift::RuntimeValue v;if(ai<aq.size()&&aq[ai])v=nift::RuntimeValue(ar[ai]);else if(!eval(ar[ai],v,depth+1))return false;if(contains_timer_resource(v)){error="host callable '"+call_name+"' cannot receive a timer value";return false;}av.push_back(std::move(v));}return call_runtime_host(host_, call_name, av, out, error);}
                if (call_name != "inject" && call_name != "validate" && call_name != "copy" && call_name != "deepcopy") { error = "undefined callable: " + call_name; return false; }
            }
        }

        if (text.rfind("copy(",0)==0 && text.back()==')') {
            // v4.4 ownership model: assignment/extraction is the sharing
            // operation; copy() means a recursively independent value.  Keep
            // deepcopy() as a compatibility spelling for now and route copy()
            // through the same certified recursive clone implementation.
            return eval("deepcopy("+text.substr(5,text.size()-6)+")",out,depth+1);
        }

        if (text.rfind("deepcopy(",0)==0 && text.back()==')') {
            nift::RuntimeValue source; if(!eval(text.substr(9,text.size()-10),source,depth+1))return false;
            std::unordered_map<std::string,std::string> seen;
            std::function<bool(const nift::RuntimeValue&,nift::RuntimeValue&)> clone;
            clone=[&](const nift::RuntimeValue& in,nift::RuntimeValue& dst)->bool{
                if(in.is_string()&&in.string.rfind("\x1fnift:collection:",0)==0){auto it=collection_instances_.find(in.string.substr(17));if(it==collection_instances_.end()){error="deepcopy: invalid collection";return false;}auto nc=std::make_shared<CollectionInstance>();nc->kind=it->second->kind;for(const auto& v:it->second->values){nift::RuntimeValue cv;if(!clone(v,cv))return false;nc->values.push_back(std::move(cv));if(nc->kind==CollectionKind::Set||nc->kind==CollectionKind::SortedSet){std::string k=runtime_scalar_key(cv);if(!k.empty())nc->scalar_keys.insert(k);if(cv.type==nift::RuntimeType::StrNumber)nc->has_huge_int=true;}}for(const auto& e:it->second->entries){nift::RuntimeValue ck,cv;if(!clone(e.first,ck)||!clone(e.second,cv))return false;nc->entries.push_back({std::move(ck),std::move(cv)});if(nc->kind==CollectionKind::Map||nc->kind==CollectionKind::SortedMap){std::string kk=runtime_scalar_key(nc->entries.back().first);if(!kk.empty())nc->scalar_keys.insert(kk);if(nc->entries.back().first.type==nift::RuntimeType::StrNumber)nc->has_huge_int=true;}}const std::string id=std::to_string(next_collection_instance_id_++);collection_instances_[id]=nc;dst=nift::RuntimeValue(std::string("\x1fnift:collection:")+id);return true;}
                if(in.is_string()&&in.string.rfind("\x1fnift:struct:",0)==0){const std::string old=in.string.substr(13);auto sit=seen.find(old);if(sit!=seen.end()){dst=nift::RuntimeValue(std::string("\x1fnift:struct:")+sit->second);return true;}auto it=struct_instances_.find(old);if(it==struct_instances_.end()){error="deepcopy: invalid struct instance";return false;}auto ni=std::make_shared<StructInstance>();ni->type_name=it->second->type_name;const std::string id=std::to_string(next_struct_instance_id_++);seen[old]=id;struct_instances_[id]=ni;for(const auto& f:it->second->fields){nift::RuntimeValue cv;if(!clone(*f.second.value,cv))return false;auto sp=std::make_shared<nift::RuntimeValue>(std::move(cv));ni->fields.emplace(f.first,VariableBinding{sp,f.second.type,f.second.mutable_binding,f.second.deep_readonly});}dst=nift::RuntimeValue(std::string("\x1fnift:struct:")+id);return true;}
                dst=in;if(in.is_array()){dst.array.clear();for(const auto& v:in.array){nift::RuntimeValue cv;if(!clone(v,cv))return false;dst.array.push_back(std::move(cv));}}else if(in.is_object()){dst.object.clear();for(const auto& e:in.object){nift::RuntimeValue cv;if(!clone(e.second,cv))return false;dst.object.emplace_back(e.first,std::move(cv));}}return true;
            };
            return clone(source,out);
        }

        if (text.rfind("validate(", 0) == 0 && text.back() == ')') {
            bool ok_params = false; auto args = parse_parameters(text.substr(9, text.size() - 10), ok_params);
            if (!ok_params || args.size() != 2) { error = "validate: expected schema and value"; return false; }
            nift::RuntimeValue schema, candidate; if (!eval(args[0], schema, depth + 1) || !eval(args[1], candidate, depth + 1)) return false;
            json::Document schema_document, candidate_document;
            std::string conversion_error;
            if (!nift::runtime_to_json(schema, schema_document, conversion_error) ||
                !nift::runtime_to_json(candidate, candidate_document, conversion_error)) {
                error = "validate: " + conversion_error;
                return false;
            }
            std::string validation_error; if (!jsonschema::validate(candidate_document, schema_document, validation_error)) { error = "validate: " + validation_error; return false; }
            out = std::move(candidate); return true;
        }

        if (text.rfind("inject(", 0) == 0 && text.back() == ')') {
            std::string arg = trim_copy(text.substr(7, text.size() - 8));
            if (arg.size() >= 2 && ((arg.front() == '"' && arg.back() == '"') || (arg.front() == '\'' && arg.back() == '\''))) arg = arg.substr(1, arg.size() - 2);
            else { nift::RuntimeValue av; if (!eval(arg, av, depth + 1) || !av.is_string()) { error = "inject: expected string path"; return false; } arg = av.string; }
            fs::path path = fs::absolute(host_.root() / arg).lexically_normal();
            if (!standalone_script_host_ && !host_.root().empty() && !filesystem::path_within(fs::absolute(host_.root()).lexically_normal(), path)) { error = "inject: path must stay inside the Nift project"; return false; }
            if (!nift_fs_root_allowed(path,standalone_script_host_,error)) { error = "inject: " + error; return false; }
            if (std::find(input_stack_.begin(), input_stack_.end(), path) != input_stack_.end()) { error = "inject: source cycle through " + path.generic_string(); return false; }
            auto injected = filesystem::read_file_checked(path); if (!injected) { error = "inject: source is not readable"; return false; }
            result_.dependencies.insert(host_.relative(path)); input_stack_.push_back(path);
            // JSON is the overwhelmingly common structured-data inject case.  Do
            // not feed a multi-megabyte JSON document through the generic Nift
            // expression classifier: that repeatedly scans the complete source
            // for operators before eventually reaching the JSON literal parser.
            // Parse valid JSON directly into the runtime Document and retain the
            // expression evaluator as the compatibility path for .expr files and
            // Nift expression-valued object/array literals.
            nift::RuntimeValue injected_value; std::string direct_json_error;
            if (parse_runtime_json(*injected, injected_value, direct_json_error) && !injected_value.is_string()) {
                // A top-level JSON string must stay on the expression path: the
                // legacy scalar-literal evaluator interpolates $[...] inside it
                // (inject of a bare-string .expr), which strict JSON parsing
                // would silently drop.
                input_stack_.pop_back(); out = std::move(injected_value); return true;
            }
            push_variable_scope();
            const bool saved_mutation = last_expression_mutation_;
            std::string nested_error; const bool ok = evaluate_expression(*injected, injected_value, nested_error);
            last_expression_mutation_ = depth == 0 ? last_expression_mutation_ : saved_mutation;
            pop_variable_scope(); input_stack_.pop_back(); if (!ok) { error = "inject: " + nested_error; return false; } out = std::move(injected_value); return true;
        }

        // CP101 expression-valued array literals. Evaluate elements left-to-right,
        // exactly once. Empty arrays and ordinary JSON-compatible arrays remain a subset.
        if (text.size()>=2 && text.front()=='[' && text.back()==']') {
            std::size_t close=0;
            if(find_balanced(text,0,'[',']',close)&&close==text.size()-1){
                const std::string inner=trim_copy(text.substr(1,text.size()-2));
                out=nift::RuntimeValue::make_array();
                if(inner.empty())return true;
                bool ok=false;std::vector<bool> quoted;auto elements=parse_parameters(inner,ok,&quoted);
                if(!ok){error="array literal: malformed elements";return false;}
                for(std::size_t i=0;i<elements.size();++i){nift::RuntimeValue v;if(i<quoted.size()&&quoted[i])v=nift::RuntimeValue(elements[i]);else if(!eval(elements[i],v,depth+1))return false;out.array.push_back(std::move(v));}
                return true;
            }
        }

        // Expression-valued object literals, mirroring array literals. Quoted
        // string values are literal strings (JSON escape conventions); every
        // other value is a Nift expression evaluated left-to-right exactly once,
        // preserving its runtime type (enums stay symbolic). Pure JSON objects
        // keep the JSON fast path (strict double-quoted keys, duplicate-key
        // rejection, big integers, scientific notation, full string escaping).
        if (text.size()>=2 && text.front()=='{' && text.back()=='}') {
            std::size_t close=0;
            if(find_balanced(text,0,'{','}',close)&&close==text.size()-1){
                std::string json_error;
                if(parse_runtime_json(text,out,json_error))return true;
                const std::string inner=trim_copy(text.substr(1,text.size()-2));
                out=nift::RuntimeValue::make_object();
                if(inner.empty())return true;
                if(inner.back()==','){error="object literal: trailing comma";return false;}
                bool ok=false;auto members=parse_parameters(inner,ok);
                if(!ok){error="object literal: malformed members";return false;}
                for(auto& raw : members){
                    raw=trim_copy(raw);
                    if(raw.empty()){error="object literal: empty member";return false;}
                    std::size_t colon=std::string::npos;bool q=false;char qc=0;int pa=0,br=0,bc=0;
                    for(std::size_t z=0;z<raw.size();++z){char c=raw[z];if(q){if(c=='\\'&&z+1<raw.size())++z;else if(c==qc)q=false;continue;}if(c=='\''||c=='"'){q=true;qc=c;continue;}if(c=='(')++pa;else if(c==')')--pa;else if(c=='[')++br;else if(c==']')--br;else if(c=='{')++bc;else if(c=='}')--bc;if(c==':'&&!q&&!pa&&!br&&!bc){colon=z;break;}}
                    if(colon==std::string::npos){error="object literal: expected 'key: value' member";return false;}
                    std::string key=trim_copy(raw.substr(0,colon));std::string value=trim_copy(raw.substr(colon+1));
                    if(key.size()<2||key.front()!='"'||key.back()!='"'){error="object literal: keys must be double-quoted strings";return false;}
                    key=unescape_parameter_string(key.substr(1,key.size()-2));
                    if(out.has(key)){error="object literal: duplicate object key '"+key+"'";return false;}
                    nift::RuntimeValue v;
                    if(value.size()>=2&&((value.front()=='"'&&value.back()=='"')||(value.front()=='\''&&value.back()=='\''))){v=nift::RuntimeValue(unescape_parameter_string(value.substr(1,value.size()-2)));}
                    else{if(value.empty()){error="object literal: missing value for key '"+key+"'";return false;}if(!eval(value,v,depth+1))return false;}
                    out[key]=std::move(v);
                }
                return true;
            }
        }

        // Declarations/assignments must be parsed before direct JSON-path lookup;
        // structured RHS values can otherwise make the whole mutation look like an object expression.
        const bool whole_quoted=text.size()>=2&&((text.front()=='"'&&text.back()=='"')||(text.front()=='\''&&text.back()=='\''));
        // Assignment operators only count at bracket/paren/quote depth zero; a
        // `=`/`:=` inside a quoted argument (e.g. a SQL string) must not turn a
        // postfix-composition expression such as fn("a = b").member into a
        // mutation candidate.
        auto top_level_assignment=[&](const std::string& s)->bool{bool q1=false,q2=false,esc=false;int par=0,br=0,bc=0;for(size_t i=0;i<s.size();++i){char c=s[i];if(esc){esc=false;continue;}if(c=='\\'&&(q1||q2)){esc=true;continue;}if(c=='\''&&!q2){q1=!q1;continue;}if(c=='"'&&!q1){q2=!q2;continue;}if(q1||q2)continue;if(c=='(')++par;else if(c==')')--par;else if(c=='[')++br;else if(c==']')--br;else if(c=='{')++bc;else if(c=='}')--bc;else if(c==':'&&i+1<s.size()&&s[i+1]=='='&&par==0&&br==0&&bc==0)return true;else if(c=='='&&par==0&&br==0&&bc==0){if(i+1<s.size()&&(s[i+1]=='='||s[i+1]=='>')){++i;continue;}if(i>0&&(s[i-1]=='!'||s[i-1]=='<'||s[i-1]=='>'))continue;return true;}}return false;};
        const bool mutation_candidate = !whole_quoted && top_level_assignment(text);
        if (!mutation_candidate) {
            // General postfix composition: any value-producing expression can
            // feed the next postfix operator. final_postfix already chains
            // `.method(...)` calls by recursively evaluating the receiver; this
            // resolver extends the same composition to trailing `.member` and
            // `[index]` postfixes on call results (and parenthesized primaries),
            // so e.g. posts.filter(...).first().title composes naturally instead
            // of stopping at an artificial parser boundary.
            auto compose_postfix = [&](const std::string& raw, nift::RuntimeValue& out) -> bool {
                const std::string text = trim_copy(raw);
                if (text.size() < 2) return false;
                char last = text.back();
                if (last == '"' || last == '\'') return false;
                std::string receiver;
                int kind = 0; // 1=member, 2=index, 3=paren-unwrap
                std::string operand;
                if (last == ']') {
                    int dep = 0;
                    std::size_t k = text.size();
                    while (k-- > 0) {
                        const char c = text[k];
                        if (c == ']') ++dep;
                        else if (c == '[') { --dep; if (dep == 0) { receiver = text.substr(0, k); operand = text.substr(k); kind = 2; break; } }
                    }
                    if (kind != 2) return false;
                } else if (last == ')') {
                    int dep = 0;
                    std::size_t k = text.size();
                    while (k-- > 0) {
                        const char c = text[k];
                        if (c == ')') ++dep;
                        else if (c == '(') { --dep; if (dep == 0) {
                            if (k == 0) { receiver = text.substr(1, text.size() - 2); kind = 3; }
                            break;
                        } }
                    }
                    if (kind != 3) return false;
                } else if (std::isalnum(static_cast<unsigned char>(last)) || last == '_') {
                    std::size_t id_start = text.size();
                    while (id_start > 0 && (std::isalnum(static_cast<unsigned char>(text[id_start - 1])) || text[id_start - 1] == '_')) --id_start;
                    if (id_start == 0 || id_start == text.size() || text[id_start - 1] != '.') return false;
                    receiver = text.substr(0, id_start - 1);
                    operand = text.substr(id_start);
                    kind = 1;
                } else {
                    return false;
                }
                if (receiver.empty()) return false;
                // Only engage for receivers that require evaluation (method
                // calls or parenthesized/indexed expressions); pure binding
                // paths stay on the efficient resolve_direct path.
                const bool literal_receiver = !receiver.empty() && (receiver[0] == '[' || receiver[0] == '{');
                // Engage for receivers that require evaluation: method calls,
                // parenthesized/indexed expressions, or structured literals
                // (e.g. [1,2,3][0], {"a":1}["a"]). A receiver that contains a
                // TOP-LEVEL binary operator (e.g. {"a":x}.a + "|" + {"a":x})
                // must fall through to the operator machinery so the trailing
                // member binds only to the last value, not the whole compound.
                if (kind != 3 && receiver.find('(') == std::string::npos && !literal_receiver) return false;
                if (kind != 3) {
                    bool toplevel_operator = false;
                    bool q=false; char qc=0; int pa=0, br=0, bc=0;
                    for (std::size_t z = 0; z < receiver.size(); ++z) {
                        const char c = receiver[z];
                        if (q) { if (c=='\\' && z+1<receiver.size()) ++z; else if (c==qc) q=false; continue; }
                        if (c=='\'' || c=='"') { q=true; qc=c; continue; }
                        if (c=='(') { ++pa; continue; } if (c==')') { if (pa) --pa; continue; }
                        if (c=='[') { ++br; continue; } if (c==']') { if (br) --br; continue; }
                        if (c=='{') { ++bc; continue; } if (c=='}') { if (bc) --bc; continue; }
                        if (pa || br || bc) continue;
                        if (c=='?') { if (z+1<receiver.size() && (receiver[z+1]=='?'||receiver[z+1]=='.'||receiver[z+1]=='[')) continue; toplevel_operator = true; break; }
                        if (c=='>' && z+1<receiver.size() && receiver[z+1]=='=') continue; // =>
                        if (c=='+'||c=='-'||c=='*'||c=='/'||c=='%'||c=='<'||c=='>'||c=='='||c=='&'||c=='|'||c==':'||c=='!') { toplevel_operator = true; break; }
                    }
                    if (toplevel_operator) return false;
                }
                nift::RuntimeValue base;
                if (!eval(receiver, base, depth + 1)) return false;
                if (kind == 1) {
                    if (base.is_string() && base.string.rfind("\x1fnift:page:", 0) == 0) {
                        result_.dependencies.insert(host_.relative(host_.root() / ".nift/hierarchy.fingerprint"));
                        nift::RuntimeValue resolved; std::string perr;
                        if (!host_.resolve_page_member(base.string.substr(11), operand, resolved, perr)) {
                            error = perr.empty() ? ("page has no member: " + operand) : perr;
                            return false;
                        }
                        out = std::move(resolved);
                        return true;
                    }
                    if (!base.is_object()) {
                        error = "cannot access member '" + operand + "' because the current JSON value is not an object";
                        return false;
                    }
                    if (!base.has(operand)) {
                        error = "JSON value '" + receiver + "' has no member '" + operand + "'";
                        return false;
                    }
                    out = base[operand];
                    return true;
                } else if (kind == 2) {
                    std::string token = trim_copy(operand.substr(1, operand.size() - 2));
                    if (base.is_array()) {
                        std::size_t index = 0;
                        if (!token.empty() && std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isdigit(c); })) {
                            try { index = static_cast<std::size_t>(std::stoull(token)); }
                            catch (...) { error = "JSON array index is out of range in '" + text + "'"; return false; }
                        } else {
                            // Computed index from a binding or resolvable
                            // expression (e.g. a[i], a[i + 1]).
                            nift::RuntimeValue computed;
                            if (!resolve_direct(token, computed) ||
                                !nift::runtime_number_to_size(computed, index)) {
                                error = "JSON array indices must be non-negative integers in '" + text + "'";
                                return false;
                            }
                        }
                        if (index >= base.array.size()) {
                            error = "JSON array index " + std::to_string(index) + " is out of range in '" + text + "'";
                            return false;
                        }
                        out = base.array[index];
                        return true;
                    }
                    if (base.is_bytes()) {
                        nift::RuntimeValue computed;std::size_t index=0;
                        if (!eval(token,computed,depth+1) || !nift::runtime_number_to_size(computed,index)) { error="bytes indices must be non-negative integers in '"+text+"'"; return false; }
                        if (!base.bytes || index>=base.bytes->size()) { error="bytes index "+std::to_string(index)+" is out of range in '"+text+"'"; return false; }
                        out=nift::RuntimeValue(static_cast<int>((*base.bytes)[index]));return true;
                    }
                    if (base.is_object()) {
                        std::string key;
                        if (token.size() >= 2 && (token.front() == '"' || token.front() == '\'')) key = token.substr(1, token.size() - 2);
                        else {
                            VariableBinding* kb = nullptr;
                            for (auto scope = variable_scopes_.rbegin(); scope != variable_scopes_.rend(); ++scope) {
                                auto it = scope->find(token);
                                if (it != scope->end()) { kb = &it->second; break; }
                            }
                            if (kb && kb->value && kb->value->is_string()) key = kb->value->string;
                            else {
                                // Computed key from a resolvable expression
                                // (e.g. info[page.name], obj[tag.to_lower()]).
                                nift::RuntimeValue computed;
                                if (!resolve_direct(token, computed) || !computed.is_string()) {
                                    error = "JSON object index must be a quoted string, string binding, or string expression in '" + text + "'";
                                    return false;
                                }
                                key = computed.string;
                            }
                        }
                        if (!base.has(key)) { error = "JSON object has no key '" + key + "'"; return false; }
                        out = base[key];
                        return true;
                    }
                    error = "cannot index JSON value in '" + text + "'";
                    return false;
                }
                out = base;
                return true;
            };
            // Ternary condition (value semantics) is the outermost operator. A '?'
            // that begins '??', '?.' or '?[' is an optionality marker, not a
            // ternary; nested ternaries are tracked so the matching ':' is found at
            // the correct depth.
            {
                bool quoted=false; char quote=0; int parens=0, brackets=0, braces=0;
                std::size_t question=std::string::npos; int nested=0;
                for (std::size_t z=0; z<text.size(); ++z) {
                    const char c=text[z];
                    if (quoted) { if (c=='\\' && z+1<text.size()) ++z; else if (c==quote) quoted=false; continue; }
                    if (c=='\'' || c=='"') { quoted=true; quote=c; continue; }
                    if (c=='(') { ++parens; continue; } if (c==')') { if (parens) --parens; continue; }
                    if (c=='[') { ++brackets; continue; } if (c==']') { if (brackets) --brackets; continue; }
                    if (c=='{') { ++braces; continue; } if (c=='}') { if (braces) --braces; continue; }
                    if (parens || brackets || braces) continue;
                    if (c=='?') {
                        if (z+1<text.size() && (text[z+1]=='?' || text[z+1]=='.' || text[z+1]=='[')) continue;
                        if (z>0 && text[z-1]=='?') continue;
                        if (question==std::string::npos) question=z; else ++nested;
                        continue;
                    }
                    if (c==':' && question!=std::string::npos) {
                        if (nested>0) { --nested; continue; }
                        nift::RuntimeValue cond;
                        if(!eval(text.substr(0,question),cond,depth+1)) return false;
                        const std::string selected = truthy_value(cond) ? text.substr(question+1,z-question-1) : text.substr(z+1);
                        return eval(selected,out,depth+1);
                    }
                }
                if (question!=std::string::npos) { error="ternary expression has '?' without a matching ':'"; return false; }
            }
            // CP206: null coalescing. Null is Nift's sole public absence value;
            // RHS evaluation is deliberately lazy. Handled before safe access so
            // (a?.b ?? c) resolves to c when a is null.
            if (const auto p=find_top_level_op("??"); p!=std::string::npos) {
                nift::RuntimeValue left;if(!eval(text.substr(0,p),left,depth+1))return false;
                if(!left.is_null()){out=std::move(left);return true;}
                return eval(text.substr(p+2),out,depth+1);
            }
            // CP207: safe access is null propagation only. Evaluate the receiver;
            // when non-null, retry the same expression with the first safe marker
            // converted to ordinary access so normal missing/type errors survive.
            {
                bool quoted=false;char quote=0;int pa=0,br=0,bc=0;std::size_t sp=std::string::npos;
                for(std::size_t z=0;z+1<text.size();++z){char c=text[z];if(quoted){if(c=='\\'&&z+1<text.size())++z;else if(c==quote)quoted=false;continue;}if(c=='\''||c=='"'){quoted=true;quote=c;continue;}if(c=='(')++pa;else if(c==')'&&pa)--pa;else if(c=='[')++br;else if(c==']'&&br)--br;else if(c=='{')++bc;else if(c=='}'&&bc)--bc;if(!pa&&!br&&!bc&&(text.compare(z,2,"?.")==0||text.compare(z,2,"?[")==0)){sp=z;break;}}
                if(sp!=std::string::npos){nift::RuntimeValue recv;if(!eval(text.substr(0,sp),recv,depth+1))return false;if(recv.is_null()){out=nift::RuntimeValue(nullptr);return true;}std::string ordinary=text;ordinary.erase(sp,1);return eval(ordinary,out,depth+1);}
            }
            if (compose_postfix(text, out)) return true;
            if (!error.empty()) return false;
            if (resolve_direct(text,out)) return true;
            if (!error.empty()) return false;
        }

        auto find_top_level_assignment = [&]() -> std::size_t {
            bool quoted=false; char quote=0; int parens=0, brackets=0, braces=0;
            for (std::size_t i=0; i<text.size(); ++i) {
                const char c=text[i];
                if (quoted) { if (c=='\\' && i+1<text.size()) ++i; else if (c==quote) quoted=false; continue; }
                if (c=='\'' || c=='"') { quoted=true; quote=c; continue; }
                if (c=='(') { ++parens; continue; } if (c==')') { if (parens) --parens; continue; }
                if (c=='[') { ++brackets; continue; } if (c==']') { if (brackets) --brackets; continue; }
                if (c=='{') { ++braces; continue; } if (c=='}') { if (braces) --braces; continue; }
                if (parens || brackets || braces || c!='=') continue;
                const char prev = i ? text[i-1] : '\0';
                const char next = i+1<text.size() ? text[i+1] : '\0';
                if (prev==':' || prev=='=' || prev=='!' || prev=='<' || prev=='>' || next=='=' || next=='>') continue;
                return i;
            }
            return std::string::npos;
        };

        auto atomic_scalar = [&](const nift::RuntimeValue& in, nift::RuntimeValue& scalar)->bool {
            if(!(in.is_string()&&in.string.rfind("\x1fnift:atomic:",0)==0)){scalar=in;return true;}
            auto it=atomic_instances_.find(in.string.substr(13));if(it==atomic_instances_.end()){error="atomic: invalid handle";return false;}
            if(it->second->kind==AtomicInstance::Kind::Bool)scalar=nift::RuntimeValue(it->second->bool_value.load());
            else{auto v=it->second->int_value.load();scalar=nift::RuntimeValue(static_cast<double>(v));if(v>9007199254740992LL||v<-9007199254740992LL){scalar.type=nift::RuntimeType::StrNumber;scalar.string=std::to_string(v);}}return true;
        };

        auto numeric_binary = [&](const nift::RuntimeValue& left_in, const nift::RuntimeValue& right_in, char op, nift::RuntimeValue& result)->bool {
            nift::RuntimeValue left,right;if(!atomic_scalar(left_in,left)||!atomic_scalar(right_in,right))return false;
            auto as_i64=[](const nift::RuntimeValue& d,std::int64_t& v)->bool{
                return nift::runtime_number_to_i64(d,v);};
            std::int64_t a=0,b=0; if(as_i64(left,a)&&as_i64(right,b)&&op!='/'){std::int64_t r=0;bool overflow=false;
                if(op=='+')overflow=__builtin_add_overflow(a,b,&r);else if(op=='-')overflow=__builtin_sub_overflow(a,b,&r);else if(op=='*')overflow=__builtin_mul_overflow(a,b,&r);else if(op=='%'){if(b==0){error="modulo by zero";return false;}if(a==std::numeric_limits<std::int64_t>::min()&&b==-1)r=0;else r=a%b;}else return false;
                if(overflow){error="signed 64-bit integer overflow";return false;}result=nift::RuntimeValue(static_cast<double>(r));if(r>9007199254740992LL||r<-9007199254740992LL){result.type=nift::RuntimeType::StrNumber;result.string=std::to_string(r);}return true;}
            if(!left.is_number()||!right.is_number()){error="arithmetic operators require numeric operands";return false;}double r=0;if(op=='+')r=left.num+right.num;else if(op=='-')r=left.num-right.num;else if(op=='*')r=left.num*right.num;else if(op=='/'){if(nift::runtime_number_is_zero(right)){error="division by zero";return false;}r=left.num/right.num;}else if(op=='%'){if(nift::runtime_number_is_zero(right)){error="modulo by zero";return false;}if(!nift::runtime_number_is_integer(left)||!nift::runtime_number_is_integer(right)){error="modulo requires integer-valued operands";return false;}r=std::fmod(left.num,right.num);}if(!std::isfinite(r)){error="arithmetic result is not finite";return false;}result=nift::RuntimeValue(r);return true;
        };

        // Compound assignment is a true mutation expression. Current assignable
        // paths are identifiers and struct member paths; both are side-effect-free
        // to resolve, so the target is read once and written once. For a plain
        // identifier target, assign directly instead of rebuilding "name = value"
        // and re-parsing it through the evaluator (a full string round-trip on
        // every += in a hot loop).
        auto assign_identifier = [&](const std::string& name, nift::RuntimeValue&& value) -> bool {
            VariableBinding* tb = find_binding(name);
            if (!tb) { error = "assignment to undefined binding: " + name; return false; }
            if (!tb->mutable_binding) { error = "cannot assign to const binding: " + name; return false; }
            const int at = nift_binding_type(value);
            if (!nift_type_assignable(at, tb->type)) {
                error = "cannot assign " + std::string(nift_binding_type_name(at)) +
                        " to " + std::string(nift_binding_type_name(tb->type)) + " binding '" + name + "'";
                return false;
            }
            auto rebound = std::make_shared<nift::RuntimeValue>(std::move(value));
            tb->rebind(rebound);
            out = *rebound;
            return true;
        };
        for(const std::string cop:{"+=","-=","*=","/=","%=","&=","|=","^="}){const auto p=find_top_level_op(cop);if(p!=std::string::npos){const std::string target=trim_copy(text.substr(0,p));
            {bool decl=false;bool iq=false;char iqc=0;int ip=0,ib=0;for(std::size_t k=0;k+1<target.size();++k){char cc=target[k];if(iq){if(cc=='\\')++k;else if(cc==iqc)iq=false;continue;}if(cc=='\''||cc=='"'){iq=true;iqc=cc;continue;}if(cc=='(')++ip;else if(cc==')')--ip;else if(cc=='[')++ib;else if(cc==']')--ib;if(ip||ib)continue;if(target.compare(k,2,":=")==0||target.compare(k,2,"=>")==0){decl=true;break;}}if(decl)break;}
            nift::RuntimeValue oldv,rhs,next;if(!eval(target,oldv,depth+1)||!eval(text.substr(p+2),rhs,depth+1))return false;
            if(target.find_first_of(".([")==std::string::npos&&oldv.is_string()&&oldv.string.rfind("\x1fnift:atomic:",0)==0){auto ai=atomic_instances_.find(oldv.string.substr(13));if(ai==atomic_instances_.end()){error="atomic: invalid handle";return false;}if(ai->second->kind!=AtomicInstance::Kind::Int){error=cop+": only valid for atomic<int>";return false;}std::int64_t v=0;if(!exact_i64(rhs,v)){error=cop+": atomic<int> requires signed 64-bit integer";return false;}auto st=ai->second;std::int64_t nv=0;if(cop=="+="||cop=="-="){std::int64_t before=0;if(!nift_atomic_add_sub_checked(st->int_value,v,cop=="-=",before,nv)){error=cop+": integer overflow";return false;}}else if(cop=="&=")nv=st->int_value.fetch_and(v)&v;else if(cop=="|=")nv=st->int_value.fetch_or(v)|v;else if(cop=="^=")nv=st->int_value.fetch_xor(v)^v;else if(cop=="%="){if(v==0){error="modulo by zero";return false;}auto cur=st->int_value.load();for(;;){std::int64_t desired=(cur==std::numeric_limits<std::int64_t>::min()&&v==-1)?0:cur%v;if(st->int_value.compare_exchange_weak(cur,desired)){nv=desired;break;}}}else{error=cop+": not supported for atomic<int>";return false;}out=nift::RuntimeValue(static_cast<double>(nv));if(nv>9007199254740992LL||nv<-9007199254740992LL){out.type=nift::RuntimeType::StrNumber;out.string=std::to_string(nv);}if(depth==0)last_expression_mutation_=true;return true;}
            if(cop=="+="&&oldv.is_string()&&rhs.is_string())next=nift::RuntimeValue(oldv.string+rhs.string);else if(cop=="+="&&oldv.is_array()&&rhs.is_array()){next=nift::RuntimeValue::make_array();next.array.reserve(oldv.array.size()+rhs.array.size());next.array.insert(next.array.end(),oldv.array.begin(),oldv.array.end());next.array.insert(next.array.end(),rhs.array.begin(),rhs.array.end());}else if(cop=="&="||cop=="|="||cop=="^="){error=cop+": requires atomic<int>";return false;}else if(!numeric_binary(oldv,rhs,cop[0],next))return false;
            if (target.find_first_of(".([") == std::string::npos) {
                if (!assign_identifier(target, std::move(next))) return false;
            } else {
                const std::string temporary = bind_temporary(std::move(next));
                const bool assigned = eval(target+" = "+temporary,out,depth+1);
                variable_scopes_.back().erase(temporary);
                if(!assigned)return false;
            }
            if(depth==0)last_expression_mutation_=true;
            return true;}}

        const bool prefix_inc=(text.rfind("++",0)==0||text.rfind("--",0)==0);
        const bool postfix_inc=(text.size()>2&&(text.compare(text.size()-2,2,"++")==0||text.compare(text.size()-2,2,"--")==0) && find_top_level_op(":=")==std::string::npos && find_top_level_assignment()==std::string::npos);
        if(prefix_inc||postfix_inc){const bool inc=prefix_inc?text[0]=='+':text[text.size()-2]=='+';const std::string target=trim_copy(prefix_inc?text.substr(2):text.substr(0,text.size()-2));nift::RuntimeValue oldv,one(1.0),next;if(!eval(target,oldv,depth+1))return false;
            if(target.find_first_of(".([")==std::string::npos&&oldv.is_string()&&oldv.string.rfind("\x1fnift:atomic:",0)==0){auto ai=atomic_instances_.find(oldv.string.substr(13));if(ai==atomic_instances_.end()){error="atomic: invalid handle";return false;}if(ai->second->kind!=AtomicInstance::Kind::Int){error="increment/decrement requires atomic<int> or numeric lvalue";return false;}std::int64_t before=0,after=0;if(!nift_atomic_add_sub_checked(ai->second->int_value,1,!inc,before,after)){error="increment/decrement: integer overflow";return false;}const auto value=prefix_inc?after:before;out=nift::RuntimeValue(static_cast<double>(value));if(value>9007199254740992LL||value<-9007199254740992LL){out.type=nift::RuntimeType::StrNumber;out.string=std::to_string(value);}if(depth==0)last_expression_mutation_=true;return true;}
            if(!oldv.is_number()){error="increment/decrement requires a numeric lvalue";return false;}if(!numeric_binary(oldv,one,inc?'+':'-',next))return false;
            if (target.find_first_of(".([") == std::string::npos) {
                if (!assign_identifier(target, std::move(next))) return false;
                if (!prefix_inc) out = oldv; // postfix yields the pre-increment value
            } else {
                const std::string temporary = bind_temporary(std::move(next));
                nift::RuntimeValue assigned;const bool assigned_ok=eval(target+" = "+temporary,assigned,depth+1);
                variable_scopes_.back().erase(temporary);
                if(!assigned_ok)return false;
                out=prefix_inc?assigned:oldv;
            }
            if(depth==0)last_expression_mutation_=true;
            return true;}

        // CP208-209: deliberately small, flat destructuring surface.
        // Arrays are exact-length; objects require named keys and ignore extras.
        // No nesting, defaults, rest, or renaming in v4.3. Both declaration and
        // assignment validate/evaluate completely before mutating bindings.
        auto destructure=[&](bool declaration,std::size_t p)->bool{
            std::string lhs=trim_copy(text.substr(0,p));
            if(lhs.size()<2||!((lhs.front()=='['&&lhs.back()==']')||(lhs.front()=='{'&&lhs.back()=='}')))return false;
            bool pok=false;auto names=parse_parameters(lhs.substr(1,lhs.size()-2),pok);if(!pok||names.empty()){error="destructuring pattern must contain identifiers";return true;}
            for(auto& n:names){n=trim_copy(n);if(!valid_binding_identifier(n)){error="destructuring patterns support only flat identifiers";return true;}}
            nift::RuntimeValue rhs;if(!eval(text.substr(p+(declaration?2:1)),rhs,depth+1))return true;
            std::vector<nift::RuntimeValue> values;
            if(lhs.front()=='['){if(!rhs.is_array()){error="array destructuring requires an array";return true;}if(rhs.array.size()!=names.size()){error="array destructuring requires exactly "+std::to_string(names.size())+" items";return true;}values=rhs.array;}
            else{if(!rhs.is_object()){error="object destructuring requires an object";return true;}for(const auto& n:names){if(!rhs.has(n)){error="object destructuring missing key: "+n;return true;}values.push_back(rhs[n]);}}
            if(variable_scopes_.empty())variable_scopes_.emplace_back();
            if(declaration){std::set<std::string> seen;for(const auto& n:names){if(!seen.insert(n).second){error="duplicate destructuring target: "+n;return true;}if(reserved_binding_name(n)||host_.is_contract_name(n)){error="destructuring name is reserved: "+n;return true;}if(variable_scopes_.back().count(n)){error="binding already declared in this scope: "+n;return true;}}
                for(std::size_t j=0;j<names.size();++j){auto sp=std::make_shared<nift::RuntimeValue>(values[j]);variable_scopes_.back().emplace(names[j],VariableBinding{sp,nift_binding_type(values[j]),true,false});}}
            else{std::vector<VariableBinding*> targets;for(std::size_t j=0;j<names.size();++j){VariableBinding* b=nullptr;for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(names[j]);if(it!=sc->end()){b=&it->second;break;}}if(!b){error="assignment to undefined binding: "+names[j];return true;}if(!b->mutable_binding){error="cannot assign to const binding: "+names[j];return true;}const int at=nift_binding_type(values[j]);if(!nift_type_assignable(at,b->type)){error="cannot change binding type in destructuring: "+names[j];return true;}targets.push_back(b);}for(std::size_t j=0;j<targets.size();++j)targets[j]->rebind(std::make_shared<nift::RuntimeValue>(values[j]));}
            out=rhs;if(depth==0)last_expression_mutation_=true;return true;
        };
        if(const auto dp=find_top_level_op(":=");dp!=std::string::npos){std::string lhs=trim_copy(text.substr(0,dp));if(!lhs.empty()&&(lhs.front()=='['||lhs.front()=='{')){if(destructure(true,dp))return error.empty();}}
        if(const auto dp=find_top_level_assignment();dp!=std::string::npos){std::string lhs=trim_copy(text.substr(0,dp));if(!lhs.empty()&&(lhs.front()=='['||lhs.front()=='{')){if(destructure(false,dp))return error.empty();}}

        if (const auto p=find_top_level_op(":="); p!=std::string::npos) {
            std::string declaration = trim_copy(text.substr(0, p));
            bool mutable_binding = true;
            bool deep_readonly = false;
            if (declaration.rfind("const ", 0) == 0) { mutable_binding = false; declaration = trim_copy(declaration.substr(6)); }
            else if (declaration.rfind("immut ", 0) == 0) { mutable_binding = false; deep_readonly = true; declaration = trim_copy(declaration.substr(6)); }
            const std::string name = declaration;
            if (!valid_binding_identifier(name)) { error = "declaration requires an identifier before ':='"; return false; }
            if (reserved_binding_name(name) || host_.is_contract_name(name)) { error = "declaration name is reserved: " + name; return false; }
            if (variable_scopes_.empty()) variable_scopes_.emplace_back();
            if (variable_scopes_.back().find(name) != variable_scopes_.back().end()) {
                // Injected `cmd`/`args` bindings may be shadowed by a script that
                // intentionally declares its own binding.
                auto& existing = variable_scopes_.back().find(name)->second;
                if (!existing.is_script_invocation) { error = "binding already declared in this scope: " + name; return false; }
            }
            nift::RuntimeValue assigned;
            last_call_return_loc_root_.reset(); last_call_return_loc_path_.clear();
            if (!eval(text.substr(p + 2), assigned, depth + 1)) return false;
            std::shared_ptr<nift::RuntimeValue> stored;
            const std::string rhs_name=trim_copy(text.substr(p+2)); VariableBinding* alias=find_binding(rhs_name);
            if(alias&&alias->value&&alias->value->is_array()) stored=alias->value; else stored=std::make_shared<nift::RuntimeValue>(assigned);
            variable_scopes_.back()[name] = VariableBinding{stored, nift_binding_type_from_text(text.substr(p + 2), assigned), mutable_binding, deep_readonly};
            auto& declared_binding=variable_scopes_.back()[name];
            // Aggregate extraction is a persistent location reference.  If the
            // RHS is not a reference-shaped expression this simply remains an
            // ordinary value/root binding.
            try_location_ref(text.substr(p+2),declared_binding);
            // A call whose return carried a location keeps that location here,
            // so `c := get(loc)` rebinds c to the same root+path location.
            if(!declared_binding.is_location_ref()&&last_call_return_loc_root_&&
               (assigned.is_array()||assigned.is_object()||(assigned.is_string()&&assigned.string.rfind("\x1fnift:",0)==0))){
                declared_binding.ref_root_slot=last_call_return_loc_root_;
                declared_binding.ref_path=last_call_return_loc_path_;
                declared_binding.sync();
                if(declared_binding.value){declared_binding.type=nift_binding_type(*declared_binding.value);last_call_return_loc_root_.reset();last_call_return_loc_path_.clear();}
                else{declared_binding.ref_root_slot.reset();declared_binding.ref_path.clear();declared_binding.sync();}
            }
            if (depth == 0) last_expression_mutation_ = true;
            out = std::move(assigned);
            return true;
        }

        if (const auto p=find_top_level_assignment(); p!=std::string::npos) {
            const std::string name = trim_copy(text.substr(0, p));
            const auto dot=name.find('.');
            if(dot!=std::string::npos){
                const std::string root=name.substr(0,dot); VariableBinding* rb=nullptr;
                for(auto scope=variable_scopes_.rbegin();scope!=variable_scopes_.rend();++scope){auto it=scope->find(root);if(it!=scope->end()){rb=&it->second;break;}}
                if(!rb&&!receiver_stack_.empty()){auto rf=receiver_stack_.back()->fields.find(root);if(rf!=receiver_stack_.back()->fields.end())rb=&rf->second;}
                if(!rb||!rb->value->is_string()||rb->value->string.rfind("\x1fnift:struct:",0)!=0){error="member assignment requires a struct instance: "+root;return false;}
                nift::RuntimeValue current=*rb->value;std::size_t mp=dot+1;std::shared_ptr<StructInstance> parent;std::string member;
                while(mp<name.size()){std::size_t me=name.find('.',mp);member=name.substr(mp,me==std::string::npos?std::string::npos:me-mp);auto ii=struct_instances_.find(current.string.substr(13));if(ii==struct_instances_.end()){error="invalid struct instance";return false;}parent=ii->second;auto fit=parent->fields.find(member);if(fit==parent->fields.end()){error="struct has no field: "+member;return false;}auto sd=structs_.find(parent->type_name);bool priv=false;if(sd!=structs_.end())for(const auto& f:sd->second.fields)if(f.name==member){priv=f.private_member;break;}if(priv&&(receiver_stack_.empty()||receiver_stack_.back()!=parent)){error="private struct field: "+member;return false;}if(me==std::string::npos)break;current=*fit->second.value;if(!current.is_string()||current.string.rfind("\x1fnift:struct:",0)!=0){error="member path is not a struct: "+member;return false;}mp=me+1;}
                auto fit=parent->fields.find(member);
                nift::RuntimeValue assigned;if(!eval(text.substr(p+1),assigned,depth+1))return false;const int at=nift_binding_type_from_text(text.substr(p+1),assigned);if(!nift_type_assignable(at,fit->second.type)){error="cannot change struct field type: "+member;return false;}
                if(assigned.is_string()&&(assigned.string.rfind("\x1fnift:struct:",0)==0||assigned.string.rfind("\x1fnift:collection:",0)==0)){std::string parent_id;for(const auto& e:struct_instances_)if(e.second==parent){parent_id=e.first;break;}if(reference_would_cycle(assigned.string,std::string("\x1fnift:struct:")+parent_id)){error="assignment would create a cyclic reference: "+member;return false;}}
                fit->second.value=std::make_shared<nift::RuntimeValue>(std::move(assigned));out=*fit->second.value;if(depth==0)last_expression_mutation_=true;return true;
            }
            if (name.find('[') != std::string::npos) {
                // Bracket-indexed assignment: a[i] = v, m["k"] = v, grid[r][c] = v.
                // Mutates the element in place through the root binding's shared
                // Document so earlier reads of the binding see the change. This
                // also makes a[i] += 1 and a[i]++ work (they rewrite to a[i] = v).
                const std::size_t open = name.find('[');
                const std::string root = trim_copy(name.substr(0, open));
                VariableBinding* rb = nullptr;
                for (auto scope = variable_scopes_.rbegin(); scope != variable_scopes_.rend(); ++scope) {
                    const auto it = scope->find(root);
                    if (it != scope->end()) { rb = &it->second; break; }
                }
                if (!rb) { error = "assignment to undefined binding: " + root; return false; }
                if (!rb->mutable_binding) { error = "cannot assign to const binding: " + root; return false; }
                std::vector<std::string> indexes;
                std::size_t pos = open;
                while (pos < name.size()) {
                    const std::size_t close = name.find(']', pos);
                    if (close == std::string::npos) { error = "unterminated index in assignment target: " + name; return false; }
                    indexes.push_back(trim_copy(name.substr(pos + 1, close - pos - 1)));
                    pos = close + 1;
                    std::size_t q = pos;
                    while (q < name.size() && isspace(static_cast<unsigned char>(name[q]))) ++q;
                    if (q == name.size()) break;
                    if (name[q] != '[') { error = "invalid assignment target: " + name; return false; }
                    pos = q;
                }
                nift::RuntimeValue assigned;
                if (!eval(text.substr(p + 1), assigned, depth + 1)) return false;
                nift::RuntimeValue* node = rb->value.get();
                for (std::size_t k = 0; k + 1 < indexes.size(); ++k) {
                    nift::RuntimeValue idx;
                    if (!eval(indexes[k], idx, depth + 1)) return false;
                    std::size_t i = 0;
                    if (node->is_array() && nift::runtime_number_to_size(idx, i)) {
                        if (i >= node->array.size()) { error = "index out of range: " + indexes[k]; return false; }
                        node = &(*node)[i];
                    } else if (idx.is_string() && node->is_object()) {
                        if (!node->has(idx.string)) { error = "object has no member: " + idx.string; return false; }
                        node = &(*node)[idx.string];
                    } else {
                        error = "assignment index does not address an array or object: " + indexes[k];
                        return false;
                    }
                }
                nift::RuntimeValue last;
                if (!eval(indexes.back(), last, depth + 1)) return false;
                std::size_t i = 0;
                if (node->is_array() && nift::runtime_number_to_size(last, i)) {
                    if (i >= node->array.size()) { error = "index out of range: " + indexes.back(); return false; }
                    (*node)[i] = std::move(assigned);
                    out = (*node)[i];
                } else if (last.is_string() && node->is_object()) {
                    (*node)[last.string] = std::move(assigned);
                    out = (*node)[last.string];
                } else {
                    error = "assignment index does not address an array or object: " + indexes.back();
                    return false;
                }
                if (depth == 0) last_expression_mutation_ = true;
                return true;
            }
            if (!valid_binding_identifier(name)) { error = "assignment requires an identifier before '='"; return false; }
            VariableBinding* binding = nullptr;
            for (auto scope = variable_scopes_.rbegin(); scope != variable_scopes_.rend(); ++scope) {
                const auto it = scope->find(name);
                if (it != scope->end()) { binding = &it->second; break; }
            }
            if (!binding && !receiver_stack_.empty()) {
                auto rec = receiver_stack_.back()->fields.find(name);
                if (rec != receiver_stack_.back()->fields.end()) {
                    nift::RuntimeValue assigned;
                    if (!eval(text.substr(p + 1), assigned, depth + 1)) return false;
                    const int assigned_type = nift_binding_type_from_text(text.substr(p + 1), assigned);
                    if (!nift_type_assignable(assigned_type, rec->second.type)) {
                        error = "cannot assign " + std::string(nift_binding_type_name(assigned_type)) +
                                " to " + nift_binding_type_name(rec->second.type) + " struct field '" + name + "'";
                        return false;
                    }
                    if (assigned.is_string()&&(assigned.string.rfind("\x1fnift:struct:",0)==0||assigned.string.rfind("\x1fnift:collection:",0)==0)){std::string recv_id;for(const auto& e:struct_instances_)if(e.second==receiver_stack_.back()){recv_id=e.first;break;}if(reference_would_cycle(assigned.string,std::string("\x1fnift:struct:")+recv_id)){error="assignment would create a cyclic reference: "+name;return false;}}
                    auto rebound = std::make_shared<nift::RuntimeValue>(std::move(assigned));
                    rec->second.value = rebound;
                    if (depth == 0) last_expression_mutation_ = true;
                    out = *rebound;
                    return true;
                }
            }
            if (!binding) { error = "assignment to undefined binding: " + name; return false; }
            if (!binding->mutable_binding) { error = "cannot assign to const binding: " + name; return false; }
            nift::RuntimeValue assigned;
            if (!eval(text.substr(p + 1), assigned, depth + 1)) return false;
            if(binding->value&&binding->value->is_string()&&binding->value->string.rfind("\x1fnift:atomic:",0)==0&&!(assigned.is_string()&&assigned.string.rfind("\x1fnift:atomic:",0)==0)){auto ai=atomic_instances_.find(binding->value->string.substr(13));if(ai==atomic_instances_.end()){error="atomic: invalid handle";return false;}if(ai->second->kind==AtomicInstance::Kind::Int){std::int64_t v=0;if(!exact_i64(assigned,v)){error="assignment to atomic<int> requires signed 64-bit integer";return false;}ai->second->int_value.store(v);}else{if(!assigned.is_bool()){error="assignment to atomic<bool> requires bool";return false;}ai->second->bool_value.store(assigned.boolean);}out=assigned;if(depth==0)last_expression_mutation_=true;return true;}
            const int assigned_type = nift_binding_type_from_text(text.substr(p + 1), assigned);
            if (!nift_type_assignable(assigned_type, binding->type)) {
                error = "cannot assign " + std::string(nift_binding_type_name(assigned_type)) +
                        " to " + nift_binding_type_name(binding->type) + " binding '" + name + "'";
                return false;
            }
            auto rebound = std::make_shared<nift::RuntimeValue>(std::move(assigned));
            binding->rebind(rebound);
            if (depth == 0) last_expression_mutation_ = true;
            out = *rebound;
            return true;
        }

        if (const auto p=find_top_level_op("||"); p!=std::string::npos) {
            nift::RuntimeValue left;
            if (!eval(text.substr(0,p),left,depth+1)) return false;
            if (truthy_value(left)) { out=nift::RuntimeValue(true); return true; }
            nift::RuntimeValue right; if (!eval(text.substr(p+2),right,depth+1)) return false;
            out=nift::RuntimeValue(truthy_value(right)); return true;
        }
        if (const auto p=find_top_level_op("&&"); p!=std::string::npos) {
            nift::RuntimeValue left;
            if (!eval(text.substr(0,p),left,depth+1)) return false;
            if (!truthy_value(left)) { out=nift::RuntimeValue(false); return true; }
            nift::RuntimeValue right; if (!eval(text.substr(p+2),right,depth+1)) return false;
            out=nift::RuntimeValue(truthy_value(right)); return true;
        }
        for (const std::string op : {"==","!=","<=",">=","<",">"}) {
            const auto p=find_top_level_op(op);
            if (p==std::string::npos) continue;
            nift::RuntimeValue left,right;
            if (!eval(text.substr(0,p),left,depth+1) || !eval(text.substr(p+op.size()),right,depth+1)) return false;
            nift::RuntimeValue ls,rs;if(!atomic_scalar(left,ls)||!atomic_scalar(right,rs))return false;left=std::move(ls);right=std::move(rs);
            bool result=false;
            if (op=="==" || op=="!=") {
                bool equal=false;
                equal = structural_equal(left,right);
                result = op=="==" ? equal : !equal;
            } else {
                if(left.is_bytes()||right.is_bytes()){error="bytes ordering is not supported";return false;}
                if((left.is_string()&&left.string.rfind("\x1fnift:enum:",0)==0)||(right.is_string()&&right.string.rfind("\x1fnift:enum:",0)==0)){error="enum ordering is not defined; compare explicit to_int() values if intended";return false;}
                if ((!left.is_number() || !right.is_number()) && !(left.is_string() && right.is_string())) { error="ordering comparisons require two numbers or two strings"; return false; }
                int ordering=0;
                if (left.is_number() && right.is_number()) {
                    if (!nift::runtime_compare_numbers_relational(left,right,ordering)) {
                        out=nift::RuntimeValue(false); return true;
                    }
                } else ordering=left.string<right.string?-1:(left.string>right.string?1:0);
                if (op=="<") result=ordering<0; else if (op=="<=") result=ordering<=0; else if (op==">") result=ordering>0; else result=ordering>=0;
            }
            out=nift::RuntimeValue(result); return true;
        }
        if (text.front()=='!' && (text.size()<2 || text[1]!='=')) {
            nift::RuntimeValue operand; if (!eval(text.substr(1),operand,depth+1)) return false;
            out=nift::RuntimeValue(!truthy_value(operand)); return true;
        }

        { std::size_t cp=std::string::npos;bool q=false;char qc=0;int pa=0,br=0,bc=0;for(size_t z=text.size();z-- >0;){char c=text[z];if(q){if(c==qc&&(z==0||text[z-1]!='\\'))q=false;continue;}if(c=='\''||c=='"'){q=true;qc=c;continue;}if(c==')')++pa;else if(c=='(')--pa;else if(c==']')++br;else if(c=='[')--br;else if(c=='}')++bc;else if(c=='{')--bc;else if(c=='+'&&!pa&&!br&&!bc){if((z>0&&std::string("+-*/%(<>=!&|?:,").find(text[z-1])!=std::string::npos)||numeric_exponent_sign(text,z))continue;cp=z;break;}}if(cp!=std::string::npos){nift::RuntimeValue l,r;if(!eval(text.substr(0,cp),l,depth+1)||!eval(text.substr(cp+1),r,depth+1))return false;nift::RuntimeValue ls,rs;if(!atomic_scalar(l,ls)||!atomic_scalar(r,rs))return false;l=std::move(ls);r=std::move(rs);if(l.is_array()&&r.is_array()){out=nift::RuntimeValue::make_array();out.array.reserve(l.array.size()+r.array.size());out.array.insert(out.array.end(),l.array.begin(),l.array.end());out.array.insert(out.array.end(),r.array.begin(),r.array.end());return true;}if(l.is_bytes()||r.is_bytes()){if(!l.is_bytes()||!r.is_bytes()){error="bytes values cannot be rendered as text; bytes concatenation requires two bytes values";return false;}nift::RuntimeBytes joined;if(l.bytes)joined.insert(joined.end(),l.bytes->begin(),l.bytes->end());if(r.bytes)joined.insert(joined.end(),r.bytes->begin(),r.bytes->end());out=nift::RuntimeValue(std::move(joined));return true;}if(l.is_string()||r.is_string()){auto safe=[&](const nift::RuntimeValue& v,std::string& z){if(v.is_timer()||v.is_bytes()||v.is_array()||v.is_object()||(v.is_string()&&v.string.rfind("\x1fnift:",0)==0&&v.string.rfind("\x1fnift:timer:",0)!=0))return false;z=render_expression_value(v);return true;};std::string a,b;if(!safe(l,a)||!safe(r,b)){error="string concatenation requires renderable scalar values";return false;}out=nift::RuntimeValue(a+b);return true;}return numeric_binary(l,r,'+',out);}}

        auto find_binary = [&](const std::string& ops) -> std::size_t {
            bool quoted=false; char quote=0; int parens=0; int brackets=0;
            for (std::size_t i=text.size(); i-- > 0;) {
                char c=text[i];
                if (quoted) { if (c==quote && (i==0 || text[i-1]!='\\')) quoted=false; continue; }
                if (c=='\'' || c=='"') { quoted=true; quote=c; continue; }
                if (c==']') { ++brackets; continue; }
                if (c=='[') { if (brackets) --brackets; continue; }
                if (brackets) continue;
                if (c==')') { ++parens; continue; }
                if (c=='(') { if (parens) --parens; continue; }
                if (parens || ops.find(c)==std::string::npos) continue;
                if ((c=='+' || c=='-') &&
                    (i==0 || std::string("+-*/%(<>=!&|?:,").find(text[i-1])!=std::string::npos ||
                     numeric_exponent_sign(text,i))) continue;
                return i;
            }
            return std::string::npos;
        };

        std::size_t pos=find_binary("+-");
        if (pos==std::string::npos) pos=find_binary("*/%");
        if (pos!=std::string::npos) {
            nift::RuntimeValue left,right;
            if (!eval(text.substr(0,pos),left,depth+1) || !eval(text.substr(pos+1),right,depth+1)) return false;
            nift::RuntimeValue ls,rs;if(!atomic_scalar(left,ls)||!atomic_scalar(right,rs))return false;left=std::move(ls);right=std::move(rs);
            const char op=text[pos];
            if(op=='+' && left.is_array() && right.is_array()){out=nift::RuntimeValue::make_array();out.array.reserve(left.array.size()+right.array.size());out.array.insert(out.array.end(),left.array.begin(),left.array.end());out.array.insert(out.array.end(),right.array.begin(),right.array.end());return true;}
            if(op=='+' && (left.is_bytes()||right.is_bytes())){if(!left.is_bytes()||!right.is_bytes()){error="bytes values cannot be rendered as text; bytes concatenation requires two bytes values";return false;}nift::RuntimeBytes joined;if(left.bytes)joined.insert(joined.end(),left.bytes->begin(),left.bytes->end());if(right.bytes)joined.insert(joined.end(),right.bytes->begin(),right.bytes->end());out=nift::RuntimeValue(std::move(joined));return true;}
            if(op=='+' && (left.is_string()||right.is_string())) { auto safe=[&](const nift::RuntimeValue& v,std::string& r){if(v.is_timer()||v.is_bytes()||v.is_array()||v.is_object()||(v.is_string()&&v.string.rfind("\x1fnift:",0)==0&&v.string.rfind("\x1fnift:timer:",0)!=0))return false;r=render_expression_value(v);return true;};std::string l,r;if(!safe(left,l)||!safe(right,r)){error="string concatenation requires renderable scalar values";return false;}out=nift::RuntimeValue(l+r);return true; }
            if (!left.is_number() || !right.is_number()) { error="arithmetic operators require numeric operands"; return false; }
            return numeric_binary(left,right,op,out);
        }

        if ((text.front()=='+' || text.front()=='-') && text.size()>1) {
            nift::RuntimeValue operand;
            if (!eval(text.substr(1),operand,depth+1)) return false;
            nift::RuntimeValue scalar;if(!atomic_scalar(operand,scalar))return false;operand=std::move(scalar);
            if (!operand.is_number()) { error="unary arithmetic operators require a numeric operand"; return false; }
            out=text.front()=='-' ? nift::runtime_number_negate(operand) : operand; return true;
        }

        error="unknown value or malformed expression: " + text;
        return false;
    };
    return eval(expression,value,0);
}

bool Parser::evaluate_condition(const std::string& expression, bool& value, std::string& error) {
    auto resolve_operand = [&](const std::string& operand,
                               std::shared_ptr<const nift::RuntimeValue>& document) -> bool {
        nift::RuntimeValue evaluated;
        if (!evaluate_expression(operand, evaluated, error)) return false;
        document = std::make_shared<const nift::RuntimeValue>(std::move(evaluated));
        return true;
    };

    auto truthy = [](const nift::RuntimeValue& document) { return nift::runtime_truthy(document); };

    std::function<bool(const std::string&, bool&)> eval;
    eval = [&](const std::string& raw, bool& result) -> bool {
        std::string condition = trim_copy(raw);
        if (condition.empty()) {
            error = "@if condition cannot be empty";
            return false;
        }

        auto encloses_entire_expression = [&](const std::string& text) {
            if (text.size() < 2 || text.front() != '(' || text.back() != ')') return false;
            bool quoted = false; char quote = 0; int parens = 0; int brackets = 0;
            for (std::size_t i = 0; i < text.size(); ++i) {
                const char c = text[i];
                if (quoted) {
                    if (c == '\\' && i + 1 < text.size()) ++i;
                    else if (c == quote) quoted = false;
                    continue;
                }
                if (c == '\'' || c == '"') { quoted = true; quote = c; continue; }
                if (c == '[') { ++brackets; continue; }
                if (c == ']') { if (brackets) --brackets; continue; }
                if (brackets) continue;
                if (c == '(') ++parens;
                else if (c == ')' && --parens == 0 && i + 1 != text.size()) return false;
                if (parens < 0) return false;
            }
            return parens == 0 && !quoted;
        };
        while (encloses_entire_expression(condition)) {
            condition = trim_copy(condition.substr(1, condition.size() - 2));
            if (condition.empty()) { error = "@if condition cannot be empty"; return false; }
        }

        auto find_top_level = [&](const std::string& op) -> std::size_t {
            bool quoted = false; char quote = 0; int parens = 0; int brackets = 0;
            for (std::size_t i = 0; i + op.size() <= condition.size(); ++i) {
                const char c = condition[i];
                if (quoted) {
                    if (c == '\\' && i + 1 < condition.size()) ++i;
                    else if (c == quote) quoted = false;
                    continue;
                }
                if (c == '\'' || c == '"') { quoted = true; quote = c; continue; }
                if (c == '[') { ++brackets; continue; }
                if (c == ']') { if (brackets) --brackets; continue; }
                if (brackets) continue;
                if (c == '(') { ++parens; continue; }
                if (c == ')') { if (parens) --parens; continue; }
                if (parens == 0 && condition.compare(i, op.size(), op) == 0) return i;
            }
            return std::string::npos;
        };

        // Lowest precedence first. Recursing on each side gives && tighter binding
        // than || and preserves short-circuit behaviour.
        if (const auto pos = find_top_level("||"); pos != std::string::npos) {
            bool left = false;
            if (!eval(condition.substr(0, pos), left)) return false;
            if (left) { result = true; return true; }
            return eval(condition.substr(pos + 2), result);
        }
        if (const auto pos = find_top_level("&&"); pos != std::string::npos) {
            bool left = false;
            if (!eval(condition.substr(0, pos), left)) return false;
            if (!left) { result = false; return true; }
            return eval(condition.substr(pos + 2), result);
        }

        if (condition.front() == '!' && (condition.size() < 2 || condition[1] != '=')) {
            bool nested = false;
            if (!eval(condition.substr(1), nested)) return false;
            result = !nested;
            return true;
        }

        std::string op;
        std::size_t op_position = std::string::npos;
        for (const std::string candidate : {"==", "!=", "<=", ">=", "<", ">"}) {
            op_position = find_top_level(candidate);
            if (op_position != std::string::npos) { op = candidate; break; }
        }

        if (!op.empty()) {
            std::shared_ptr<const nift::RuntimeValue> left, right;
            if (!resolve_operand(condition.substr(0, op_position), left) ||
                !resolve_operand(condition.substr(op_position + op.size()), right)) return false;

            if (op == "==" || op == "!=") {
                bool equal = false;
                if (left->is_number() && right->is_number()) equal = nift::runtime_numbers_equal(*left, *right);
                else if (left->type == right->type) {
                    if (left->is_null()) equal = true;
                    else if (left->is_bool()) equal = left->boolean == right->boolean;
                    else if (left->is_string()) equal = left->string == right->string;
                    else if (left->is_bytes()) equal = nift::runtime_equal(*left,*right);
                    else { error = "@if comparisons are only supported for scalar JSON values"; return false; }
                }
                result = op == "==" ? equal : !equal;
                return true;
            }

            if(left->is_bytes()||right->is_bytes()){error="bytes ordering is not supported";return false;}
            if ((!left->is_number() || !right->is_number()) &&
                (left->type != right->type || !left->is_string())) {
                error = "@if ordering comparisons require two numbers or two strings of the same type";
                return false;
            }
            int ordering = 0;
            if (left->is_number()) {
                if (!nift::runtime_compare_numbers_relational(*left, *right, ordering)) {
                    result = false;
                    return true;
                }
            }
            else ordering = left->string < right->string ? -1 : (left->string > right->string ? 1 : 0);
            if (op == "<") result = ordering < 0;
            else if (op == "<=") result = ordering <= 0;
            else if (op == ">") result = ordering > 0;
            else result = ordering >= 0;
            return true;
        }

        std::shared_ptr<const nift::RuntimeValue> document;
        if (!resolve_operand(condition, document)) return false;
        result = truthy(*document);
        return true;
    };

    return eval(expression, value);
}

bool Parser::invoke_struct_method(std::shared_ptr<StructInstance> instance,
                                  const StructMethod& method,
                                  const std::vector<nift::RuntimeValue>& args,
                                  const std::vector<std::string>& arg_sources,
                                  nift::RuntimeValue& out,
                                  std::string& error) {
    if (method.callable.variadic_param.empty() ? args.size() != method.callable.params.size() : args.size() < method.callable.params.size()) { error = "struct method argument count mismatch"; return false; }
    if (callable_call_depth_ >= kMaxCallableDepth) { error = "callable recursion depth exceeded"; return false; }
    ++callable_call_depth_;
    const int caller_loop_depth = loop_depth_; loop_depth_ = 0;
    auto lexical_env = enter_lexical_environment(method.callable.module_env);
    push_variable_scope(); const std::size_t scope_index=variable_scopes_.size()-1;
    for(std::size_t i=0;i<method.callable.params.size();++i){auto sp=std::make_shared<nift::RuntimeValue>(args[i]);variable_scopes_[scope_index][method.callable.params[i]]=VariableBinding{sp,nift_binding_type_from_text(arg_sources[i],*sp),true,false};}
    if(!method.callable.variadic_param.empty()){nift::RuntimeValue rest=nift::RuntimeValue::make_array();for(std::size_t i=method.callable.params.size();i<args.size();++i)rest.array.push_back(args[i]);auto sp=std::make_shared<nift::RuntimeValue>(std::move(rest));variable_scopes_[scope_index].emplace(method.callable.variadic_param,VariableBinding{sp,nift_binding_type(*sp),true,false});}
    std::string id;
    for(const auto& e:struct_instances_) if(e.second==instance){id=e.first;break;}
    auto thisv=std::make_shared<nift::RuntimeValue>(std::string("\x1fnift:struct:")+id); variable_scopes_[scope_index]["this"]=VariableBinding{thisv,nift_binding_type(*thisv),false,false};
    receiver_stack_.push_back(instance); ++function_call_depth_; pending_control_={};
    RenderResult rr=execute_native_program(method.callable.body,method.callable.source_path,1);
    --function_call_depth_; receiver_stack_.pop_back();
    pop_variable_scope();
    leave_lexical_environment(std::move(lexical_env)); loop_depth_=caller_loop_depth; --callable_call_depth_;
    if(!rr.ok){error=rr.error.message;pending_control_={};return false;}
    if(method.constructor && pending_control_.kind==ControlFlow::Return && pending_control_.value && !pending_control_.value->is_null()){error="constructor cannot return a value";pending_control_={};return false;}
    consume_return(out); return true;
}
