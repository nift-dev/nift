#include "ParserHelpers.h"
#include "JsonFile.h"
#include "RenderHost.h"
#include "RuntimeJson.h"
#include "RuntimeValue.h"

#include <nift/context.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <sstream>
#include <type_traits>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <cerrno>
#include <sys/random.h>
#elif defined(__APPLE__)
#include <cstdlib>
#else
#error "secure_random_bytes requires a supported native CSPRNG"
#endif
#ifndef _WIN32
#include <time.h>
#endif

namespace fs = std::filesystem;

namespace nift::detail {

int nift_binding_type(const nift::RuntimeValue& value) {
    if (value.is_null()) return 0;
    if (value.is_bool()) return 1;
    if (value.is_number()) return nift::runtime_number_is_integer(value) ? 2 : 3;
    if (value.is_string()) return 4;
    if (value.is_array()) return 5;
    if (value.is_object()) return 6;
    if (value.is_bytes()) return 7;
    if (value.is_timer()) return 8;
    if (value.is_error()) return 9;
    return -1;
}

const char* nift_binding_type_name(int type) {
    switch (type) { case 0:return "null"; case 1:return "bool"; case 2:return "int"; case 3:return "double"; case 4:return "string"; case 5:return "array"; case 6:return "json"; case 7:return "bytes"; case 8:return "timer"; case 9:return "error"; default:return "unknown"; }
}

// An int value may be assigned to a double binding (widening: a double binding
// represents any numeric value under Nift's arithmetic model, which already
// computes int + double as double). The reverse (double -> int) stays an error
// because it is lossy.
bool nift_type_assignable(int from, int to) {
    return from == to || (from == 2 && to == 3);
}

extern const int kMaxCallableDepth = 64;

bool nift_unix_epoch_milliseconds(std::int64_t& value, std::string& error) {
#ifdef _WIN32
    FILETIME file_time;
    GetSystemTimeAsFileTime(&file_time);
    ULARGE_INTEGER ticks;
    ticks.LowPart = file_time.dwLowDateTime;
    ticks.HighPart = file_time.dwHighDateTime;
    constexpr std::uint64_t windows_to_unix_ticks = 116444736000000000ULL;
    if (ticks.QuadPart < windows_to_unix_ticks) {
        error = "epoch: system clock predates the Unix epoch";
        return false;
    }
    value = static_cast<std::int64_t>((ticks.QuadPart - windows_to_unix_ticks) / 10000ULL);
#else
    timespec now{};
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        error = "epoch: cannot read the system clock";
        return false;
    }
    using Seconds = decltype(now.tv_sec);
    const auto seconds_raw = now.tv_sec;
    if constexpr (!std::is_signed_v<Seconds>) {
        if (static_cast<std::uintmax_t>(seconds_raw) >
            static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max())) {
            error = "epoch: system time is outside the signed 64-bit millisecond range";
            return false;
        }
    }
    const std::int64_t seconds = static_cast<std::int64_t>(seconds_raw);
    if (static_cast<Seconds>(seconds) != seconds_raw) {
        error = "epoch: system time is outside the signed 64-bit millisecond range";
        return false;
    }
    if (seconds < std::numeric_limits<std::int64_t>::min() / 1000 ||
        seconds > std::numeric_limits<std::int64_t>::max() / 1000) {
        error = "epoch: system time is outside the signed 64-bit millisecond range";
        return false;
    }
    const std::int64_t milliseconds = now.tv_nsec / 1000000;
    const std::int64_t whole_seconds = seconds * 1000;
    if (whole_seconds >= 0 &&
        milliseconds > std::numeric_limits<std::int64_t>::max() - whole_seconds) {
        error = "epoch: system time is outside the signed 64-bit millisecond range";
        return false;
    }
    value = whole_seconds + milliseconds;
#endif
    return true;
}

bool nift_secure_random_bytes(std::size_t count, nift::RuntimeValue& value,
                              std::string& error) {
    nift::RuntimeBytes bytes;
    try {
        bytes.assign(count, 0);
    } catch (const std::exception&) {
        error = "secure_random_bytes: cannot allocate result";
        return false;
    }
    if (count == 0) {
        try {
            value = nift::RuntimeValue(std::move(bytes));
        } catch (const std::exception&) {
            error = "secure_random_bytes: cannot allocate result";
            return false;
        }
        return true;
    }
#ifdef _WIN32
    using BCryptGenRandomFn = LONG (WINAPI*)(void*, unsigned char*, unsigned long, unsigned long);
    wchar_t system_directory[MAX_PATH];
    const UINT system_length = GetSystemDirectoryW(system_directory, MAX_PATH);
    std::wstring bcrypt_path;
    if (system_length > 0 && system_length < MAX_PATH) {
        bcrypt_path.assign(system_directory, system_length);
        bcrypt_path += L"\\bcrypt.dll";
    }
    HMODULE module = bcrypt_path.empty() ? nullptr : LoadLibraryW(bcrypt_path.c_str());
    BCryptGenRandomFn generate = nullptr;
    if (module) {
        const FARPROC address = GetProcAddress(module, "BCryptGenRandom");
        // Windows procedure addresses share the representation of typed function pointers.
        static_assert(sizeof(generate) == sizeof(address));
        std::memcpy(&generate, &address, sizeof(generate));
    }
    std::size_t offset = 0;
    while (generate && offset < count) {
        const auto chunk = static_cast<unsigned long>(std::min<std::size_t>(count - offset, std::numeric_limits<unsigned long>::max()));
        if (generate(nullptr, bytes.data() + offset, chunk, 0x00000002UL) != 0) break;
        offset += chunk;
    }
    if (module) FreeLibrary(module);
    if (!generate || offset != count) {
        error = "secure_random_bytes: operating-system random source failed";
        return false;
    }
#elif defined(__linux__)
    std::size_t offset = 0;
    while (offset < count) {
        const auto received = ::getrandom(bytes.data() + offset, count - offset, 0);
        if (received > 0) { offset += static_cast<std::size_t>(received); continue; }
        if (received < 0 && errno == EINTR) continue;
        error = "secure_random_bytes: operating-system random source failed";
        return false;
    }
#elif defined(__APPLE__)
    if (count != 0) ::arc4random_buf(bytes.data(), bytes.size());
#endif
    try {
        value = nift::RuntimeValue(std::move(bytes));
    } catch (const std::exception&) {
        error = "secure_random_bytes: cannot allocate result";
        return false;
    }
    return true;
}

bool parse_runtime_json(const std::string& text, nift::RuntimeValue& value,
                        std::string& error) {
    json::Document document;
    if (!nift_json::parse(text, document, error)) return false;
    value = nift::runtime_from_json(document);
    return true;
}

bool call_runtime_host(const RenderHost& host, const std::string& name,
                       const std::vector<nift::RuntimeValue>& args,
                       nift::RuntimeValue& out, std::string& error) {
    return host.call_host_callable(name, args, out, error);
}

std::string runtime_scalar_key(const nift::RuntimeValue& value) {
    if (value.is_bool()) return std::string("b") + (value.boolean ? "1" : "0");
    if (value.is_number()) return "n" + nift::runtime_numeric_fingerprint(value);
    if (value.is_string() && (value.string.rfind("\x1fnift:", 0) != 0 ||
                              value.string.rfind("\x1fnift:timer:", 0) == 0))
        return "s" + value.string;
    return {};
}

bool runtime_contains_bytes(const nift::RuntimeValue& value) {
    if (value.is_bytes()) return true;
    if (value.is_array())
        return std::any_of(value.array.begin(), value.array.end(), runtime_contains_bytes);
    if (value.is_object())
        return std::any_of(value.object.begin(), value.object.end(), [](const auto& entry) {
            return runtime_contains_bytes(entry.second);
        });
    return false;
}

std::string runtime_bytes_string(const nift::RuntimeValue& value) {
    if (!value.bytes || value.bytes->empty()) return {};
    return std::string(reinterpret_cast<const char*>(value.bytes->data()), value.bytes->size());
}

bool nift_atomic_add_sub_checked(std::atomic<std::int64_t>& value,
                                 std::int64_t operand, bool subtract,
                                 std::int64_t& before, std::int64_t& after) {
    before = value.load();
    for (;;) {
        if ((!subtract && ((operand > 0 && before > std::numeric_limits<std::int64_t>::max() - operand) ||
                           (operand < 0 && before < std::numeric_limits<std::int64_t>::min() - operand))) ||
            (subtract && ((operand > 0 && before < std::numeric_limits<std::int64_t>::min() + operand) ||
                          (operand < 0 && before > std::numeric_limits<std::int64_t>::max() + operand)))) return false;
        after = subtract ? before - operand : before + operand;
        if (value.compare_exchange_weak(before, after)) return true;
    }
}

bool glob_has_magic(const std::string& s) {
    bool escaped=false; for(char c:s){if(escaped){escaped=false;continue;}if(c=='\\'){escaped=true;continue;}if(c=='*'||c=='?')return true;} return false;
}

namespace {

void glob_walk(const fs::path& base,const std::vector<std::string>& parts,std::size_t i,std::vector<fs::path>& out){
    if(i==parts.size()){std::error_code ec;if(fs::exists(base,ec)&&!ec)out.push_back(fs::absolute(base).lexically_normal());return;}
    const auto& part=parts[i];
    if(part=="**"){
        glob_walk(base,parts,i+1,out);
        std::error_code ec; if(!fs::is_directory(base,ec)||ec)return;
        std::vector<fs::directory_entry> entries; for(fs::directory_iterator it(base,fs::directory_options::skip_permission_denied,ec),end;!ec&&it!=end;it.increment(ec))entries.push_back(*it);
        std::sort(entries.begin(),entries.end(),[](const auto&a,const auto&b){return a.path().generic_string()<b.path().generic_string();});
        for(const auto& e:entries){auto name=e.path().filename().string();if(!name.empty()&&name[0]=='.')continue;std::error_code sec;if(e.is_directory(sec)&&!e.is_symlink(sec))glob_walk(e.path(),parts,i,out);}
        return;
    }
    if(!glob_has_magic(part)){std::string literal;literal.reserve(part.size());for(std::size_t k=0;k<part.size();++k){if(part[k]=='\\'&&k+1<part.size())literal+=part[++k];else literal+=part[k];}glob_walk(base/literal,parts,i+1,out);return;}
    std::error_code ec;if(!fs::is_directory(base,ec)||ec)return;std::vector<fs::directory_entry> entries;for(fs::directory_iterator it(base,fs::directory_options::skip_permission_denied,ec),end;!ec&&it!=end;it.increment(ec))entries.push_back(*it);
    std::sort(entries.begin(),entries.end(),[](const auto&a,const auto&b){return a.path().generic_string()<b.path().generic_string();});for(const auto&e:entries)if(glob_component_match(part,e.path().filename().string()))glob_walk(e.path(),parts,i+1,out);
}

bool is_single_quoted_parameter(const std::string& text) {
    if (text.size() < 2 || (text.front() != '\'' && text.front() != '"')) return false;
    const char outer = text.front();
    for (std::size_t i = 1; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) { ++i; continue; }
        if (text[i] == outer) return i + 1 == text.size();
    }
    return false;
}

}  // namespace

bool numeric_exponent_sign(const std::string& text, std::size_t sign) {
    if (sign < 2 || (text[sign - 1] != 'e' && text[sign - 1] != 'E')) return false;
    std::size_t start = sign - 1;
    bool digit = false;
    bool dot = false;
    while (start > 0) {
        const unsigned char c = static_cast<unsigned char>(text[start - 1]);
        if (std::isdigit(c)) { digit = true; --start; continue; }
        if (c == '.' && !dot) { dot = true; --start; continue; }
        break;
    }
    if (!digit) return false;
    if (start > 0) {
        const unsigned char before = static_cast<unsigned char>(text[start - 1]);
        if (std::isalnum(before) || before == '_' || before == '.') return false;
    }
    return true;
}

bool glob_component_match(const std::string& pattern,const std::string& name){
    if(!name.empty()&&name[0]=='.'&&(pattern.empty()||pattern[0]!='.'))return false;
    std::size_t p=0,n=0,star=std::string::npos,mark=0; bool escaped=false;
    while(n<name.size()){
        if(p<pattern.size()&&pattern[p]=='\\'&&!escaped){if(p+1<pattern.size()){++p;if(pattern[p]==name[n]){++p;++n;continue;}}}
        else if(p<pattern.size()&&pattern[p]=='?'){++p;++n;continue;}
        else if(p<pattern.size()&&pattern[p]=='*'){star=p++;mark=n;continue;}
        else if(p<pattern.size()&&pattern[p]==name[n]){++p;++n;continue;}
        if(star!=std::string::npos){p=star+1;n=++mark;continue;}return false;
    }
    while(p<pattern.size()&&pattern[p]=='*')++p;
    return p==pattern.size();
}

std::vector<fs::path> glob_expand(const fs::path& resolved_pattern){
    std::string g=resolved_pattern.generic_string();fs::path root=resolved_pattern.root_path();std::string rel=root.empty()?g:g.substr(root.generic_string().size());while(!rel.empty()&&rel.front()=='/')rel.erase(rel.begin());std::vector<std::string> parts;std::stringstream ss(rel);std::string part;while(std::getline(ss,part,'/'))if(!part.empty())parts.push_back(part);std::vector<fs::path> out;glob_walk(root.empty()?fs::path("."):root,parts,0,out);std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.generic_string()<b.generic_string();});out.erase(std::unique(out.begin(),out.end()),out.end());return out;
}

// Presentation-method chain recognizer (.stringify()/.prettify()/.highlight()).
// It only strips a trailing chain when the dots sit at bracket/paren/quote
// depth 0 and the remaining base is a single value expression with no
// top-level operator. Larger compound expressions such as
// `a == b.prettify()` or `"x" + a.prettify()` are therefore left to the
// ordinary evaluator, and a string literal like "a.prettify()" is never
// mistaken for a presentation call.
bool strip_presentation_chain(const std::string& text, std::string& base, bool& pretty, bool& highlight) {
    pretty = highlight = false;
    std::size_t end = text.size();
    auto balanced_prefix = [&](std::size_t cut) {
        bool quoted = false; char qc = 0; int par = 0, br = 0, bc = 0;
        for (std::size_t i = 0; i < cut; ++i) {
            const char c = text[i];
            if (quoted) { if (c == '\\' && i + 1 < cut) ++i; else if (c == qc) quoted = false; continue; }
            if (c == '\'' || c == '"') { quoted = true; qc = c; continue; }
            if (c == '(') ++par; else if (c == ')') { if (--par < 0) return false; }
            else if (c == '[') ++br; else if (c == ']') { if (--br < 0) return false; }
            else if (c == '{') ++bc; else if (c == '}') { if (--bc < 0) return false; }
        }
        return !quoted && par == 0 && br == 0 && bc == 0;
    };
    bool matched = false;
    for (;;) {
        bool stripped = false;
        for (const auto& method : {std::string("stringify"), std::string("prettify"), std::string("highlight")}) {
            const std::string suffix = "." + method + "()";
            if (end >= suffix.size() && text.compare(end - suffix.size(), suffix.size(), suffix) == 0) {
                const std::size_t cut = end - suffix.size();
                if (!balanced_prefix(cut)) return false;
                end = cut; matched = true; stripped = true;
                if (method == "prettify") pretty = true;
                if (method == "highlight") highlight = true;
                break;
            }
        }
        if (!stripped) break;
    }
    if (!matched) return false;
    std::string raw = text.substr(0, end);
    std::size_t first = raw.find_first_not_of(" \t\r\n"); std::size_t last = raw.find_last_not_of(" \t\r\n");
    base = (first == std::string::npos) ? std::string() : raw.substr(first, last - first + 1);
    bool quoted = false; char qc = 0; int par = 0, br = 0, bc = 0;
    for (std::size_t i = 0; i < base.size(); ++i) {
        const char c = base[i];
        if (quoted) { if (c == '\\' && i + 1 < base.size()) ++i; else if (c == qc) quoted = false; continue; }
        if (c == '\'' || c == '"') { quoted = true; qc = c; continue; }
        if (c == '(') ++par; else if (c == ')') --par; else if (c == '[') ++br; else if (c == ']') --br; else if (c == '{') ++bc; else if (c == '}') --bc;
        if (quoted || par || br || bc) continue;
        if (std::string("=<>+-*/%?:,").find(c) != std::string::npos) return false;
        if (c == '&' && i + 1 < base.size() && base[i + 1] == '&') return false;
        if (c == '|' && i + 1 < base.size() && base[i + 1] == '|') return false;
    }
    return true;
}

std::string unescape_parameter_string(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            const char escaped = text[++i];
            if (escaped == '$') { result += '\\'; result += '$'; continue; }
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
            result += text[i];
        }
    }
    return result;
}

std::vector<std::string> parse_parameters(const std::string& text, bool& ok,
                                          std::vector<bool>* quoted) {
    std::vector<std::string> result; std::string current; bool in_quotes=false; char quote=0;
    bool parameter_was_quoted=false;
    int parens=0, brackets=0, braces=0; std::size_t significant_end=0; ok=true;
    auto append_parameter=[&]{
        current.resize(significant_end);
        // A parameter that is exactly one quoted string is a quoted parameter:
        // strip its outer quotes (applying the legacy in-string escape rules)
        // and record it. Quotes inside structured literals (objects/arrays)
        // are preserved so callables and validate() can accept {...}/[...] args.
        if (is_single_quoted_parameter(current)) {
            current = unescape_parameter_string(current.substr(1, current.size() - 2));
            parameter_was_quoted = true;
        } else {
            parameter_was_quoted = false;
        }
        result.push_back(current);
        if (quoted) quoted->push_back(parameter_was_quoted);
        current.clear(); significant_end=0; parameter_was_quoted=false; };
    for (std::size_t i=0;i<text.size();++i) {
        const char c=text[i];
        if (in_quotes) {
            if (c=='\\' && i+1<text.size()) { current+=c; current+=text[++i]; significant_end=current.size(); continue; }
            if (c==quote) { in_quotes=false; current+=c; significant_end=current.size(); }
            else { current+=c; significant_end=current.size(); }
            continue;
        }
        if (c=='\'' || c=='"') { in_quotes=true; quote=c; parameter_was_quoted=true; current+=c; significant_end=current.size(); continue; }
        if (c=='(') ++parens; else if (c==')') --parens; else if (c=='[') ++brackets; else if (c==']') --brackets; else if (c=='{') ++braces; else if (c=='}') --braces;
        if (parens<0 || brackets<0 || braces<0) { ok=false; return {}; }
        if (c==',' && parens==0 && brackets==0 && braces==0) { append_parameter(); continue; }
        if (std::isspace(static_cast<unsigned char>(c))) { if (!current.empty()) current+=c; }
        else { current+=c; significant_end=current.size(); }
    }
    if (in_quotes || parens || brackets || braces) { ok=false; return {}; }
    if (!text.empty() || !current.empty()) append_parameter();
    return result;
}

std::vector<SourceText> parse_parameters(const SourceText& text, bool& ok,
                                         std::vector<bool>* quoted) {
    std::vector<bool> flags;
    auto values = parse_parameters(static_cast<const std::string&>(text), ok, &flags);
    if (quoted) quoted->insert(quoted->end(), flags.begin(), flags.end());
    if (!ok) return {};
    std::vector<SourceText> result;
    std::size_t start = 0;
    int parens = 0, brackets = 0, braces = 0;
    char quote = 0;
    auto append = [&](std::size_t end) {
        auto begin = start, trimmed_end = end;
        while (begin < trimmed_end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
        while (trimmed_end > begin && std::isspace(static_cast<unsigned char>(text[trimmed_end-1]))) --trimmed_end;
        auto raw = text.substr(begin, trimmed_end - begin);
        const auto index = result.size();
        if (flags[index] && raw.view) {
            SourceBuilder builder(raw.view);
            builder.generated(values[index], 0, raw.size());
            auto mapped = std::move(builder).finish();
            result.emplace_back(std::move(mapped.text), std::move(mapped.view));
        } else result.emplace_back(std::move(values[index]), std::move(raw.view));
        start = end + 1;
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote) {
            if (c == '\\' && i + 1 < text.size()) ++i;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '\'' || c == '"') { quote = c; continue; }
        if (c == '(') ++parens; else if (c == ')') --parens;
        else if (c == '[') ++brackets; else if (c == ']') --brackets;
        else if (c == '{') ++braces; else if (c == '}') --braces;
        else if (c == ',' && !parens && !brackets && !braces) append(i);
    }
    if (!values.empty()) append(text.size());
    return result;
}

bool parse_callable_parameters(const std::string& text,
                               std::vector<std::string>& params,
                               std::string& variadic_param) {
    auto trim=[](const std::string& value){const auto first=value.find_first_not_of(" \t\r\n");if(first==std::string::npos)return std::string();const auto last=value.find_last_not_of(" \t\r\n");return value.substr(first,last-first+1);};
    bool ok = false;
    params = parse_parameters(text, ok);
    variadic_param.clear();
    if (!ok) return false;
    std::unordered_set<std::string> names;
    for (std::size_t i = 0; i < params.size(); ++i) {
        std::string param = trim(params[i]);
        if (param.rfind("...", 0) == 0) {
            if (!variadic_param.empty() || i + 1 != params.size() ||
                !valid_binding_identifier(trim(param.substr(3)))) return false;
            variadic_param = trim(param.substr(3));
            if (!names.insert(variadic_param).second) return false;
            params.pop_back();
            break;
        }
        if (!valid_binding_identifier(param) || !names.insert(param).second) return false;
        params[i] = std::move(param);
    }
    return true;
}

bool reserved_binding_name(const std::string& name) {
    static const std::unordered_set<std::string> names = {
        "title", "name", "content-path", "output-path", "template-path",
        "build-timezone", "build-time", "build-UTC-time", "build-date",
        "build-UTC-date", "build-YYYY", "build-YY", "build-OS",
        // `loop` is the lexical metadata object injected by @for. Reserving it
        // prevents user data aliases from becoming ambiguous with $[loop.*].
        "loop"
    };
    return names.count(name) != 0;
}

bool built_in_metadata_name(const std::string& name) {
    static const std::unordered_set<std::string> names = {
        "title", "name", "content-path", "output-path", "template-path",
        "build-timezone", "build-time", "build-UTC-time", "build-date",
        "build-UTC-date", "build-YYYY", "build-YY", "build-OS"
    };
    return names.count(name) != 0;
}

bool parse_for_collection_clause(const std::string& clause,
                                 std::string& collection_expression,
                                 std::string& sort_expression,
                                 bool& descending,
                                 std::string& error) {
    bool quoted=false; char quote=0; int parens=0, brackets=0; std::size_t by=std::string::npos;
    for(std::size_t i=0;i+4<=clause.size();++i){char c=clause[i]; if(quoted){if(c=='\\')++i;else if(c==quote)quoted=false;continue;} if(c=='\''||c=='"'){quoted=true;quote=c;continue;} if(c=='(')++parens;else if(c==')')--parens;else if(c=='[')++brackets;else if(c==']')--brackets; if(!parens&&!brackets&&clause.compare(i,4," by ")==0){by=i;break;}}
    if(by==std::string::npos){collection_expression=clause;sort_expression.clear();descending=false;return true;}
    collection_expression=clause.substr(0,by); std::string tail=clause.substr(by+4);
    const auto space=tail.find_last_of(" \t"); if(space==std::string::npos){error="@for sorting syntax is @for(item : collection by item.field asc|desc){...}";return false;}
    sort_expression=tail.substr(0,space); const std::string direction=tail.substr(space+1); if(direction!="asc"&&direction!="desc"){error="@for sorting syntax is @for(item : collection by item.field asc|desc){...}";return false;} descending=direction=="desc"; return true;
}

std::shared_ptr<const nift::RuntimeValue> make_loop_metadata(std::size_t index,
                                                             std::size_t length) {
    auto loop = std::make_shared<nift::RuntimeValue>(nift::RuntimeValue::make_object());
    (*loop)["index"] = nift::RuntimeValue(static_cast<double>(index + 1));
    (*loop)["index0"] = nift::RuntimeValue(static_cast<double>(index));
    (*loop)["first"] = nift::RuntimeValue(index == 0);
    (*loop)["last"] = nift::RuntimeValue(index + 1 == length);
    (*loop)["length"] = nift::RuntimeValue(static_cast<double>(length));
    return loop;
}

bool sortable_scalar(const nift::RuntimeValue& value) {
    return value.is_number() || value.is_string();
}

int compare_sort_keys(const nift::RuntimeValue& left, const nift::RuntimeValue& right) {
    if (left.is_number()) return nift::runtime_compare_numbers(left, right);
    return left.string < right.string ? -1 : (left.string > right.string ? 1 : 0);
}

ControlBlockBody normalize_control_block_body(std::string body, SourceView view) {
    if(!view)view=SourceView::identity({},body);
    ControlBlockBody result;

    // A block written in the usual form
    //
    //   @if(...) {
    //       ...
    //   }
    //
    // uses the whitespace immediately inside the braces for source readability,
    // not as output indentation. Strip that structural first/last line, then
    // remove the common indentation from the remaining non-empty lines.
    std::size_t first = 0;
    while (first < body.size() && (body[first] == ' ' || body[first] == '\t')) ++first;
    if (first < body.size() && (body[first] == '\n' || body[first] == '\r')) {
        result.multiline = true;
        if (body[first] == '\r' && first + 1 < body.size() && body[first + 1] == '\n') ++first;
        body.erase(0, first + 1);
        view=view.slice(first+1);

        while (!body.empty() && (body.back() == ' ' || body.back() == '\t')) body.pop_back();
        if (!body.empty() && body.back() == '\n') {
            body.pop_back();
            if (!body.empty() && body.back() == '\r') body.pop_back();
        }

        view=view.slice(0,body.size());
        std::size_t common = std::string::npos;
        std::size_t line_start = 0;
        while (line_start <= body.size()) {
            const std::size_t line_end = body.find('\n', line_start);
            const std::size_t end = line_end == std::string::npos ? body.size() : line_end;
            std::size_t pos = line_start;
            while (pos < end && (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\r')) ++pos;
            if (pos < end) common = std::min(common, pos - line_start);
            if (line_end == std::string::npos) break;
            line_start = line_end + 1;
        }

        if (common != std::string::npos && common > 0) {
            SourceBuilder mapped(view);
            std::string dedented;
            dedented.reserve(body.size());
            line_start = 0;
            while (line_start <= body.size()) {
                const std::size_t line_end = body.find('\n', line_start);
                const std::size_t end = line_end == std::string::npos ? body.size() : line_end;
                std::size_t remove = 0;
                while (remove < common && line_start + remove < end &&
                       (body[line_start + remove] == ' ' || body[line_start + remove] == '\t')) {
                    ++remove;
                }
                dedented.append(body, line_start + remove, end - (line_start + remove));
                mapped.copy(body,line_start+remove,end-line_start-remove);
                if (line_end == std::string::npos) break;
                dedented += '\n';
                mapped.copy(body,end,1);
                line_start = line_end + 1;
            }
            body = std::move(dedented);
            view=std::move(mapped).finish().view;
        }
    }

    result.text = std::move(body);
    result.view=std::move(view);
    return result;
}

std::string insertion_indent(const std::string& output) {
    const auto previous_newline = output.find_last_of('\n');
    const std::string current_line = previous_newline == std::string::npos
        ? output : output.substr(previous_newline + 1);
    std::string indent = current_line;
    for (char c : current_line) {
        if (c != ' ' && c != '\t') {
            indent.assign(current_line.size(), ' ');
            break;
        }
    }
    return indent;
}

void append_indented(std::string& output, const std::string& text, const std::string& indent,
                     int initial_code_block_depth, bool trim_trailing_newline) {
    std::size_t size = text.size();
    if (trim_trailing_newline && size && text[size - 1] == '\n') {
        --size;
        if (size && text[size - 1] == '\r') --size;
    }
    if (size == 0) return;

    int code_block_depth = initial_code_block_depth;
    int html_comment_depth = 0;

    auto is_pre_open = [&](std::size_t pos) {
        if (pos + 4 > size || text.compare(pos, 4, "<pre") != 0) return false;
        if (pos + 4 == size) return false;
        const char next = text[pos + 4];
        return next == '>' || next == ' ' || next == '\t' || next == '\r' || next == '\n';
    };
    auto is_pre_close = [&](std::size_t pos) {
        return pos + 6 <= size && text.compare(pos, 6, "</pre>") == 0;
    };

    std::size_t segment_start = 0;
    for (std::size_t i = 0; i < size; ++i) {
        if (text.compare(i, 4, "<!--") == 0) {
            ++html_comment_depth;
        } else if (text.compare(i, 3, "-->") == 0 && html_comment_depth > 0) {
            --html_comment_depth;
        }

        if (html_comment_depth == 0) {
            if (is_pre_close(i) && code_block_depth > 0) {
                --code_block_depth;
            } else if (is_pre_open(i)) {
                ++code_block_depth;
            }
        }

        if (text[i] != '\n') continue;

        output.append(text, segment_start, i - segment_start + 1);
        if (i + 1 < size && code_block_depth == 0) output += indent;
        segment_start = i + 1;
    }

    if (segment_start < size)
        output.append(text, segment_start, size - segment_start);
}

std::string entity(const std::string& value, bool& ok) {
    static const std::vector<std::pair<std::string, std::string>> entities = {
        {"`", "&grave;"}, {"~", "&tilde;"}, {"!", "&excl;"}, {"@", "&commat;"},
        {"#", "&num;"}, {"$", "&dollar;"}, {"%", "&percnt;"}, {"^", "&Hat;"},
        {"&", "&amp;"}, {"*", "&ast;"}, {"?", "&quest;"}, {"<", "&lt;"},
        {">", "&gt;"}, {"(", "&lpar;"}, {")", "&rpar;"}, {"[", "&lbrack;"},
        {"]", "&rbrack;"}, {"{", "&lbrace;"}, {"}", "&rbrace;"}, {"-", "&minus;"},
        {"_", "&lowbar;"}, {"=", "&equals;"}, {"+", "&plus;"}, {"|", "&vert;"},
        {"\\", "&bsol;"}, {"/", "&sol;"}, {";", "&semi;"}, {":", "&colon;"},
        {"'", "&apos;"}, {"\"", "&quot;"}, {",", "&comma;"}, {".", "&period;"},
        {"£", "&pound;"}, {"¥", "&yen;"}, {"€", "&euro;"}, {"section", "&sect;"},
        {"+-", "&pm;"}, {"-+", "&mp;"}, {"!=", "&ne;"}, {"<=", "&leq;"},
        {">=", "&geq;"}, {"->", "&rarr;"}, {"<-", "&larr;"}, {"<->", "&harr;"},
        {"==>", "&rArr;"}, {"<==", "&lArr;"}, {"<==>", "&hArr;"},
        {"<=!=>", "&nhArr;"}, {"...", "&hellip;"}
    };
    for (const auto& [key, encoded] : entities) {
        if (key == value) { ok = true; return encoded; }
    }
    ok = false;
    return {};
}

}  // namespace nift::detail
