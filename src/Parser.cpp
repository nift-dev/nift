#include "JsonFile.h"
#include "Parser.h"
#include "FrontMatter.h"
#include "Console.h"
#include "FileSystem.h"
#include "Proc.h"
#include "ProjectInfo.h"
#include "Automation.h"
#include "Ast.h"
#include "Json.h"
#include "JsonSchema.h"
#include "RenderHost.h"
#include <markup/Markup.h>
#include <minify/Minify.h>

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
#include <set>
#include <thread>
#include <atomic>
#include <charconv>
#include <limits>

namespace fs = std::filesystem;
static int nift_binding_type(const json::Document& value) {
    if (value.is_null()) return 0;
    if (value.is_bool()) return 1;
    if (value.is_number()) return std::trunc(value.num) == value.num ? 2 : 3;
    if (value.is_string()) return 4;
    if (value.is_array()) return 5;
    if (value.is_object()) return 6;
    return -1;
}

static const char* nift_binding_type_name(int type) {
    switch (type) { case 0:return "null"; case 1:return "bool"; case 2:return "int"; case 3:return "double"; case 4:return "string"; case 5:return "array"; case 6:return "json"; default:return "unknown"; }
}

// An int value may be assigned to a double binding (widening: a double binding
// represents any numeric value under Nift's arithmetic model, which already
// computes int + double as double). The reverse (double -> int) stays an error
// because it is lossy.
static bool nift_type_assignable(int from, int to) {
    return from == to || (from == 2 && to == 3);
}

static const int kMaxCallableDepth = 64;

namespace {

static bool nift_glob_has_magic(const std::string& s) {
    bool escaped=false; for(char c:s){if(escaped){escaped=false;continue;}if(c=='\\'){escaped=true;continue;}if(c=='*'||c=='?')return true;} return false;
}
static bool nift_glob_component_match(const std::string& pattern,const std::string& name){
    if(!name.empty()&&name[0]=='.'&&(pattern.empty()||pattern[0]!='.'))return false;
    std::size_t p=0,n=0,star=std::string::npos,mark=0; bool escaped=false;
    while(n<name.size()){
        if(p<pattern.size()&&pattern[p]=='\\'&&!escaped){if(p+1<pattern.size()){++p;if(pattern[p]==name[n]){++p;++n;continue;}}}
        else if(p<pattern.size()&&pattern[p]=='?'){++p;++n;continue;}
        else if(p<pattern.size()&&pattern[p]=='*'){star=p++;mark=n;continue;}
        else if(p<pattern.size()&&pattern[p]==name[n]){++p;++n;continue;}
        if(star!=std::string::npos){p=star+1;n=++mark;continue;}return false;
    }
    while(p<pattern.size()&&pattern[p]=='*')++p;return p==pattern.size();
}
static void nift_glob_walk(const fs::path& base,const std::vector<std::string>& parts,std::size_t i,std::vector<fs::path>& out){
    if(i==parts.size()){std::error_code ec;if(fs::exists(base,ec)&&!ec)out.push_back(fs::absolute(base).lexically_normal());return;}
    const auto& part=parts[i];
    if(part=="**"){
        nift_glob_walk(base,parts,i+1,out);
        std::error_code ec; if(!fs::is_directory(base,ec)||ec)return;
        std::vector<fs::directory_entry> entries; for(fs::directory_iterator it(base,fs::directory_options::skip_permission_denied,ec),end;!ec&&it!=end;it.increment(ec))entries.push_back(*it);
        std::sort(entries.begin(),entries.end(),[](const auto&a,const auto&b){return a.path().generic_string()<b.path().generic_string();});
        for(const auto& e:entries){auto name=e.path().filename().string();if(!name.empty()&&name[0]=='.')continue;std::error_code sec;if(e.is_directory(sec)&&!e.is_symlink(sec))nift_glob_walk(e.path(),parts,i,out);}
        return;
    }
    if(!nift_glob_has_magic(part)){std::string literal;literal.reserve(part.size());for(std::size_t k=0;k<part.size();++k){if(part[k]=='\\'&&k+1<part.size())literal+=part[++k];else literal+=part[k];}nift_glob_walk(base/literal,parts,i+1,out);return;}
    std::error_code ec;if(!fs::is_directory(base,ec)||ec)return;std::vector<fs::directory_entry> entries;for(fs::directory_iterator it(base,fs::directory_options::skip_permission_denied,ec),end;!ec&&it!=end;it.increment(ec))entries.push_back(*it);
    std::sort(entries.begin(),entries.end(),[](const auto&a,const auto&b){return a.path().generic_string()<b.path().generic_string();});for(const auto&e:entries)if(nift_glob_component_match(part,e.path().filename().string()))nift_glob_walk(e.path(),parts,i+1,out);
}
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
static std::vector<fs::path> nift_glob_expand(const fs::path& resolved_pattern){
    std::string g=resolved_pattern.generic_string();fs::path root=resolved_pattern.root_path();std::string rel=root.empty()?g:g.substr(root.generic_string().size());while(!rel.empty()&&rel.front()=='/')rel.erase(rel.begin());std::vector<std::string> parts;std::stringstream ss(rel);std::string part;while(std::getline(ss,part,'/'))if(!part.empty())parts.push_back(part);std::vector<fs::path> out;nift_glob_walk(root.empty()?fs::path("."):root,parts,0,out);std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.generic_string()<b.generic_string();});out.erase(std::unique(out.begin(),out.end()),out.end());return out;
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

}  // namespace

// Nift source numeric literals preserve their lexical int/double form: 0, 8
// are int; 0.0, 8.5, 1e3 are double even when the value is integral. An
// arithmetic expression containing any double-typed operand is double; binding
// references propagate their inferred type so `total = total + value` stays
// double even when the accumulated value is integral. Everything else
// (computed expressions, JSON, injection) keeps value-based classification; a
// parsed JSON number keeps its established representation.
int Parser::nift_binding_type_from_text(const std::string& source,
                                        const json::Document& value) const {
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


namespace {
bool is_single_quoted_parameter(const std::string& text) {
    if (text.size() < 2 || (text.front() != '\'' && text.front() != '"')) return false;
    const char outer = text.front();
    for (std::size_t i = 1; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) { ++i; continue; }
        if (text[i] == outer) return i + 1 == text.size();
    }
    return false;
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
                                          std::vector<bool>* quoted = nullptr) {
    std::vector<std::string> result; std::string current; bool in_quotes=false; char quote=0;
    bool parameter_was_quoted=false;
    int parens=0, brackets=0, braces=0; std::size_t significant_end=0; ok=true;
    auto append_parameter=[&]{
        current.resize(significant_end);
        // A parameter that is exactly one quoted string is a quoted parameter:
        // strip its outer quotes (applying the legacy in-string escape rules)
        // and record it. Quotes inside structured literals (objects/arrays)
        // are preserved so callables and validate() can accept {…}/[…] args.
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

bool valid_binding_identifier(const std::string& name) {
    if (name.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) return false;
    return std::all_of(name.begin() + 1, name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    });
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
std::shared_ptr<const json::Document> make_loop_metadata(std::size_t index,
                                                         std::size_t length) {
    auto loop = std::make_shared<json::Document>(json::Document::make_object());
    (*loop)["index"] = json::Document(static_cast<double>(index + 1));
    (*loop)["index0"] = json::Document(static_cast<double>(index));
    (*loop)["first"] = json::Document(index == 0);
    (*loop)["last"] = json::Document(index + 1 == length);
    (*loop)["length"] = json::Document(static_cast<double>(length));
    return loop;
}

bool sortable_scalar(const json::Document& value) {
    return value.is_number() || value.is_string();
}

int compare_sort_keys(const json::Document& left, const json::Document& right) {
    if (left.is_number()) return left.num < right.num ? -1 : (left.num > right.num ? 1 : 0);
    return left.string < right.string ? -1 : (left.string > right.string ? 1 : 0);
}

struct ControlBlockBody {
    std::string text;
    bool multiline = false;
};

ControlBlockBody normalize_control_block_body(std::string body) {
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

        while (!body.empty() && (body.back() == ' ' || body.back() == '\t')) body.pop_back();
        if (!body.empty() && body.back() == '\n') {
            body.pop_back();
            if (!body.empty() && body.back() == '\r') body.pop_back();
        }

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
                if (line_end == std::string::npos) break;
                dedented += '\n';
                line_start = line_end + 1;
            }
            body = std::move(dedented);
        }
    }

    result.text = std::move(body);
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
                     int initial_code_block_depth = 0, bool trim_trailing_newline = true) {
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

Parser::Parser(RenderHost& host, TrackedInfo& tracked_info)
    : host_(host), tracked_info_(tracked_info) {
    variable_scopes_.emplace_back();
    if (!tracked_info_.name.empty()) {
        json::Document m=json::Document::make_object(); bool from_project=false; std::string page_metadata_error;
        // Per-page model-equivalent metadata (front matter + type + schema
        // validation) computed without constructing the project-wide query
        // model, so an ordinary build never pays for project features it does
        // not use. Hosts without the content model fall back to direct parsing.
        if (host_.page_project_metadata(tracked_info_, m, page_metadata_error)) from_project = true;
        if(!from_project){std::string e;auto cp=host_.content_path(tracked_info_);if(filesystem::file_exists(cp)){auto parsed=frontmatter::parse_inline(filesystem::read_file(cp));if(tracked_info_.frontmatter&&parsed.present)m["_error"]=json::Document("multiple front matter sources");else if(tracked_info_.frontmatter){frontmatter::load_external(host_.root(),*tracked_info_.frontmatter,m,e);if(!e.empty())m["_error"]=json::Document(e);}else if(parsed.present&&!parsed.error.empty())m["_error"]=json::Document(parsed.error);else if(parsed.present)m=parsed.value;}}
        json_bindings_["frontmatter"]=std::make_shared<const json::Document>(std::move(m));
        // Current page for hierarchy composition (page.parent etc.). The
        // reference is constructed directly without touching the hierarchy
        // index, which is built lazily only when a hierarchy member is
        // actually accessed (pay-for-use).
        if (!tracked_info_.name.empty()) {
            auto page_sp=std::make_shared<json::Document>(json::Document(std::string("\x1fnift:page:")+tracked_info_.name));
            variable_scopes_.back().emplace("page", VariableBinding{page_sp, nift_binding_type(*page_sp), false, false});
        }
    }
}

bool Parser::eval_expression(const std::string& expression, json::Document& value, std::string& error) {
    standalone_script_host_ = true;
    return evaluate_expression(expression, value, error);
}

bool Parser::finalize_script_resources(std::string& error) {
    std::vector<std::string> open_paths;
    for (auto& kv : file_instances_) {
        auto& f = *kv.second;
        if (!f.open) continue;
        open_paths.push_back(f.path.generic_string() + (f.dirty ? " (unsaved changes discarded)" : ""));
        f.open = false; f.dirty = false; f.mode.clear(); f.working.clear(); f.saved.clear(); f.cursor = 0;
    }
    if (open_paths.empty()) return true;
    error = "managed file left open";
    if (open_paths.size() > 1) error += " (" + std::to_string(open_paths.size()) + " files)";
    error += ": " + open_paths.front();
    return false;
}

void Parser::fail(const fs::path& source_path, const std::string& source, std::size_t offset, const std::string& message) {
    result_.ok = false;
    result_.error.tracked_name = tracked_info_.name;
    result_.error.source_file = source_path;
    result_.error.line = 1;

    std::size_t line_start = 0;
    for (std::size_t i = 0; i < offset && i < source.size(); ++i) {
        if (source[i] == '\n') {
            ++result_.error.line;
            line_start = i + 1;
        }
    }

    result_.error.column = offset >= line_start ? offset - line_start + 1 : 1;
    result_.error.source_length = 1;
    if (offset < source.size() && source[offset] == '@') {
        std::size_t name_end = offset + 1;
        while (name_end < source.size() && source[name_end] >= 'a' && source[name_end] <= 'z') ++name_end;
        std::size_t span_end = name_end;
        if (name_end < source.size() && source[name_end] == '(') {
            std::size_t close = 0;
            if (find_balanced(source, name_end, '(', ')', close)) span_end = close + 1;
        }
        result_.error.source_length = std::max<std::size_t>(1, span_end - offset);
    } else if (offset + 1 < source.size() && source[offset] == '$' && source[offset + 1] == '[') {
        const auto close = source.find(']', offset + 2);
        if (close != std::string::npos) result_.error.source_length = close + 1 - offset;
    }
    const std::size_t line_end = source.find('\n', line_start);
    result_.error.source_line = source.substr(
        line_start,
        line_end == std::string::npos ? std::string::npos : line_end - line_start);
    if (!result_.error.source_line.empty() && result_.error.source_line.back() == '\r')
        result_.error.source_line.pop_back();

    result_.error.message = message;
}

std::string Parser::metadata(const std::string& key) const {
    if (key == "title") return tracked_info_.title;
    if (key == "name") return tracked_info_.name;
    if (key == "content-path") return host_.relative(host_.content_path(tracked_info_));
    if (key == "output-path") return host_.relative(host_.output_path(tracked_info_));
    if (key == "template-path") return tracked_info_.template_path;

    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local = *std::localtime(&now);
    std::tm utc = *std::gmtime(&now);
    char buffer[64] = {};
    auto format = [&](const char* pattern, const std::tm& tm) {
        std::strftime(buffer, sizeof buffer, pattern, &tm);
        return std::string(buffer);
    };
    if (key == "build-time") return format("%H:%M:%S", local);
    if (key == "build-date") return format("%Y-%m-%d", local);
    if (key == "build-UTC-time") return format("%H:%M:%S", utc);
    if (key == "build-UTC-date") return format("%Y-%m-%d", utc);
    if (key == "build-YYYY") return format("%Y", local);
    if (key == "build-YY") return format("%y", local);
    if (key == "build-timezone") return format("%Z", local);
#if defined(_WIN32)
    if (key == "build-OS") return "Windows";
#elif defined(__APPLE__)
    if (key == "build-OS") return "macOS";
#else
    if (key == "build-OS") return "Linux";
#endif
    return {};
}



void Parser::push_variable_scope() { variable_scopes_.emplace_back(); }
void Parser::pop_variable_scope() { if (variable_scopes_.size() > 1) variable_scopes_.pop_back(); }

void Parser::push_json_scope() {
    json_binding_scopes_.emplace_back();
    push_variable_scope();
}

void Parser::pop_json_scope() {
    if (json_binding_scopes_.empty()) return;
    for (const auto& name : json_binding_scopes_.back()) json_bindings_.erase(name);
    json_binding_scopes_.pop_back();
    pop_variable_scope();
}

std::string Parser::trim_copy(const std::string& text) const {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool Parser::find_balanced(const std::string& source,
                           std::size_t open_position,
                           char open_char,
                           char close_char,
                           std::size_t& close_position) const {
    if (open_position >= source.size() || source[open_position] != open_char) return false;

    std::size_t depth = 0;
    bool quoted = false;
    char quote = 0;

    for (std::size_t i = open_position; i < source.size(); ++i) {
        const char c = source[i];
        if (quoted) {
            if (c == '\\' && i + 1 < source.size()) {
                ++i;
            } else if (c == quote) {
                quoted = false;
            }
            continue;
        }

        if (c == '\'' || c == '"') {
            quoted = true;
            quote = c;
            continue;
        }

        // Code spans and fenced blocks (backtick-delimited) are not template
        // structure: braces inside Markdown code samples or AsciiDoc/RST
        // literal backtick spans must not terminate a surrounding @markup or
        // @json block. An unterminated run is treated as literal text.
        if (c == '`') {
            std::size_t run = 1;
            while (i + run < source.size() && source[i + run] == '`') ++run;
            const std::string delimiter(source, i, run);
            const auto span_end = source.find(delimiter, i + run);
            if (span_end != std::string::npos) {
                i = span_end + run - 1;
            }
            continue;
        }

        // Comment bodies are not template structure. In particular, a brace in
        // a source/HTML comment must not terminate a surrounding @if/@for block.
        if (source.compare(i, 3, "@/*") == 0) {
            const auto comment_end = source.find("*/", i + 3);
            if (comment_end == std::string::npos) return false;
            i = comment_end + 1;
            continue;
        }
        if (source.compare(i, 4, "<!--") == 0) {
            const auto comment_end = source.find("-->", i + 4);
            if (comment_end == std::string::npos) return false;
            i = comment_end + 2;
            continue;
        }
        if (source.compare(i, 3, "@//") == 0) {
            const auto line_end = source.find('\n', i);
            if (line_end == std::string::npos) return false;
            i = line_end;
            continue;
        }

        if (c == open_char) {
            ++depth;
        } else if (c == close_char) {
            if (--depth == 0) {
                close_position = i;
                return true;
            }
        }
    }
    return false;
}

bool Parser::resolve_json_value(const std::string& expression,
                                std::shared_ptr<const json::Document>& value,
                                std::string& error) {
    std::size_t position = 0;

    auto identifier = [&](std::string& result) -> bool {
        if (position >= expression.size()) return false;
        const unsigned char first = static_cast<unsigned char>(expression[position]);
        if (!(std::isalpha(first) || expression[position] == '_')) return false;
        const std::size_t start = position++;
        while (position < expression.size()) {
            const unsigned char c = static_cast<unsigned char>(expression[position]);
            if (!(std::isalnum(c) || expression[position] == '_')) break;
            ++position;
        }
        result = expression.substr(start, position - start);
        return true;
    };

    std::string root_name;
    if (!identifier(root_name)) return false;

    std::shared_ptr<const json::Document> current;
    bool contract_binding = false;
    // Host-supplied bindings (Embedded Nift engine defaults / Context overlays)
    // resolve before @json bindings, contracts and built-in metadata.
    VariableBinding* local_binding=nullptr;
    for(auto scope=variable_scopes_.rbegin();scope!=variable_scopes_.rend();++scope){auto it=scope->find(root_name);if(it!=scope->end()){local_binding=&it->second;break;}}
    if (local_binding && local_binding->value && (local_binding->value->is_object() || local_binding->value->is_array())) {
        current = local_binding->value;
    } else if (const auto* supplied = host_.binding(root_name)) {
        current = *supplied;
        if (root_name == "project") {
            result_.dependencies.insert(host_.relative(host_.root() / ".nift/config.json"));
            result_.dependencies.insert(host_.relative(host_.root() / ".nift/tracked.json"));
            // Compact semantic project dependency: the whole project model is
            // captured by a single fingerprint file that is rewritten only when
            // the model changes, so a project-using page records O(1) deps
            // instead of one entry per tracked file.
            result_.dependencies.insert(host_.relative(host_.root() / ".nift/project.fingerprint"));
        }
    } else {
        const auto binding = json_bindings_.find(root_name);
        if (binding != json_bindings_.end()) {
            current = binding->second;
        } else {
            const std::string* contract_source = host_.contract_source(root_name);
            if (!contract_source) return false;
            contract_binding = true;

            const std::string& contract_path_argument = *contract_source;
            const fs::path contract_path = (host_.root() / contract_path_argument).lexically_normal();
            if (!filesystem::path_within(host_.root(), contract_path)) {
                error = "contract '" + root_name + "': path must stay inside the Nift project: " +
                        contract_path_argument;
                return true;
            }
            if (!host_.source_exists(contract_path)) {
                error = "contract '" + root_name + "': file does not exist: " + contract_path_argument;
                return true;
            }

            const auto cached = contract_bindings_.find(root_name);
            if (cached != contract_bindings_.end()) {
                current = cached->second;
            } else {
                std::string contract_error;
                auto document = host_.read_shared_json(contract_path, contract_error);
                if (!document) {
                    error = "contract '" + root_name + "': failed to parse " + contract_path_argument +
                            (contract_error.empty() ? "" : " (" + contract_error + ")");
                    return true;
                }
                contract_bindings_.emplace(root_name, document);
                current = std::move(document);
            }

            result_.dependencies.insert(host_.relative(host_.root() / ".nift/config.json"));
            result_.dependencies.insert(host_.relative(contract_path));
        }
    }

    while (position < expression.size()) {
        if (expression[position] == '.') {
            ++position;
            std::string member;
            if (!identifier(member)) {
                error = "invalid JSON member access in '" + expression + "'";
                return true;
            }
            if (current->is_string() && current->string.rfind("\x1fnift:page:",0)==0) {
                result_.dependencies.insert(host_.relative(host_.root() / ".nift/hierarchy.fingerprint"));
                json::Document resolved; std::string perr;
                if (!host_.resolve_page_member(current->string.substr(11), member, resolved, perr)) {
                    error = perr.empty() ? ("page has no member: " + member) : perr;
                    return true;
                }
                current = std::make_shared<const json::Document>(std::move(resolved));
                continue;
            }
            if (!current->is_object()) {
                error = "cannot access member '" + member + "' because the current JSON value is not an object";
                return true;
            }
            if (!current->has(member)) {
                if (contract_binding) {
                    const std::string key = expression.size() > root_name.size() + 1
                        ? expression.substr(root_name.size() + 1) : std::string{};
                    error = "contract '" + root_name + "' has no entry '" + key + "'";
                } else {
                    error = "JSON value '" + expression.substr(0, position - member.size() - 1) +
                            "' has no member '" + member + "'";
                }
                return true;
            }

            const json::Document* child = &(*current)[member];
            current = std::shared_ptr<const json::Document>(current, child);
            continue;
        }

        if (expression[position] == '[') {
            ++position; const std::size_t token_start=position; bool quoted=false; char quote=0;
            if(position<expression.size()&&(expression[position]=='"'||expression[position]=='\'')){quoted=true;quote=expression[position++];while(position<expression.size()&&expression[position]!=quote){if(expression[position]=='\\'&&position+1<expression.size())position+=2;else ++position;}if(position>=expression.size()){error="unterminated JSON object key in '"+expression+"'";return true;}++position;}else while(position<expression.size()&&expression[position]!=']')++position;
            if(position>=expression.size()||expression[position]!=']'){error="JSON index has no closing ']' in '"+expression+"'";return true;}std::string token=trim_copy(expression.substr(token_start,position-token_start));++position;
            if(current->is_array()){
                std::size_t index=0;
                if(quoted||token.empty()||!std::all_of(token.begin(),token.end(),[](unsigned char c){return std::isdigit(c); })){
                    if(quoted||token.empty()){
                        error="JSON array indices must be non-negative integers in '"+expression+"'";return true;
                    }
                    json::Document computed;
                    if(!evaluate_expression(token,computed,error)||!computed.is_number()||computed.num<0||std::trunc(computed.num)!=computed.num){
                        if(error.empty())error="JSON array indices must be non-negative integers in '"+expression+"'";return true;
                    }
                    index=(std::size_t)computed.num;
                }else{try{index=(std::size_t)std::stoull(token);}catch(...){error="JSON array index is out of range in '"+expression+"'";return true;}}
                if(index>=current->array.size()){error="JSON array index "+std::to_string(index)+" is out of range in '"+expression+"'";return true;}const json::Document* child=&(*current)[index];current=std::shared_ptr<const json::Document>(current,child);continue;
            }
            if(current->is_object()){
                std::string key;if(quoted){if(token.size()<2){error="invalid JSON object key";return true;}key=token.substr(1,token.size()-2);}else{VariableBinding* kb=nullptr;for(auto scope=variable_scopes_.rbegin();scope!=variable_scopes_.rend();++scope){auto it=scope->find(token);if(it!=scope->end()){kb=&it->second;break;}}if(!kb||!kb->value||!kb->value->is_string()){
                    json::Document computed;
                    if(!evaluate_expression(token,computed,error)||!computed.is_string()){if(error.empty())error="JSON object index must be a quoted string, string binding, or string expression in '"+expression+"'";return true;}
                    key=computed.string;
                }else{key=kb->value->string;}}if(!current->has(key)){error="JSON object has no key '"+key+"'";return true;}const json::Document* child=&(*current)[key];current=std::shared_ptr<const json::Document>(current,child);continue;
            }
            error="cannot index JSON value in '"+expression+"'";return true;
        }

        error = "invalid JSON access syntax in '" + expression + "'";
        return true;
    }

    value = std::move(current);
    return true;
}

bool Parser::json_value(const std::string& expression, std::string& value, std::string& error) {
    std::shared_ptr<const json::Document> document;
    if (!resolve_json_value(expression, document, error)) return false;
    if (!error.empty()) return true;

    if (document->is_string()) {
        value = document->string;
    } else if (document->is_number() || document->is_bool() || document->is_null()) {
        value = document->dump(0);
    } else if (document->is_array()) {
        error = "cannot render JSON array $[" + expression + "]; select an element first";
    } else if (document->is_object()) {
        error = "cannot render JSON object $[" + expression + "]; select a member first";
    }
    return true;
}

bool Parser::interpolate_parameter(const std::string& parameter,
                                   std::string& resolved,
                                   std::string& error) {
    resolved.clear();
    resolved.reserve(parameter.size());

    for (std::size_t i = 0; i < parameter.size();) {
        if (parameter[i] == '\\' && i + 1 < parameter.size() && parameter[i + 1] == '$') {
            resolved.push_back('$');
            i += 2;
            continue;
        }
        if (parameter.compare(i, 2, "$[") != 0) {
            const auto next = parameter.find_first_of("\\$", i + 1);
            const std::size_t end = next == std::string::npos ? parameter.size() : next;
            resolved.append(parameter, i, end - i);
            i = end;
            continue;
        }

        std::size_t end = i + 2;
        std::size_t nested_brackets = 0;
        for (; end < parameter.size(); ++end) {
            if (parameter[end] == '[') {
                ++nested_brackets;
            } else if (parameter[end] == ']') {
                if (nested_brackets == 0) break;
                --nested_brackets;
            }
        }
        if (end == parameter.size()) {
            error = "unterminated parameter value expression";
            return false;
        }

        const std::string expression = parameter.substr(i + 2, end - i - 2);
        json::Document expression_value;
        std::string expression_error;
        if (evaluate_expression(expression, expression_value, expression_error)) {
            if (expression_value.is_array() || expression_value.is_object()) {
                error = "parameter expression must resolve to a scalar value";
                return false;
            }
            resolved += render_expression_value(expression_value);
        } else if (!expression_error.empty() && expression_error.rfind("unknown value or malformed expression:", 0) != 0) {
            error = expression_error;
            return false;
        } else if (built_in_metadata_name(expression)) {
            resolved += metadata(expression);
        } else {
            std::string value;
            std::string value_error;
            if (!json_value(expression, value, value_error)) {
                error = "unknown parameter value $[" + expression + "]";
                return false;
            }
            if (!value_error.empty()) {
                error = std::move(value_error);
                return false;
            }
            resolved += value;
        }
        i = end + 1;
    }
    return true;
}

bool Parser::resolve_pagination_value(const std::string& expression,
                                      std::shared_ptr<const json::Document>& value) const {
    if (!pagination_context_active_) return false;
    if (expression == "paginate.items") value = std::make_shared<const json::Document>(pagination_items_text_);
    else if (expression == "paginate.current") value = std::make_shared<const json::Document>(static_cast<double>(pagination_current_));
    else if (expression == "paginate.total") value = std::make_shared<const json::Document>(static_cast<double>(pagination_total_));
    else if (expression == "paginate.first") value = std::make_shared<const json::Document>(pagination_current_ == 1);
    else if (expression == "paginate.last") value = std::make_shared<const json::Document>(pagination_current_ == pagination_total_);
    else if (expression == "paginate.previous") value = std::make_shared<const json::Document>(static_cast<double>(pagination_current_ > 1 ? pagination_current_ - 1 : 1));
    else if (expression == "paginate.next") value = std::make_shared<const json::Document>(static_cast<double>(pagination_current_ < pagination_total_ ? pagination_current_ + 1 : pagination_total_));
    else return false;
    return true;
}

std::string Parser::path_to_page(std::size_t page) {
    if (!pagination_context_active_ || !tracked_info_.paginate.has_value()) {
        result_.ok = false;
        result_.error = {tracked_info_.name, {}, 0, "pathtopage: only available while rendering pagination"};
        return {};
    }
    if (page < 1 || page > pagination_total_) {
        result_.ok = false;
        result_.error = {tracked_info_.name, {}, 0, "pathtopage: page must be between 1 and " + std::to_string(pagination_total_)};
        return {};
    }
    const fs::path destination = host_.pagination_output_path(tracked_info_, page);
    const fs::path base = pagination_current_output_.empty()
        ? host_.output_path(tracked_info_).parent_path()
        : pagination_current_output_.parent_path();
    if (page == 1 && (tracked_info_.name == "/" || (!tracked_info_.name.empty() && tracked_info_.name.back() == '/'))) {
        fs::path relative_path = destination.parent_path().lexically_normal().lexically_relative(base.lexically_normal());
        std::string relative = relative_path.empty() ? destination.parent_path().generic_string() : relative_path.generic_string();
        if (relative.empty() || relative == ".") return "./";
        if (relative.back() != '/') relative += '/';
        return relative;
    }
    fs::path relative_path = destination.lexically_normal().lexically_relative(base.lexically_normal());
    std::string relative = relative_path.empty() ? destination.generic_string() : relative_path.generic_string();
    if (relative.find('/') == std::string::npos && relative.rfind("..", 0) != 0) relative = "./" + relative;
    return relative;
}

bool Parser::scalar_literal(const std::string& text, json::Document& value, std::string& error) const {
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
        value = json::Document(result);
        return true;
    }

    if (trimmed == "true") { value = json::Document(true); return true; }
    if (trimmed == "false") { value = json::Document(false); return true; }
    if (trimmed == "null") { value = json::Document(nullptr); return true; }

    const bool floating_literal = trimmed.find('.') != std::string::npos || trimmed.find('e') != std::string::npos || trimmed.find('E') != std::string::npos;
    if (!floating_literal) {
        std::int64_t integer = 0;
        const auto parsed = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), integer);
        if (parsed.ec == std::errc() && parsed.ptr == trimmed.data() + trimmed.size()) {
            value = json::Document(static_cast<double>(integer));
            if (integer > 9007199254740992LL || integer < -9007199254740992LL) {
                value.type = json::Type::StrNumber;
                value.string = trimmed;
            }
            return true;
        }
        if (parsed.ec == std::errc::result_out_of_range) { error = "integer literal outside signed 64-bit range"; return false; }
    }
    char* end = nullptr;
    const double number = std::strtod(trimmed.c_str(), &end);
    if (floating_literal && end && *end == '\0' && std::isfinite(number)) { value = json::Document(number); return true; }

    error = "expected JSON path or scalar literal in @if condition: " + trimmed;
    return false;
}

std::string Parser::render_expression_value(const json::Document& value) const {
    if (value.is_string() && value.string.rfind("\x1fnift:enum:",0)==0) { const auto last=value.string.rfind(':'); const auto prev=last==std::string::npos?last:value.string.rfind(':',last-1); if(prev!=std::string::npos&&last!=std::string::npos)return value.string.substr(prev+1,last-prev-1); }
    if (value.is_string()) return value.string;
    return value.dump(0);
}

bool Parser::serialize_value(const json::Document& value, bool pretty, std::string& output, std::string& error, int depth) const {
    if(value.is_string()&&value.string.rfind("\x1fnift:enum:",0)==0){const auto p=value.string.rfind(':');if(p==std::string::npos){error="invalid enum value";return false;}output+=value.string.substr(p+1);return true;}
    if (depth > 128) { error = "value serialization depth exceeded"; return false; }
    const std::string pad(pretty ? static_cast<std::size_t>(depth * 2) : 0, ' ');
    const std::string child_pad(pretty ? static_cast<std::size_t>((depth + 1) * 2) : 0, ' ');
    auto append_sequence = [&](const std::vector<json::Document>& values, const std::string& open, const std::string& close, std::string& dst)->bool {
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
            output += json::Document(value.object[i].first).dump(0);
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

bool Parser::evaluate_collection_value(const std::string& expression, json::Document& value, std::string& error) {
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

    auto truthy = [](const json::Document& d) {
        if (d.is_bool()) return d.boolean;
        if (d.is_null()) return false;
        if (d.is_number()) return d.num != 0.0;
        if (d.is_string()) return !d.string.empty();
        if (d.is_array()) return !d.array.empty();
        if (d.is_object()) return !d.object.empty();
        return false;
    };
    auto collection_arg = [&](const std::string& raw, json::Document& out) {
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
    auto with_bindings = [&](const std::vector<std::string>& bindings, const std::shared_ptr<const json::Document>& root,
                             const json::Document* element, const std::function<bool()>& action) {
        std::vector<std::pair<bool, std::shared_ptr<const json::Document>>> previous;
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
            json_bindings_[bindings[0]] = std::shared_ptr<const json::Document>(root, element);
        } else {
            if (!element->is_array() || element->array.size() != bindings.size()) {
                error = function + ": tuple binding arity must match each array item";
                return false;
            }
            for (std::size_t i = 0; i < bindings.size(); ++i)
                json_bindings_[bindings[i]] = std::shared_ptr<const json::Document>(root, &element->array[i]);
        }
        const bool ok = action();
        restore();
        return ok;
    };
    auto comparable = [&](const json::Document& a, const json::Document& b, int& result) {
        if (a.type != b.type || (!a.is_number() && !a.is_string())) {
            error = function + ": values must be numbers or strings of the same type";
            return false;
        }
        if (a.is_number()) result = a.num < b.num ? -1 : (a.num > b.num ? 1 : 0);
        else result = a.string < b.string ? -1 : (a.string > b.string ? 1 : 0);
        return true;
    };

    // Simple forms use the ordinary comma-separated parameter grammar.
    if (function == "slice") {
        bool params_ok = false;
        auto params = parse_parameters(body, params_ok);
        if (!params_ok || params.size() != 3) { error = "slice: expected collection, position and length"; return false; }
        json::Document source; if (!collection_arg(params[0], source)) return false;
        auto index = [&](const std::string& raw, std::size_t& out, const char* label) {
            json::Document v; if (!evaluate_expression(raw, v, error)) return false;
            if (!v.is_number() || v.num < 0 || std::trunc(v.num) != v.num) { error = std::string("slice: ") + label + " must be a non-negative integer"; return false; }
            out = static_cast<std::size_t>(v.num); return true;
        };
        std::size_t pos = 0, len = 0; if (!index(params[1], pos, "position") || !index(params[2], len, "length")) return false;
        value = json::Document::make_array();
        const std::size_t end = std::min(source.array.size(), pos > source.array.size() ? source.array.size() : pos + std::min(len, source.array.size() - pos));
        if (pos < source.array.size()) value.array.insert(value.array.end(), source.array.begin() + pos, source.array.begin() + end);
        return true;
    }
    if (function == "reverse" || function == "distinct") {
        bool params_ok = false; auto params = parse_parameters(body, params_ok);
        if (!params_ok || params.size() != 1) { error = function + ": expected one collection"; return false; }
        json::Document source; if (!collection_arg(params[0], source)) return false;
        value = json::Document::make_array();
        if (function == "reverse") { value.array.assign(source.array.rbegin(), source.array.rend()); return true; }
        std::unordered_set<std::string> seen;
        for (const auto& item : source.array) { const std::string key = item.dump(0); if (seen.insert(key).second) value.array.push_back(item); }
        return true;
    }
    if ((function == "sort" || function == "sum" || function == "prod" || function == "min" || function == "max") && find_top_level(body, "=>") == std::string::npos) {
        bool params_ok = false; auto params = parse_parameters(body, params_ok);
        if (!params_ok || params.size() != 1) { error = function + ": expected one collection or binding : collection => expression"; return false; }
        json::Document source; if (!collection_arg(params[0], source)) return false;
        if (function == "sort") {
            value = source; if (value.array.empty()) return true;
            const auto type = value.array.front().type;
            if (type != json::Type::Number && type != json::Type::String) { error = "sort: simple form requires an array of numbers or strings"; return false; }
            for (const auto& item : value.array) if (item.type != type) { error = "sort: values must all have the same sortable type"; return false; }
            std::stable_sort(value.array.begin(), value.array.end(), [](const auto& a, const auto& b) { return a.is_number() ? a.num < b.num : a.string < b.string; });
            return true;
        }
        if (function == "sum" || function == "prod") {
            double aggregate = function == "sum" ? 0.0 : 1.0;
            for (const auto& item : source.array) {
                if (!item.is_number()) { error = function + ": values must be numeric"; return false; }
                aggregate = function == "sum" ? aggregate + item.num : aggregate * item.num;
                if (!std::isfinite(aggregate)) { error = function + ": result is not finite"; return false; }
            }
            value = json::Document(aggregate); return true;
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
        json::Document source; if (!collection_arg(source_text, source)) return false;
        json::Document accumulator_value; if (!evaluate_expression(initial, accumulator_value, error)) return false;
        auto root = std::make_shared<const json::Document>(source);
        const auto old_acc = json_bindings_.find(accumulator); const bool had_acc = old_acc != json_bindings_.end(); const auto previous_acc = had_acc ? old_acc->second : nullptr;
        for (std::size_t i = 0; i < root->array.size(); ++i) {
            auto acc_root = std::make_shared<const json::Document>(accumulator_value);
            json_bindings_[accumulator] = acc_root;
            json::Document next;
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
    json::Document source; if (!collection_arg(source_text, source)) return false;
    auto root = std::make_shared<const json::Document>(source);

    bool descending = false;
    if (function == "sort") {
        if (expr.size() > 5 && expr.compare(expr.size() - 5, 5, " desc") == 0) { descending = true; expr = trim_copy(expr.substr(0, expr.size() - 5)); }
        else if (expr.size() > 4 && expr.compare(expr.size() - 4, 4, " asc") == 0) expr = trim_copy(expr.substr(0, expr.size() - 4));
    }
    if (function == "filter" || function == "map") value = json::Document::make_array();
    if (function == "some") value = json::Document(false);
    if (function == "every") value = json::Document(true);
    if (function == "find") value = json::Document(nullptr);
    if (function == "sum" || function == "prod") value = json::Document(function == "sum" ? 0.0 : 1.0);
    bool have_extreme = false;
    std::vector<json::Document> keys; if (function == "sort") keys.reserve(root->array.size());

    for (std::size_t i = 0; i < root->array.size(); ++i) {
        json::Document evaluated;
        const bool ok = with_bindings(bindings, root, &root->array[i], [&] { return evaluate_expression(expr, evaluated, error); });
        if (!ok) return false;
        if (function == "filter") { if (truthy(evaluated)) value.array.push_back(root->array[i]); }
        else if (function == "map") value.array.push_back(std::move(evaluated));
        else if (function == "find") { if (truthy(evaluated)) { value = root->array[i]; return true; } }
        else if (function == "some") { if (truthy(evaluated)) { value = json::Document(true); return true; } }
        else if (function == "every") { if (!truthy(evaluated)) { value = json::Document(false); return true; } }
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
    const auto type = keys.front().type;
    if (type != json::Type::Number && type != json::Type::String) { error = "sort: keys must be numbers or strings"; return false; }
    for (const auto& key : keys) if (key.type != type) { error = "sort: keys must all have the same type"; return false; }
    std::vector<std::size_t> order(keys.size()); for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
        const bool less = type == json::Type::Number ? keys[a].num < keys[b].num : keys[a].string < keys[b].string;
        const bool greater = type == json::Type::Number ? keys[a].num > keys[b].num : keys[a].string > keys[b].string;
        return descending ? greater : less;
    });
    value = json::Document::make_array(); for (auto index : order) value.array.push_back(root->array[index]); return true;
}

bool Parser::evaluate_expression(const std::string& expression, json::Document& value, std::string& error) {
    last_expression_mutation_ = false;
    auto resolve_direct = [&](const std::string& raw, json::Document& out) -> bool {
        const std::string text = trim_copy(raw);
        for (auto scope = variable_scopes_.rbegin(); scope != variable_scopes_.rend(); ++scope) {
            const auto it = scope->find(text); if (it != scope->end()) { it->second.sync(); out = *it->second.value; return true; }
            std::size_t root_len = 0;
            while (root_len < text.size() &&
                   (std::isalnum(static_cast<unsigned char>(text[root_len])) || text[root_len] == '_')) ++root_len;
            if (root_len == 0 || root_len == text.size()) continue;
            auto root_it = scope->find(text.substr(0, root_len));
            if (root_it == scope->end()) continue;
            if (root_it->second.value && root_it->second.value->is_string() && root_it->second.value->string.rfind("\x1fnift:struct:",0)==0 && text[root_len]=='.') {
                json::Document current=*root_it->second.value; std::size_t mp=root_len+1;
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
                json::Document current=*root_it->second.value; std::size_t mp=root_len+1;
                while(mp<text.size()){
                    std::size_t me=mp; while(me<text.size()&&(std::isalnum((unsigned char)text[me])||text[me]=='_'))++me;
                    if(me==mp){ if(text[mp]=='.'||text[mp]=='['){break;} error="invalid page member path: "+text;return false; }
                    const std::string member=text.substr(mp,me-mp);
                    if(current.is_string()&&current.string.rfind("\x1fnift:page:",0)==0){
                        result_.dependencies.insert(host_.relative(host_.root() / ".nift/hierarchy.fingerprint"));
                        json::Document resolved; std::string perr;
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
            const json::Document* cur = root_it->second.value.get();
            std::size_t pos = root_len;
            bool walk_ok = true;
            while (pos < text.size()) {
                if (text[pos] == '.') {
                    ++pos; const std::size_t member_start = pos;
                    while (pos < text.size() &&
                           (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_')) ++pos;
                    if (member_start == pos || !cur->is_object() || !cur->has(text.substr(member_start, pos - member_start))) { walk_ok = false; break; }
                    cur = &(*cur)[text.substr(member_start, pos - member_start)];
                } else if (text[pos] == '[') {
                    ++pos; const std::size_t index_start = pos;
                    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
                    if (index_start == pos || pos >= text.size() || text[pos] != ']') { walk_ok = false; break; }
                    std::size_t index = 0;
                    try { index = static_cast<std::size_t>(std::stoull(text.substr(index_start, pos - index_start))); }
                    catch (...) { walk_ok = false; break; }
                    ++pos;
                    if (!cur->is_array() || index >= cur->array.size()) { walk_ok = false; break; }
                    cur = &(*cur)[index];
                } else { walk_ok = false; break; }
            }
            if (walk_ok) { out = *cur; return true; }
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
                    json::Document current = *rf->second.value;
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
        std::shared_ptr<const json::Document> document;
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
        if (built_in_metadata_name(text)) { out = json::Document(metadata(text)); return true; }
        json::Document literal;
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
                if (nift_json::parse(text, literal, literal_error)) { out = std::move(literal); return true; }
                error = "invalid structured literal: " + literal_error;
                return false;
            }
            return false;
        }
        if (scalar_literal(text, literal, literal_error)) { if(literal.is_string()&&literal.string.find("$[")!=std::string::npos){std::string r,e;if(!interpolate_parameter(literal.string,r,e)){error=e;return false;}literal=json::Document(r);} out = std::move(literal); return true; }
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

    std::function<bool(const std::string&, json::Document&, int)> eval;
    eval = [&](const std::string& raw, json::Document& out, int depth) -> bool {
        std::string text=trim_copy(raw);
        if (text.empty()) { error="expression cannot be empty"; return false; }
        while (encloses(text)) text=trim_copy(text.substr(1,text.size()-2));

        auto find_binding = [&](const std::string& name) -> VariableBinding* {
            for (auto scope=variable_scopes_.rbegin(); scope!=variable_scopes_.rend(); ++scope) { auto it=scope->find(name); if(it!=scope->end()) { it->second.sync(); return &it->second; } }
            if(!receiver_stack_.empty()){auto it=receiver_stack_.back()->fields.find(name);if(it!=receiver_stack_.back()->fields.end())return &it->second;}
            return nullptr;
        };

        auto truthy_value = [](const json::Document& document) {
            if (document.is_bool()) return document.boolean;
            if (document.is_null()) return false;
            if (document.is_number()) return document.num != 0.0;
            if (document.is_string()) return !document.string.empty();
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
        // Exact int64-vs-double comparison across the complete supported range.
        // An integer is compared without converting it to a lossy double; a
        // fractional or out-of-int64 double is compared by magnitude so no pair
        // can report contradictory equality and ordering relationships.
        auto exact_i64 = [](const json::Document& d, std::int64_t& v)->bool {
            if(d.type==json::Type::StrNumber){auto r=std::from_chars(d.string.data(),d.string.data()+d.string.size(),v);return r.ec==std::errc()&&r.ptr==d.string.data()+d.string.size();}
            if(d.is_number()&&std::isfinite(d.num)&&std::trunc(d.num)==d.num&&d.num>=-9223372036854775808.0&&d.num<9223372036854775808.0){v=static_cast<std::int64_t>(d.num);return true;}return false;};
        auto compare_i64_double = [](std::int64_t i, double d)->int {
            if(d>=9223372036854775808.0) return -1;      // d >= 2^63 > any int64
            if(d<-9223372036854775808.0) return 1;       // d < -2^63 < any int64
            if(std::trunc(d)==d){std::int64_t di=static_cast<std::int64_t>(d);return i<di?-1:(i>di?1:0);}
            std::int64_t fl=static_cast<std::int64_t>(std::floor(d));
            if(i<fl)return -1; if(i>fl)return 1; return -1;  // i==floor(d) < d (d fractional)
        };
        auto numeric_compare = [&](const json::Document& a, const json::Document& b)->int {
            std::int64_t ai=0,bi=0; const bool a_i=exact_i64(a,ai), b_i=exact_i64(b,bi);
            if(a_i&&b_i)return ai<bi?-1:(ai>bi?1:0);
            if(a_i)return compare_i64_double(ai,b.num);
            if(b_i)return -compare_i64_double(bi,a.num);
            return a.num<b.num?-1:(a.num>b.num?1:0);
        };
        auto structural_equal = [&](const json::Document& a, const json::Document& b) -> bool {
            std::function<bool(const json::Document&,const json::Document&)> eq;
            eq = [&](const json::Document& x,const json::Document& y)->bool {
                if(x.is_number() && y.is_number()) {
                    return numeric_compare(x,y)==0;
                }
                if(x.type!=y.type)return false;
                if(x.is_null())return true; if(x.is_bool())return x.boolean==y.boolean;
                if(x.is_string()){
                    const bool xs=x.string.rfind("\x1fnift:struct:",0)==0, ys=y.string.rfind("\x1fnift:struct:",0)==0;
                    const bool xc=x.string.rfind("\x1fnift:callable:",0)==0, yc=y.string.rfind("\x1fnift:callable:",0)==0;
                    const bool xk=x.string.rfind("\x1fnift:collection:",0)==0, yk=y.string.rfind("\x1fnift:collection:",0)==0;
                    if(xs||ys||xc||yc) return (xs&&ys)||(xc&&yc) ? x.string==y.string : false;
                    if(xk||yk){
                        if(!(xk&&yk))return false; auto xi=collection_instances_.find(x.string.substr(17)), yi=collection_instances_.find(y.string.substr(17));
                        if(xi==collection_instances_.end()||yi==collection_instances_.end()||xi->second->kind!=yi->second->kind)return false;
                        auto a=xi->second,b=yi->second; if(a->kind==CollectionKind::PriQue)return x.string==y.string;
                        if(a->kind==CollectionKind::Stack||a->kind==CollectionKind::Queue){if(a->values.size()!=b->values.size())return false;for(size_t i=0;i<a->values.size();++i)if(!eq(a->values[i],b->values[i]))return false;return true;}
                        if(a->kind==CollectionKind::Set||a->kind==CollectionKind::SortedSet){if(a->values.size()!=b->values.size())return false;for(const auto& v:a->values){bool found=false;for(const auto& w:b->values)if(eq(v,w)){found=true;break;}if(!found)return false;}return true;}
                        if(a->entries.size()!=b->entries.size())return false;for(const auto& e:a->entries){bool found=false;for(const auto& f:b->entries)if(eq(e.first,f.first)&&eq(e.second,f.second)){found=true;break;}if(!found)return false;}return true;
                    }
                    return x.string==y.string;
                }
                if(x.is_array()){if(x.array.size()!=y.array.size())return false;for(size_t i=0;i<x.array.size();++i)if(!eq(x.array[i],y.array[i]))return false;return true;}
                if(x.is_object()){if(x.object.size()!=y.object.size())return false;for(const auto& e:x.object){auto it=std::find_if(y.object.begin(),y.object.end(),[&](const auto& z){return z.first==e.first;});if(it==y.object.end()||!eq(e.second,it->second))return false;}return true;}
                return false;
            }; return eq(a,b);
        };


        // v4.3 first-class callable values and lambda expressions.
        if (auto ci = callables_.find(text); ci != callables_.end()) {
            out = json::Document(std::string("\x1fnift:callable:named:") + text);
            return true;
        }
        {
            std::size_t arrow = std::string::npos; int pd=0, bd=0, cd=0; bool iq=false; char qc=0;
            for(std::size_t ai=0;ai+1<text.size();++ai){char ch=text[ai];if(iq){if(ch=='\\')++ai;else if(ch==qc)iq=false;continue;}if(ch=='\"'||ch=='\''){iq=true;qc=ch;continue;}if(ch=='(')++pd;else if(ch==')')--pd;else if(ch=='[')++bd;else if(ch==']')--bd;else if(ch=='{')++cd;else if(ch=='}')--cd;else if(ch=='='&&text[ai+1]=='>'&&pd==0&&bd==0&&cd==0){arrow=ai;break;}}
            if (arrow != std::string::npos && text.substr(0, arrow).find(":=") == std::string::npos && text.substr(0, arrow).find(" = ") == std::string::npos) {
                std::string lhs = trim_copy(text.substr(0, arrow));
                std::string rhs = trim_copy(text.substr(arrow + 2));
                std::vector<std::string> params;
                if (!lhs.empty() && lhs.front() == '(' && lhs.back() == ')') {
                    bool pok=false; params=parse_parameters(lhs.substr(1,lhs.size()-2),pok);
                    if(!pok){error="lambda: malformed parameter list";return false;}
                } else if (valid_binding_identifier(lhs)) params.push_back(lhs);
                else { error="lambda: expected identifier or parenthesized parameter list"; return false; }
                std::string variadic_param;
                for(std::size_t pi=0;pi<params.size();++pi){auto p=trim_copy(params[pi]);if(p.rfind("...",0)==0){if(!variadic_param.empty()||pi+1!=params.size()||!valid_binding_identifier(trim_copy(p.substr(3)))){error="lambda: invalid variadic parameter";return false;}variadic_param=trim_copy(p.substr(3));params.pop_back();break;}if(!valid_binding_identifier(p)){error="lambda: invalid parameter";return false;}params[pi]=p;}
                auto li=std::make_shared<LambdaInstance>(); li->params=params; li->variadic_param=variadic_param;
                li->block=rhs.size()>=2&&rhs.front()=='{'&&rhs.back()=='}';
                li->body=li->block?rhs.substr(1,rhs.size()-2):rhs;
                for(const auto& scope:variable_scopes_) for(const auto& kv:scope) li->captures[kv.first]=kv.second;
                // A lambda created inside an imported module may escape via an
                // exported value; snapshot the module's callables so its body
                // can still reach package-private @fn helpers.
                if (in_import_program_ && !callables_.empty()) {
                    auto env=std::make_shared<ModuleEnv>(); env->callables=callables_;
                    li->module_env=std::move(env);
                }
                const std::string id=std::to_string(next_lambda_instance_id_++); lambda_instances_[id]=li;
                out=json::Document(std::string("\x1fnift:callable:lambda:")+id); return true;
            }
        }

        // v4.3 native scripting filesystem, console and stream primitives.
        {
            auto call_args=[&](const std::string& name,std::vector<std::string>& args,std::vector<bool>& quoted)->bool{
                if(text.rfind(name+"(",0)!=0||text.back()!=')')return false;
                std::size_t close=0;if(!find_balanced(text,name.size(),'(',')',close)||close!=text.size()-1)return false;
                bool ok=false;args=parse_parameters(text.substr(name.size()+1,text.size()-name.size()-2),ok,&quoted);
                if(!ok){error=name+": malformed arguments";return false;}
                std::vector<std::string> expanded; std::vector<bool> expanded_q;
                for(std::size_t ai=0;ai<args.size();++ai){
                    if(!(ai<quoted.size()&&quoted[ai])&&trim_copy(args[ai]).rfind("...",0)==0){json::Document sv;if(!eval(trim_copy(args[ai]).substr(3),sv,depth+1))return false;if(!sv.is_array()){error=name+": spread value must be an array";return false;}for(const auto& item:sv.array){std::string encoded;if(!serialize_value(item,false,encoded,error))return false;expanded.push_back(encoded);expanded_q.push_back(false);}}
                    else {expanded.push_back(args[ai]);expanded_q.push_back(ai<quoted.size()&&quoted[ai]);}
                }
                args.swap(expanded);quoted.swap(expanded_q);return true;
            };
            auto arg_value=[&](const std::vector<std::string>& args,const std::vector<bool>& q,size_t i,json::Document& v)->bool{
                if(i>=args.size())return false;if(i<q.size()&&q[i]){v=json::Document(args[i]);return true;}return eval(args[i],v,depth+1);
            };
            auto string_arg=[&](const std::string& name,const std::vector<std::string>& args,const std::vector<bool>& q,size_t i,std::string& v)->bool{
                json::Document d;if(!arg_value(args,q,i,d)||!d.is_string()){error=name+": expected string path";return false;}v=d.string;return true;
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
                for(std::size_t ai=0;ai<args.size();++ai){json::Document v;if(!arg_value(args,q,ai,v))return false;if(v.is_array()){for(const auto&x:v.array){if(!(x.is_string()||x.is_number()||x.is_bool())){error="cmd: arguments must be scalar";return false;}vals.push_back(render_expression_value(x));}}else if(v.is_string()||v.is_number()||v.is_bool())vals.push_back(render_expression_value(v));else{error="cmd: arguments must be scalar";return false;}}
                auto c=std::make_shared<CommandInstance>();ProcessSpec ps;ps.program=vals.front();ps.args.assign(vals.begin()+1,vals.end());c->stages.push_back(std::move(ps));auto id=std::to_string(next_command_instance_id_++);command_instances_[id]=c;out=json::Document(std::string("\x1fnift:cmd:")+id);return true;
            }
            if(call_args("run",args,q)){
                if(std::getenv("NIFT_NO_PROCESS")){error="run: external process execution disabled";return false;}
                if(args.empty()){error="run: expected executable and optional arguments";return false;}
                std::vector<std::string> vals;
                for(std::size_t ai=0;ai<args.size();++ai){json::Document v;if(!arg_value(args,q,ai,v))return false;if(v.is_array()){for(const auto& x:v.array){if(!x.is_string()&&!x.is_number()&&!x.is_bool()){error="run: arguments must be scalar";return false;}vals.push_back(render_expression_value(x));}}else if(v.is_string()||v.is_number()||v.is_bool())vals.push_back(render_expression_value(v));else{error="run: arguments must be scalar";return false;}}
                ProcessSpec spec;spec.program=vals.front();spec.args.assign(vals.begin()+1,vals.end());auto pr=nift_run_process(spec,true,false);
                out=json::Document::make_object();out["exit_code"]=json::Document((double)pr.exit_code);out["stdout"]=json::Document(pr.out);out["stderr"]=json::Document(pr.err);out["launched"]=json::Document(pr.launched);if(!pr.error.empty())out["error"]=json::Document(pr.error);return true;
            }
            if(call_args("which",args,q)){if(args.size()!=1){error="which: expected command name";return false;}std::string n;if(!string_arg("which",args,q,0,n))return false;static const std::unordered_set<std::string> builtins={"ls","cd","pwd","cat","touch","exists","cp","mv","rm","mkdir","copy","move","remove","make_dir","run","cmd"};if(builtins.count(n)){out=json::Document("nift:"+n);return true;}std::string p;if(nift_find_executable(n,p)){out=json::Document(p);return true;}out=json::Document(nullptr);return true;}
            // Native project/build automation (v4.4 CP23/CP24). Direct ProjectInfo
            // calls, never a subprocess. Script-land only.
            auto automation_result=[&](NiftOperationResult r)->json::Document{json::Document d=json::Document::make_object();d["ok"]=json::Document(r.ok);d["exit_code"]=json::Document((double)r.exit_code);json::Document aff=json::Document::make_array();for(const auto& a:r.affected)aff.array.emplace_back(a);d["affected"]=std::move(aff);json::Document errs=json::Document::make_array();for(const auto& e:r.errors)errs.array.emplace_back(e);d["errors"]=std::move(errs);d["duration_seconds"]=json::Document(r.duration_seconds);return d;};
            auto ensure_project=[&](const std::string& fn,ProjectInfo*& proj)->bool{if(!standalone_script_host_){error=fn+": only available under nift run/nift sh";return false;}const fs::path cwd=fs::absolute(fs::current_path()).lexically_normal();if(!automation_project_||automation_root_.empty()||!filesystem::path_within(automation_root_,cwd)){auto p=std::make_shared<ProjectInfo>();std::string oe;if(!automation_open_project(cwd,*p,oe)){automation_project_.reset();automation_root_.clear();error=fn+": "+oe;return false;}automation_project_=p;automation_root_=fs::absolute(automation_project_->root).lexically_normal();}proj=automation_project_.get();return true;};
            if(call_args("build",args,q)||call_args("build_all",args,q)||call_args("build_repair",args,q)){const std::string fn=text.rfind("build_all",0)==0?"build_all":(text.rfind("build_repair",0)==0?"build_repair":"build");if(!args.empty()){error=fn+": expected no arguments";return false;}ProjectInfo* proj=nullptr;if(!ensure_project(fn,proj))return false;NiftBuildRequest req;req.mode=fn=="build_all"?NiftBuildRequest::Mode::All:(fn=="build_repair"?NiftBuildRequest::Mode::Repair:NiftBuildRequest::Mode::Updated);out=automation_result(automation_build(*proj,req));return true;}
            if(call_args("build_names",args,q)){if(args.empty()){error="build_names: expected at least one name";return false;}ProjectInfo* proj=nullptr;if(!ensure_project("build_names",proj))return false;NiftBuildRequest req;req.mode=NiftBuildRequest::Mode::Names;for(size_t ai=0;ai<args.size();++ai){std::string n;if(!string_arg("build_names",args,q,ai,n))return false;req.names.push_back(n);}out=automation_result(automation_build(*proj,req));return true;}
            if(call_args("track",args,q)){if(args.size()<1||args.size()>3){error="track: expected name, optional title and template";return false;}std::string name,title,template_path;if(!string_arg("track",args,q,0,name))return false;if(args.size()>=2&&!string_arg("track",args,q,1,title))return false;if(args.size()>=3&&!string_arg("track",args,q,2,template_path))return false;ProjectInfo* proj=nullptr;if(!ensure_project("track",proj))return false;out=automation_result(automation_track(*proj,name,title,template_path));return true;}
            if(call_args("untrack",args,q)){if(args.empty()){error="untrack: expected at least one name";return false;}ProjectInfo* proj=nullptr;if(!ensure_project("untrack",proj))return false;std::vector<std::string> names;for(size_t ai=0;ai<args.size();++ai){std::string n;if(!string_arg("untrack",args,q,ai,n))return false;names.push_back(n);}out=automation_result(automation_untrack(*proj,names,false));return true;}
            if(call_args("status",args,q)){if(!args.empty()){error="status: expected no arguments";return false;}ProjectInfo* proj=nullptr;if(!ensure_project("status",proj))return false;auto names=automation_status(*proj);out=json::Document::make_array();for(const auto& n:names)out.array.emplace_back(n);return true;}
            if(call_args("tracked",args,q)){if(!args.empty()){error="tracked: expected no arguments";return false;}ProjectInfo* proj=nullptr;if(!ensure_project("tracked",proj))return false;auto names=automation_tracked_names(*proj);out=json::Document::make_array();for(const auto& n:names)out.array.emplace_back(n);return true;}
            if(call_args("project_root",args,q)){if(!args.empty()){error="project_root: expected no arguments";return false;}ProjectInfo* proj=nullptr;if(!ensure_project("project_root",proj))return false;out=json::Document(fs::absolute(proj->root).generic_string());return true;}
            if(call_args("getenv",args,q)){if(args.size()!=1){error="getenv: expected name";return false;}std::string k;if(!string_arg("getenv",args,q,0,k))return false;const char*v=std::getenv(k.c_str());out=v?json::Document(std::string(v)):json::Document(nullptr);return true;}
            if(call_args("setenv",args,q)){if(!standalone_script_host_){error="setenv: only available under nift run/nift sh";return false;}if(args.size()!=2){error="setenv: expected name and value";return false;}std::string k,v;if(!string_arg("setenv",args,q,0,k)||!string_arg("setenv",args,q,1,v))return false;
#ifdef _WIN32
                if(_putenv_s(k.c_str(),v.c_str())!=0){error="setenv: failed";return false;}
#else
                if(::setenv(k.c_str(),v.c_str(),1)!=0){error="setenv: failed";return false;}
#endif
                out=json::Document(nullptr);return true;}
            if(call_args("unsetenv",args,q)){if(!standalone_script_host_){error="unsetenv: only available under nift run/nift sh";return false;}if(args.size()!=1){error="unsetenv: expected name";return false;}std::string k;if(!string_arg("unsetenv",args,q,0,k))return false;
#ifdef _WIN32
                _putenv_s(k.c_str(),"");
#else
                ::unsetenv(k.c_str());
#endif
                out=json::Document(nullptr);return true;}
            if(call_args("pwd",args,q)){if(!args.empty()){error="pwd: expected no arguments";return false;}out=json::Document((standalone_script_host_?fs::current_path():host_.root()).generic_string());return true;}
            if(call_args("cd",args,q)){if(!standalone_script_host_){error="cd: only available under nift run/nift sh";return false;}fs::path p;if(args.size()!=1||!checked_path("cd",args,q,0,p))return false;std::error_code ec;fs::current_path(p,ec);if(ec){error="cd: "+ec.message();return false;}out=json::Document(nullptr);return true;}
            if(call_args("exists",args,q)){fs::path p;if(args.size()!=1||!checked_path("exists",args,q,0,p))return false;std::error_code ec;out=json::Document(fs::exists(p,ec)&&!ec);return true;}
            if(call_args("make_dir",args,q)||call_args("mkdir",args,q)){fs::path p;if(args.size()!=1||!checked_path("make_dir",args,q,0,p))return false;std::error_code ec;fs::create_directories(p,ec);if(ec){error="make_dir: "+ec.message();return false;}out=json::Document(nullptr);return true;}
            if(call_args("touch",args,q)){fs::path p;if(args.size()!=1||!checked_path("touch",args,q,0,p))return false;std::ofstream f(p,std::ios::app);if(!f){error="touch: cannot open path";return false;}out=json::Document(nullptr);return true;}
            auto path_values=[&](const std::string& name,const std::vector<std::string>& aa,const std::vector<bool>& qq,std::size_t begin,std::size_t end,std::vector<fs::path>& paths)->bool{for(std::size_t ai=begin;ai<end;++ai){json::Document v;if(!arg_value(aa,qq,ai,v))return false;std::vector<std::string> raws;if(v.is_string())raws.push_back(v.string);else if(v.is_array()){for(const auto& x:v.array){if(!x.is_string()){error=name+": path arrays must contain strings";return false;}raws.push_back(x.string);}}else{error=name+": expected string path or array of paths";return false;}for(const auto& raw:raws){fs::path p=resolve_path(raw);if(!standalone_script_host_&&!host_.root().empty()&&!filesystem::path_within(fs::absolute(host_.root()).lexically_normal(),p)){error=name+": path must stay inside the Nift project";return false;}if(!nift_fs_root_allowed(p,standalone_script_host_,error)){error=name+": "+error;return false;}if(nift_glob_has_magic(raw)){auto matches=nift_glob_expand(p);paths.insert(paths.end(),matches.begin(),matches.end());}else paths.push_back(std::move(p));}}return true;};
            if(call_args("remove",args,q)||call_args("rm",args,q)){if(args.empty()){error="remove: expected at least one path";return false;}std::vector<fs::path> paths;if(!path_values("remove",args,q,0,args.size(),paths))return false;for(const auto& rp:paths){std::error_code ec;if(fs::is_directory(rp,ec)){error="remove: directories are not removed recursively";return false;}if(!fs::remove(rp,ec)&&ec){error="remove: "+ec.message();return false;}}out=json::Document(nullptr);return true;}
            if(call_args("copy",args,q)||call_args("cp",args,q)||call_args("move",args,q)||call_args("mv",args,q)){const bool mv=text.rfind("move(",0)==0||text.rfind("mv(",0)==0;const std::string name=mv?"move":"copy";if(text.rfind("copy(",0)==0&&args.size()<2){args.clear();q.clear();}else if(args.size()<2){error=name+": expected source(s) and destination";return false;}else{std::vector<fs::path> sources;if(!path_values(name,args,q,0,args.size()-1,sources))return false;if(sources.empty()){error=name+": glob matched no source files";return false;}std::vector<fs::path> dests;if(!path_values(name,args,q,args.size()-1,args.size(),dests)||dests.size()!=1){error=name+": destination must be one path";return false;}fs::path dest=dests.front();std::error_code dec;const bool dest_dir=fs::is_directory(dest,dec);if(sources.size()>1&&!dest_dir){error=name+": destination must be an existing directory for multiple sources";return false;}for(const auto& src:sources){fs::path target=dest_dir?dest/src.filename():dest;std::error_code ec;if(mv)fs::rename(src,target,ec);else fs::copy_file(src,target,fs::copy_options::overwrite_existing,ec);if(ec){error=name+": "+ec.message();return false;}}out=json::Document(nullptr);return true;}}
            if(call_args("cat",args,q)){fs::path p;if(args.size()!=1||!checked_path("cat",args,q,0,p))return false;std::error_code ec;if(fs::is_directory(p,ec)){error="cat: path is a directory";return false;}std::ifstream f(p,std::ios::binary);if(!f){error="cat: cannot open path";return false;}std::ostringstream ss;ss<<f.rdbuf();{static std::mutex cat_mutex;std::lock_guard<std::mutex> lock(cat_mutex);std::cout<<ss.str();std::cout.flush();}out=json::Document(nullptr);return true;}
            if(call_args("ls",args,q)){if(args.size()>1){error="ls: expected zero or one path/pattern";return false;}out=json::Document::make_array();if(args.empty()){fs::path p=standalone_script_host_?fs::current_path():host_.root();std::error_code ec;std::vector<std::string> names;for(fs::directory_iterator it(p,ec),end;!ec&&it!=end;it.increment(ec))names.push_back(it->path().filename().generic_string());if(ec){error="ls: "+ec.message();return false;}std::sort(names.begin(),names.end());for(const auto& n:names)out.array.emplace_back(n);return true;}std::string raw;if(!string_arg("ls",args,q,0,raw))return false;fs::path p=resolve_path(raw);if(!standalone_script_host_&&!host_.root().empty()&&!filesystem::path_within(fs::absolute(host_.root()).lexically_normal(),p)){error="ls: path must stay inside the Nift project";return false;}if(!nift_fs_root_allowed(p,standalone_script_host_,error)){error="ls: "+error;return false;}if(nift_glob_has_magic(raw)){auto matches=nift_glob_expand(p);const fs::path base=standalone_script_host_?fs::current_path():host_.root();const bool absolute=fs::path(raw).is_absolute();for(const auto&m:matches){std::error_code rec;auto shown=absolute?m:fs::relative(m,base,rec);out.array.emplace_back((rec?m:shown).generic_string());}return true;}std::error_code ec;if(!fs::is_directory(p,ec)||ec){error="ls: path is not a readable directory";return false;}std::vector<std::string> names;for(fs::directory_iterator it(p,ec),end;!ec&&it!=end;it.increment(ec))names.push_back(it->path().filename().generic_string());if(ec){error="ls: "+ec.message();return false;}std::sort(names.begin(),names.end());for(const auto& n:names)out.array.emplace_back(n);return true;}
            if(call_args("open",args,q)){fs::path p;if(args.size()!=1||!checked_path("open",args,q,0,p))return false;std::error_code ec;if(fs::is_directory(p,ec)){error="open: path is a directory";return false;}std::ifstream f(p,std::ios::binary);if(!f){error="open: cannot open path";return false;}std::ostringstream ss;ss<<f.rdbuf();out=json::Document(ss.str());return true;}
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
                json::Document first;if(!arg_value(args,q,0,first))return false;
                if(!first.is_string()){error="minify: source or path must be a string";return false;}
                std::string format_name;bool explicit_format=false;bool has_opts=false;json::Document opts;
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
                auto minify_fail=[&](const std::string& msg,const std::string& fmt,const std::string& written)->json::Document{json::Document r=json::Document::make_object();r["ok"]=json::Document(false);r["output"]=json::Document("");r["error"]=json::Document(msg);r["format"]=json::Document(fmt);r["written"]=json::Document(written);return r;};
                if(explicit_format&&!has_opts){
                    minify::Format fmt;if(!minify_format_from_name(format_name,fmt)){out=minify_fail("unsupported format '"+format_name+"'",format_name,"");return true;}
                    std::string out_text,merr;if(!minify::run(fmt,arg0,out_text,merr)){out=minify_fail(merr,format_name,"");return true;}
                    json::Document r=json::Document::make_object();r["ok"]=json::Document(true);r["output"]=json::Document(out_text);r["error"]=json::Document("");r["format"]=json::Document(format_name);r["written"]=json::Document("");out=std::move(r);return true;
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
                json::Document r=json::Document::make_object();r["ok"]=json::Document(true);r["output"]=json::Document(out_text);r["error"]=json::Document("");r["format"]=json::Document(format_name);r["written"]=json::Document(dest.generic_string());out=std::move(r);return true;
            }
            if(call_args("file",args,q)){fs::path p;if(args.size()!=1||!checked_path("file",args,q,0,p))return false;auto f=std::make_shared<FileInstance>();f->path=p;auto id=std::to_string(next_file_instance_id_++);file_instances_[id]=f;out=json::Document(std::string("\x1fnift:file:")+id);return true;}
            if(call_args("page",args,q)){if(args.size()!=1){error="page: expected one page name";return false;}json::Document d;if(!arg_value(args,q,0,d)||!d.is_string()){error="page: name must be a string";return false;}std::string ref;if(!host_.page_ref_for(d.string,ref)){error="page: unknown tracked page '"+d.string+"'";return false;}out=json::Document(ref);return true;}
            if(call_args("min",args,q)||call_args("max",args,q)){const bool want_min=text.rfind("min(",0)==0;const std::string name=want_min?"min":"max";std::vector<json::Document> vals;if(args.size()==1){json::Document v;if(!arg_value(args,q,0,v))return false;if(v.is_array())vals=v.array;else vals.push_back(std::move(v));}else for(std::size_t ai=0;ai<args.size();++ai){json::Document v;if(!arg_value(args,q,ai,v))return false;vals.push_back(std::move(v));}if(vals.empty()){error=name+": expected at least one value";return false;}out=vals.front();for(std::size_t ai=1;ai<vals.size();++ai){const auto& v=vals[ai];bool take=false;if(out.is_number()&&v.is_number())take=want_min?v.num<out.num:v.num>out.num;else if(out.is_string()&&v.is_string())take=want_min?v.string<out.string:v.string>out.string;else{error=name+": values must be comparable and homogeneous";return false;}if(take)out=v;}return true;}
            // CP204-205: stable public runtime introspection. Internal marker
            // encodings are mapped to public language categories, not exposed.
            auto public_type=[&](const json::Document& v)->std::string{
                if(v.is_null()) return "null";
                if(v.is_bool()) return "bool";
                if(v.type==json::Type::StrNumber) return "int";
                if(v.is_number()) return std::trunc(v.num)==v.num ? "int" : "float";
                if(v.is_array()) return "array";
                if(v.is_object()) return "object";
                if(v.is_string()){
                    if(v.string.rfind("\x1fnift:struct:",0)==0) return "struct";
                    if(v.string.rfind("\x1fnift:callable:",0)==0) return "function";
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
                json::Document v;if(!arg_value(args,q,0,v))return false;out=json::Document(public_type(v));return true;
            }
            for(const std::string pred:{"is_null","is_bool","is_number","is_int","is_float","is_string","is_array","is_object","is_struct","is_function","is_collection","is_enum"}){
                if(call_args(pred,args,q)){
                    if(args.size()!=1){error=pred+": expected one value";return false;}
                    json::Document v;if(!arg_value(args,q,0,v))return false;const std::string t=public_type(v);
                    bool yes=pred=="is_null"?t=="null":pred=="is_bool"?t=="bool":pred=="is_number"?(t=="int"||t=="float"):pred=="is_int"?t=="int":pred=="is_float"?t=="float":pred=="is_string"?t=="string":pred=="is_array"?t=="array":pred=="is_object"?t=="object":pred=="is_struct"?t=="struct":pred=="is_function"?t=="function":pred=="is_collection"?t=="collection":t=="enum";
                    out=json::Document(yes);return true;
                }
            }
            // CP210: Python-style integer ranges, stop-exclusive and materialized.
            if(call_args("range",args,q)){
                if(args.empty()||args.size()>3){error="range: expected stop, start/stop, or start/stop/step";return false;}
                std::vector<std::int64_t> n; for(std::size_t ai=0;ai<args.size();++ai){json::Document v;if(!arg_value(args,q,ai,v)||!v.is_number()||std::trunc(v.num)!=v.num||v.num<(double)std::numeric_limits<std::int64_t>::min()||v.num>=9223372036854775808.0){error="range: arguments must be signed 64-bit integers";return false;}n.push_back((std::int64_t)v.num);}
                std::int64_t start=args.size()==1?0:n[0], stop=args.size()==1?n[0]:n[1], step=args.size()==3?n[2]:1;
                if(step==0){error="range: step must not be zero";return false;}
                out=json::Document::make_array(); constexpr std::size_t limit=10000000;
                for(std::int64_t x=start; step>0?x<stop:x>stop;){if(out.array.size()>=limit){error="range: result exceeds 10000000 items";return false;}out.array.emplace_back((double)x);if((step>0&&x>std::numeric_limits<std::int64_t>::max()-step)||(step<0&&x<std::numeric_limits<std::int64_t>::min()-step)){error="range: integer overflow";return false;}x+=step;}
                return true;
            }
            if(call_args("html_escape",args,q)||call_args("attr_escape",args,q)||call_args("url_encode",args,q)){
                const bool is_attr=text.rfind("attr_escape(",0)==0; const bool is_url=text.rfind("url_encode(",0)==0; const char* fname=is_attr?"attr_escape":(is_url?"url_encode":"html_escape");
                if(args.size()!=1){error=std::string(fname)+": expected one value";return false;}
                json::Document v;if(!arg_value(args,q,0,v))return false;
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
                out=json::Document(std::move(r));return true;
            }
            if(call_args("ifstream",args,q)||call_args("ofstream",args,q)){const bool output=text.rfind("ofstream(",0)==0;fs::path p;if(args.size()!=1||!checked_path(output?"ofstream":"ifstream",args,q,0,p))return false;auto st=std::make_shared<StreamInstance>();st->kind=output?StreamInstance::Kind::Output:StreamInstance::Kind::Input;if(output){st->output=std::make_shared<std::ofstream>(p,std::ios::binary|std::ios::trunc);if(!*st->output){error="ofstream: cannot open path";return false;}}else{st->input=std::make_shared<std::ifstream>(p,std::ios::binary);if(!*st->input){error="ifstream: cannot open path";return false;}}auto id=std::to_string(next_stream_instance_id_++);stream_instances_[id]=st;out=json::Document(std::string("\x1fnift:stream:")+id);return true;}
            if(call_args("close",args,q)){if(args.size()!=1){error="close: expected stream";return false;}json::Document d;if(!arg_value(args,q,0,d)||!d.is_string()||d.string.rfind("\x1fnift:stream:",0)!=0){error="close: expected stream";return false;}auto it=stream_instances_.find(d.string.substr(13));if(it==stream_instances_.end()){error="close: invalid stream";return false;}if(it->second->closed){error="close: stream already closed";return false;}if(it->second->input)it->second->input->close();if(it->second->output)it->second->output->close();it->second->closed=true;out=json::Document(nullptr);return true;}
            if(call_args("print",args,q)){if(args.size()!=1){error="print: expected one value";return false;}json::Document d;if(!arg_value(args,q,0,d))return false;if(d.is_string()&&q.size()>0&&q[0]&&d.string.find("$[")!=std::string::npos){std::string r,e;if(!interpolate_parameter(d.string,r,e)){error="print: "+e;return false;}d=json::Document(r);}if(d.is_array()||d.is_object()||(d.is_string()&&d.string.rfind("\x1fnift:",0)==0&&d.string.rfind("\x1fnift:enum:",0)!=0)){error="print: value is not directly renderable";return false;}const std::string rendered=render_expression_value(d);{static std::mutex print_mutex;std::lock_guard<std::mutex> lock(print_mutex);std::cout<<rendered<<'\n';std::cout.flush();}out=json::Document(nullptr);return true;}
            if(call_args("read",args,q)){if(!args.empty()){error="read: expected no arguments";return false;}if(!standalone_script_host_){error="read: interactive input is only available under nift run/nift sh";return false;}std::string line;if(!std::getline(std::cin,line)){if(std::cin.eof()){std::cin.clear();out=json::Document(nullptr);return true;}error="read: input failure";return false;}out=json::Document(line);return true;}
        }

        // v4.3 canonical value presentation. Presentation modifiers compose over the
        // original value rather than serializing each other's string output. Thus
        // x.prettify().highlight() and x.highlight().prettify() both mean
        // "pretty + highlighted when directly inspected by the REPL". ANSI remains
        // a presentation concern; expression evaluation always returns a plain string.
        {
            std::string target; bool pretty=false, highlight=false;
            if (strip_presentation_chain(text, target, pretty, highlight)) {
                json::Document v;if(!eval(target,v,depth+1))return false;
                std::string rendered;if(!serialize_value(v,pretty,rendered,error))return false;
                out=json::Document(rendered);return true;
            }
        }

        // v4.3 CP100/CP102 scalar/string and read-only postfix ergonomics.
        // Parse only the final .method(...) postfix here; recursively evaluating the
        // receiver makes literals and call/expression results chain naturally without
        // teaching each primitive about every possible trailing method.
        {
            auto final_postfix = [&](std::string& receiver, std::string& method, std::string& arg_text)->bool {
                if (text.empty() || text.back() != ')') return false;
                bool quoted=false; char quote=0; int parens=0, brackets=0, braces=0;
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
                    method=="contains"||method=="starts_with"||method=="ends_with"||method=="trim"||
                    method=="trim_start"||method=="trim_end"||method=="to_lower"||method=="to_upper"||
                    method=="replace"||method=="to_int"||method=="to_double"||method=="to_string"||method=="abs"||method=="floor"||method=="ceil"||method=="round"||
                    method=="substr"||method=="size"||method=="empty"||method=="first"||method=="last"||
                    method=="join"||method=="slice"||method=="indexOf"||method=="path"||method=="exists"||method=="open"||
                    method=="close"||method=="read"||method=="read_line"||method=="read_all"||method=="read_val"||
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
                    json::Document base;
                    if (eval(receiver, base, depth + 1) && base.is_object()) {
                        for (const auto& kv : base.object) {
                            if (kv.first == method && kv.second.is_string() && kv.second.string.rfind("\x1fnift:callable:",0)==0) {
                                push_variable_scope(); auto& sc=variable_scopes_.back();
                                auto csp=std::make_shared<json::Document>(kv.second);
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
                        std::shared_ptr<const json::Document> path_value;
                        std::string path_error;
                        if (resolve_json_value(receiver, path_value, path_error) && path_error.empty() && path_value) {
                            const json::Document& pv = *path_value;
                            if (pv.is_array()) {
                                if (method=="size"||method=="length"){out=json::Document((double)pv.array.size());return true;}
                                if (method=="empty"){out=json::Document(pv.array.empty());return true;}
                                if (method=="first"||method=="last"){if(pv.array.empty()){error=method+": array is empty";return false;}out=method=="first"?pv.array.front():pv.array.back();return true;}
                            } else if (pv.is_object()) {
                                if (method=="size"){out=json::Document((double)pv.object.size());return true;}
                                if (method=="empty"){out=json::Document(pv.object.empty());return true;}
                                if (method=="keys"||method=="values"||method=="entries"){out=json::Document::make_array();for(const auto&kv:pv.object){if(method=="keys")out.array.emplace_back(kv.first);else if(method=="values")out.array.push_back(kv.second);else{json::Document e=json::Document::make_object();e["key"]=json::Document(kv.first);e["value"]=kv.second;out.array.push_back(std::move(e));}}return true;}
                            }
                        }
                    }
                    json::Document base;
                    if (!eval(receiver,base,depth+1)) return false;
                    bool aok=false; std::vector<bool> aq;
                    auto args=parse_parameters(arg_text,aok,&aq);
                    if(!aok){error=method+": malformed arguments";return false;}
                    auto eval_arg=[&](std::size_t i,json::Document& v)->bool{
                        if(i>=args.size())return false;
                        if(i<aq.size()&&aq[i]){v=json::Document(args[i]);return true;}
                        return eval(args[i],v,depth+1);
                    };
                    auto no_args=[&]()->bool{if(!args.empty()){error=method+": expected no arguments";return false;}return true;};
                    if(base.is_string()&&base.string.rfind("\x1fnift:enum:",0)==0&&(method=="to_int"||method=="to_string")){
                        if(!no_args())return false;const auto last=base.string.rfind(':');const auto prev=last==std::string::npos?last:base.string.rfind(':',last-1);if(last==std::string::npos||prev==std::string::npos){error="invalid enum value";return false;}
                        if(method=="to_string"){out=json::Document(base.string.substr(prev+1,last-prev-1));return true;}
                        std::int64_t v=0;auto r=std::from_chars(base.string.data()+last+1,base.string.data()+base.string.size(),v);if(r.ec!=std::errc()){error="invalid enum backing value";return false;}out=json::Document((double)v);if(v>9007199254740992LL||v<-9007199254740992LL){out.type=json::Type::StrNumber;out.string=std::to_string(v);}return true;
                    }
                    // CP171: all operations that construct object keys from values
                    // share one conversion rule. JSON objects ultimately have string
                    // keys, so Nift accepts only renderable scalar key values and
                    // checks uniqueness after canonical rendering.
                    auto generated_object_key=[&](const json::Document& key,std::string& rendered)->bool{
                        if(!(key.is_string()||key.is_number()||key.is_bool())){error=method+": generated key must be string, number, or bool";return false;}
                        rendered=key.is_string()?key.string:render_expression_value(key);
                        return true;
                    };
                    if(base.is_string() && base.string.rfind("\x1fnift:cmd:",0)==0) {
                        auto ci=command_instances_.find(base.string.substr(10));if(ci==command_instances_.end()){error="invalid command";return false;}auto c=ci->second;
                        auto& aa=args; auto& qq=aq;
                        auto sval=[&](size_t i,std::string&v){if(i>=aa.size())return false;json::Document d;if(i<qq.size()&&qq[i])d=json::Document(aa[i]);else if(!eval(aa[i],d,depth+1))return false;if(!d.is_string()){error=method+": expected string";return false;}v=d.string;return true;};
                        if(method=="pipe"){if(aa.size()!=1){error="pipe: expected command";return false;}json::Document d;if(!eval(aa[0],d,depth+1)||!d.is_string()||d.string.rfind("\x1fnift:cmd:",0)!=0){error="pipe: expected cmd(...)";return false;}auto oi=command_instances_.find(d.string.substr(10));if(oi==command_instances_.end()){error="pipe: invalid command";return false;}c->stages.insert(c->stages.end(),oi->second->stages.begin(),oi->second->stages.end());out=base;return true;}
                        if(method=="stdin"||method=="stdout"||method=="stderr"||method=="cwd"){std::string v;if((aa.size()<1||aa.size()>2)||!sval(0,v))return false;auto&st=(method=="stdin"?c->stages.front():c->stages.back());if(method=="stdin")st.stdin_path=v;else if(method=="stdout"){st.stdout_path=v;if(aa.size()==2){std::string m;if(!sval(1,m))return false;st.append_stdout=m=="append";}}else if(method=="stderr"){st.stderr_path=v;if(aa.size()==2){std::string m;if(!sval(1,m))return false;st.append_stderr=m=="append";}}else for(auto&x:c->stages)x.cwd=v;out=base;return true;}
                        if(method=="env"){if(aa.size()!=2){error="env: expected name and value";return false;}std::string k,v;if(!sval(0,k)||!sval(1,v))return false;for(auto&x:c->stages)x.env[k]=v;out=base;return true;}
                        if(method=="run"){if(!aa.empty()){error="run: command method takes no arguments";return false;}if(std::getenv("NIFT_NO_PROCESS")){error="run: external process execution disabled";return false;}auto pr=nift_run_pipeline(c->stages,true,false);out=json::Document::make_object();out["exit_code"]=json::Document((double)pr.exit_code);out["stdout"]=json::Document(pr.out);out["stderr"]=json::Document(pr.err);out["launched"]=json::Document(pr.launched);return true;}
                    }
                    if(base.is_string() && base.string.rfind("\x1fnift:file:",0)==0) {
                        auto fit=file_instances_.find(base.string.substr(11)); if(fit==file_instances_.end()){error="file: invalid FileValue";return false;} auto f=fit->second;
                        auto can_read=[&](){return f->mode=="r"||f->mode=="rw";}; auto can_write=[&](){return f->mode=="w"||f->mode=="a"||f->mode=="rw";};
                        auto need_open=[&](){if(!f->open){error=method+": file is not open";return false;}return true;};
                        auto need_read=[&](){if(!need_open())return false;if(!can_read()){error=method+": file is not open for reading";return false;}return true;};
                        auto need_write=[&](){if(!need_open())return false;if(!can_write()){error=method+": file is not open for writing";return false;}return true;};
                        auto sarg=[&](size_t i,std::string& x){json::Document v;if(!eval_arg(i,v)||!v.is_string()){error=method+": expected string argument";return false;}x=v.string;return true;};
                        auto dirty=[&](){f->dirty=f->working!=f->saved;};
                        if(method=="path"){if(!no_args())return false;out=json::Document(f->path.generic_string());return true;}
                        if(method=="exists"){if(!no_args())return false;std::error_code ec;out=json::Document(fs::exists(f->path,ec)&&!ec);return true;}
                        if(method=="open"){
                            if(f->open){error="open: file is already open";return false;}if(args.size()>1){error="open: expected zero or one mode";return false;}std::string m="r";if(args.size()==1&&!sarg(0,m))return false;
                            if(m!="r"&&m!="w"&&m!="a"&&m!="rw"){error="open: mode must be r, w, a or rw";return false;}std::error_code ec;bool ex=fs::exists(f->path,ec)&&!ec;
                            if((m=="r"||m=="rw")&&!ex){error="open: file does not exist";return false;}if(ex&&fs::is_directory(f->path,ec)){error="open: path is a directory";return false;}
                            std::string data;if(ex){std::ifstream in(f->path,std::ios::binary);if(!in){error="open: cannot read path";return false;}std::ostringstream ss;ss<<in.rdbuf();data=ss.str();}
                            f->mode=m;f->existed_at_open=ex;f->saved=data;f->working=m=="w"?std::string():data;f->cursor=m=="a"?f->working.size():0;f->dirty=(m=="w")||(m=="a"&&!ex);f->open=true;out=json::Document(nullptr);return true;
                        }
                        if(method=="close"){if(!no_args()||!need_open())return false;if(f->dirty){error="close: file has unsaved changes; save() or revert() first";return false;}f->open=false;f->mode.clear();f->working.clear();f->saved.clear();f->cursor=0;out=json::Document(nullptr);return true;}
                        if(method=="modified"){if(!no_args()||!need_open())return false;out=json::Document(f->dirty);return true;}
                        if(method=="tell"){if(!no_args()||!need_open())return false;out=json::Document((double)f->cursor);return true;}
                        if(method=="seek"){if(args.size()!=1||!need_open()){if(args.size()!=1&&error.empty())error="seek: expected byte position";return false;}json::Document n;if(!eval_arg(0,n)||!n.is_number()||std::trunc(n.num)!=n.num||n.num<0||n.num>(double)f->working.size()){error="seek: byte position out of range";return false;}f->cursor=(size_t)n.num;out=json::Document(nullptr);return true;}
                        if(method=="eof"){if(!no_args()||!need_read())return false;out=json::Document(f->cursor>=f->working.size());return true;}
                        if(method=="read"||method=="read_all"){if(!need_read())return false;size_t base=std::min(f->cursor,f->working.size());size_t n=f->working.size()-base;if(method=="read"&&!args.empty()){if(args.size()!=1){error="read: expected zero or one byte count";return false;}json::Document d;if(!eval_arg(0,d)||!d.is_number()||std::trunc(d.num)!=d.num||d.num<0){error="read: invalid byte count";return false;}n=std::min(n,(size_t)d.num);}else if(method=="read_all"&&!args.empty()){error="read_all: expected no arguments";return false;}out=json::Document(f->working.substr(base,n));f->cursor=base+n;return true;}
                        if(method=="read_line"){if(!no_args()||!need_read())return false;if(f->cursor>=f->working.size()){out=json::Document(nullptr);return true;}auto e=f->working.find('\n',f->cursor);size_t z=e==std::string::npos?f->working.size():e;std::string line=f->working.substr(f->cursor,z-f->cursor);if(!line.empty()&&line.back()=='\r')line.pop_back();f->cursor=e==std::string::npos?f->working.size():e+1;out=json::Document(line);return true;}
                        if(method=="read_val"){if(!no_args()||!need_read())return false;while(f->cursor<f->working.size()&&std::isspace((unsigned char)f->working[f->cursor]))++f->cursor;if(f->cursor>=f->working.size()){out=json::Document(nullptr);return true;}size_t st=f->cursor;char first=f->working[f->cursor];if(first=='"'||first=='['||first=='{'){char op=first,cl=first=='['?']':first=='{'?'}':'"';int dep=0;bool qd=false,esc=false;while(f->cursor<f->working.size()){char c=f->working[f->cursor++];if(op=='"'){if(esc){esc=false;continue;}if(c=='\\'){esc=true;continue;}if(f->cursor>st+1&&c=='"')break;}else{if(qd){if(esc)esc=false;else if(c=='\\')esc=true;else if(c=='"')qd=false;}else if(c=='"')qd=true;else if(c==op)++dep;else if(c==cl&&--dep==0)break;}}}else while(f->cursor<f->working.size()&&!std::isspace((unsigned char)f->working[f->cursor]))++f->cursor;std::string tok=f->working.substr(st,f->cursor-st);json::Document v;if(!eval(tok,v,depth+1)){error="read_val: "+error;return false;}out=v;return true;}
                        if(method=="write_val"){if(args.size()!=1||!need_write()){if(args.size()!=1&&error.empty())error="write_val: expected one value";return false;}json::Document v;if(!eval_arg(0,v))return false;std::string d;if(!serialize_value(v,false,d,error))return false;d+="\n";size_t base=std::min(f->cursor,f->working.size());size_t ov=std::min(d.size(),f->working.size()-base);f->working.replace(base,ov,d);f->cursor=base+d.size();dirty();out=json::Document(nullptr);return true;}
                        if(method=="write"||method=="write_line"){if(args.size()!=1||!need_write()){if(args.size()!=1&&error.empty())error=method+": expected one value";return false;}json::Document v;if(!eval_arg(0,v))return false;if(v.is_array()||v.is_object()||(v.is_string()&&v.string.rfind("\x1fnift:",0)==0)){error=method+": value is not directly renderable";return false;}std::string d=render_expression_value(v);if(method=="write_line")d+='\n';size_t base=std::min(f->cursor,f->working.size());size_t ov=std::min(d.size(),f->working.size()-base);f->working.replace(base,ov,d);f->cursor=base+d.size();dirty();out=json::Document(nullptr);return true;}
                        if(method=="flush"){if(!no_args()||!need_write())return false;out=json::Document(nullptr);return true;}
                        if(method=="replace"||method=="replace_once"){if(args.size()!=2||!need_write()){if(args.size()!=2&&error.empty())error=method+": expected old and replacement strings";return false;}std::string a,b;if(!sarg(0,a)||!sarg(1,b))return false;if(a.empty()){error=method+": old string must not be empty";return false;}size_t count=0,pos=0;while((pos=f->working.find(a,pos))!=std::string::npos){++count;pos+=a.size();}if(method=="replace_once"&&count!=1){error="replace_once: expected exactly one match, found "+std::to_string(count);return false;}pos=0;while((pos=f->working.find(a,pos))!=std::string::npos){f->working.replace(pos,a.size(),b);pos+=b.size();if(method=="replace_once")break;}if(f->cursor>f->working.size())f->cursor=f->working.size();dirty();out=method=="replace"?json::Document((double)count):json::Document(nullptr);return true;}
                        if(method=="insert"||method=="insert"||method=="insert_before"||method=="insert_after"||method=="prepend"||method=="append"){if(!need_write())return false;size_t pos=0;std::string d;if(method=="prepend"||method=="append"){if(args.size()!=1||!sarg(0,d)){if(args.size()!=1&&error.empty())error=method+": expected text";return false;}pos=method=="prepend"?0:f->working.size();}else if(method=="insert"){if(args.size()!=2){error="insert: expected byte position and text";return false;}json::Document n;if(!eval_arg(0,n)||!n.is_number()||std::trunc(n.num)!=n.num||n.num<0||n.num>(double)f->working.size()||!sarg(1,d)){error="insert: invalid byte position or text";return false;}pos=(size_t)n.num;}else{if(args.size()!=2){error=method+": expected anchor and text";return false;}std::string a;if(!sarg(0,a)||!sarg(1,d))return false;if(a.empty()){error=method+": anchor must not be empty";return false;}auto at=f->working.find(a);if(at==std::string::npos||f->working.find(a,at+a.size())!=std::string::npos){error=method+": expected exactly one anchor";return false;}pos=at+(method=="insert_after"?a.size():0);}f->working.insert(pos,d);if(f->cursor>f->working.size())f->cursor=f->working.size();dirty();out=json::Document(nullptr);return true;}
                        if(method=="revert"){if(!no_args()||!need_write())return false;f->working=f->saved;f->cursor=0;f->dirty=false;out=json::Document(nullptr);return true;}
                        if(method=="save"){if(!no_args()||!need_write())return false;if(!f->dirty){out=json::Document(nullptr);return true;}std::error_code ec;auto parent=f->path.parent_path();if(!parent.empty()&&!fs::exists(parent,ec)){error="save: parent directory does not exist";return false;}fs::perms perms=fs::perms::unknown;if(fs::exists(f->path,ec)&&!ec)perms=fs::status(f->path,ec).permissions();fs::path tmp=f->path;tmp+=".nift-tmp-"+std::to_string((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count());{std::ofstream o(tmp,std::ios::binary|std::ios::trunc);if(!o){error="save: cannot create temporary file";return false;}o.write(f->working.data(),(std::streamsize)f->working.size());o.flush();if(!o){o.close();fs::remove(tmp,ec);error="save: temporary write failed";return false;}}if(perms!=fs::perms::unknown)fs::permissions(tmp,perms,ec);ec.clear();fs::rename(tmp,f->path,ec);if(ec){fs::remove(tmp);error="save: atomic replace failed: "+ec.message();return false;}f->saved=f->working;f->dirty=false;f->existed_at_open=true;out=json::Document(nullptr);return true;}
                        if(method=="cat"){if(!no_args()||!need_read())return false;{static std::mutex file_cat_mutex;std::lock_guard<std::mutex> lock(file_cat_mutex);std::cout<<f->working;std::cout.flush();}out=json::Document(nullptr);return true;}
                        if(method=="copy"||method=="move"||method=="remove"){if(f->open){error=method+": file must be closed";return false;}if((method=="remove"&&!args.empty())||(method!="remove"&&args.size()!=1)){error=method+": invalid arguments";return false;}std::error_code ec;if(method=="remove"){if(fs::is_directory(f->path,ec)){error="remove: directories are not removed recursively";return false;}if(!fs::remove(f->path,ec)&&ec){error="remove: "+ec.message();return false;}out=json::Document(nullptr);return true;}std::string raw;if(!sarg(0,raw))return false;fs::path dest(raw);if(dest.is_relative())dest=(standalone_script_host_?fs::current_path():host_.root())/dest;dest=fs::absolute(dest).lexically_normal();if(!standalone_script_host_&&!host_.root().empty()&&!filesystem::path_within(fs::absolute(host_.root()).lexically_normal(),dest)){error=method+": path must stay inside the Nift project";return false;}if(method=="move")fs::rename(f->path,dest,ec);else fs::copy_file(f->path,dest,fs::copy_options::overwrite_existing,ec);if(ec){error=method+": "+ec.message();return false;}if(method=="move")f->path=dest;auto nf=std::make_shared<FileInstance>();nf->path=dest;auto id=std::to_string(next_file_instance_id_++);file_instances_[id]=nf;out=json::Document(std::string("\x1fnift:file:")+id);return true;}
                    }
                    if(base.is_string() && base.string.rfind("\x1fnift:",0)!=0) {
                        const std::string& str=base.string;
                        auto utf8_parts=[&](std::vector<std::string>& parts)->bool{
                            for(std::size_t i=0;i<str.size();){unsigned char c=(unsigned char)str[i];std::size_t n=c<0x80?1:(c>=0xC2&&c<=0xDF?2:(c>=0xE0&&c<=0xEF?3:(c>=0xF0&&c<=0xF4?4:0)));if(!n||i+n>str.size()){error="split: invalid UTF-8";return false;}for(std::size_t j=1;j<n;++j)if(((unsigned char)str[i+j]&0xC0)!=0x80){error="split: invalid UTF-8";return false;}parts.push_back(str.substr(i,n));i+=n;}return true;};
                        auto utf8_length=[&](std::size_t& count)->bool{std::vector<std::string> p;if(!utf8_parts(p))return false;count=p.size();return true;};
                        if(method=="length"){if(!no_args())return false;std::size_t n=0;if(!utf8_length(n))return false;out=json::Document((double)n);return true;}
                        if(method=="empty"){if(!no_args())return false;out=json::Document(str.empty());return true;}
                        if(method=="split"){
                            if(args.size()!=1){error="split: expected one delimiter";return false;}json::Document d;if(!eval_arg(0,d)||!d.is_string()){error="split: delimiter must be a string";return false;}out=json::Document::make_array();
                            if(d.string.empty()){std::vector<std::string> parts;if(!utf8_parts(parts))return false;for(auto& x:parts)out.array.emplace_back(x);return true;}
                            std::size_t pos=0;while(true){auto at=str.find(d.string,pos);if(at==std::string::npos){out.array.emplace_back(str.substr(pos));break;}out.array.emplace_back(str.substr(pos,at-pos));pos=at+d.string.size();}return true;
                        }
                        if(method=="index_of"||method=="last_index_of"||method=="contains"||method=="starts_with"||method=="ends_with"){
                            if(args.size()!=1){error=method+": expected one string";return false;}json::Document n;if(!eval_arg(0,n)||!n.is_string()){error=method+": argument must be a string";return false;}
                            if(method=="contains"){out=json::Document(str.find(n.string)!=std::string::npos);return true;}
                            if(method=="starts_with"){out=json::Document(str.rfind(n.string,0)==0);return true;}
                            if(method=="ends_with"){out=json::Document(n.string.size()<=str.size()&&str.compare(str.size()-n.string.size(),n.string.size(),n.string)==0);return true;}
                            auto at=method=="index_of"?str.find(n.string):str.rfind(n.string);out=json::Document(at==std::string::npos?-1.0:(double)at);return true;
                        }
                        if(method=="trim"||method=="trim_start"||method=="trim_end"){
                            if(!no_args())return false;std::size_t a=0,b=str.size();if(method!="trim_end")while(a<b&&std::isspace((unsigned char)str[a]))++a;if(method!="trim_start")while(b>a&&std::isspace((unsigned char)str[b-1]))--b;out=json::Document(str.substr(a,b-a));return true;
                        }
                        if(method=="to_lower"||method=="to_upper"){
                            if(!no_args())return false;std::string r=str;for(char& c:r)c=(char)(method=="to_lower"?std::tolower((unsigned char)c):std::toupper((unsigned char)c));out=json::Document(r);return true;
                        }
                        if(method=="replace"){
                            if(args.size()!=2){error="replace: expected old and replacement strings";return false;}json::Document a,b;if(!eval_arg(0,a)||!eval_arg(1,b)||!a.is_string()||!b.is_string()){error="replace: arguments must be strings";return false;}if(a.string.empty()){error="replace: old string must not be empty";return false;}std::string r=str;std::size_t pos=0;while((pos=r.find(a.string,pos))!=std::string::npos){r.replace(pos,a.string.size(),b.string);pos+=b.string.size();}out=json::Document(r);return true;
                        }
                        if(method=="to_int"){
                            if(!no_args())return false;if(str.empty()){error="to_int: invalid integer";return false;}const char* data=str.data();std::size_t size=str.size();if(str[0]=='+'){data+=1;size-=1;}if(size==0){error="to_int: invalid integer";return false;}std::int64_t v=0;auto pr=std::from_chars(data,data+size,v);if(pr.ec!=std::errc()||pr.ptr!=data+size){error="to_int: invalid or out-of-range integer";return false;}out=json::Document((double)v);if(v>9007199254740992LL||v<-9007199254740992LL){out.type=json::Type::StrNumber;out.string=std::to_string(v);}return true;
                        }
                        if(method=="to_string"){if(!no_args())return false;out=json::Document(str);return true;}
                        if(method=="to_double"){
                            if(!no_args())return false;if(str.empty()||std::isspace((unsigned char)str.front())||std::isspace((unsigned char)str.back())){error="to_double: invalid number";return false;}if(str.find("0x")!=std::string::npos||str.find("0X")!=std::string::npos){error="to_double: invalid number";return false;}char* end=nullptr;errno=0;double v=std::strtod(str.c_str(),&end);if(errno==ERANGE||!end||end!=str.c_str()+str.size()||!std::isfinite(v)){error="to_double: invalid or out-of-range number";return false;}out=json::Document(v);return true;
                        }
                        if(method=="substr"){
                            if(args.empty()||args.size()>2){error="substr: expected pos and optional length";return false;}json::Document p,n;if(!eval_arg(0,p)||!p.is_number()||std::trunc(p.num)!=p.num||p.num<0){error="substr: invalid pos";return false;}std::size_t at=(std::size_t)p.num;if(at>str.size())at=str.size();std::size_t len=std::string::npos;if(args.size()==2){if(!eval_arg(1,n)||!n.is_number()||std::trunc(n.num)!=n.num||n.num<0){error="substr: invalid length";return false;}len=(std::size_t)n.num;}out=json::Document(str.substr(at,len));return true;
                        }
                    }
                    if(method=="to_string" && base.is_number()) { if(!no_args())return false;out=json::Document(render_expression_value(base));return true; }
                    if(method=="to_string" && base.is_bool()) { if(!no_args())return false;out=json::Document(base.boolean?"true":"false");return true; }
                    if(method=="to_string" && base.is_null()) { if(!no_args())return false;out=json::Document("null");return true; }
                    if(base.is_number()) {
                        if(method=="abs"){if(!no_args())return false;out=json::Document(std::fabs(base.num));return true;}
                        if(method=="floor"){if(!no_args())return false;out=json::Document(std::floor(base.num));return true;}
                        if(method=="ceil"){if(!no_args())return false;out=json::Document(std::ceil(base.num));return true;}
                        if(method=="round"){if(!no_args())return false;out=json::Document(std::round(base.num));return true;}
                    }
                    if(base.is_object()) {
                        if(method=="size"||method=="empty"||method=="keys"||method=="values"||method=="entries") {
                            if(!no_args())return false;
                            if(method=="size"){out=json::Document((double)base.object.size());return true;}
                            if(method=="empty"){out=json::Document(base.object.empty());return true;}
                            out=json::Document::make_array();
                            for(const auto& kv:base.object){
                                if(method=="keys")out.array.emplace_back(kv.first);
                                else if(method=="values")out.array.push_back(kv.second);
                                else { json::Document e=json::Document::make_object(); e["key"]=json::Document(kv.first); e["value"]=kv.second; out.array.push_back(std::move(e)); }
                            }
                            return true;
                        }
                        if(method=="pick"||method=="omit") {
                            if(args.size()!=1){error=method+": expected one array of keys";return false;}
                            json::Document keys;if(!eval_arg(0,keys)||!keys.is_array()){error=method+": keys must be an array";return false;}
                            std::vector<std::string> wanted;
                            for(const auto& k:keys.array){if(!k.is_string()){error=method+": keys must be strings";return false;}if(std::find(wanted.begin(),wanted.end(),k.string)==wanted.end())wanted.push_back(k.string);}
                            json::Document result=json::Document::make_object();
                            auto wanted_key=[&](const std::string& key){for(const auto& pat:wanted){if(nift_glob_has_magic(pat)?nift_glob_component_match(pat,key):pat==key)return true;}return false;};
                            if(method=="pick"){std::vector<std::string> picked;for(const auto& pat:wanted){auto pat_match=[&](const std::string& key){return nift_glob_has_magic(pat)?nift_glob_component_match(pat,key):pat==key;};for(const auto& kv:base.object){if(std::find(picked.begin(),picked.end(),kv.first)==picked.end()&&pat_match(kv.first)){result[kv.first]=kv.second;picked.push_back(kv.first);}}}}
                            else {for(const auto& kv:base.object)if(!wanted_key(kv.first))result[kv.first]=kv.second;}
                            out=std::move(result);return true;
                        }
                        if(method=="has"||method=="get") {
                            if((method=="has"&&args.size()!=1)||(method=="get"&&(args.empty()||args.size()>2))){error=method+": expected key"+(method=="get"?" and optional default":"");return false;}
                            json::Document k;if(!eval_arg(0,k)||!k.is_string()){error=method+": key must be a string";return false;}
                            auto it=std::find_if(base.object.begin(),base.object.end(),[&](const auto& kv){return kv.first==k.string;});
                            if(method=="has"){out=json::Document(it!=base.object.end());return true;}
                            if(it!=base.object.end()){out=it->second;return true;}
                            if(args.size()==2)return eval_arg(1,out);
                            out=json::Document(nullptr);return true;
                        }
                        if(method=="merge_deep") {
                            if(args.size()!=1){error="merge_deep: expected one object";return false;}
                            json::Document rhs;if(!eval_arg(0,rhs)||!rhs.is_object()){error="merge_deep: argument must be an object";return false;}
                            std::function<bool(const json::Document&,const json::Document&,json::Document&,int)> merge_rec;
                            merge_rec=[&](const json::Document& lhs,const json::Document& r,json::Document& result,int level)->bool{
                                if(level>64){error="merge_deep: maximum merge depth exceeded";return false;}
                                if(!(lhs.is_object()&&r.is_object())){result=r;return true;}
                                result=lhs;
                                for(const auto& kv:r.object){
                                    auto it=std::find_if(result.object.begin(),result.object.end(),[&](const auto& x){return x.first==kv.first;});
                                    if(it!=result.object.end()&&it->second.is_object()&&kv.second.is_object()){json::Document merged;if(!merge_rec(it->second,kv.second,merged,level+1))return false;it->second=std::move(merged);}
                                    else result[kv.first]=kv.second;
                                }
                                return true;
                            };
                            json::Document result;if(!merge_rec(base,rhs,result,0))return false;out=std::move(result);return true;
                        }
                        if(method=="merge") {
                            if(args.size()!=1){error="merge: expected one object";return false;}json::Document rhs;if(!eval_arg(0,rhs)||!rhs.is_object()){error="merge: argument must be an object";return false;}
                            out=base;for(const auto& kv:rhs.object)out[kv.first]=kv.second;return true;
                        }
                    }
                    if(base.is_array()) {
                        const auto& a=base.array;
                        if(method=="from_entries"){
                            if(!no_args())return false;
                            json::Document result=json::Document::make_object();
                            // O(n) construction: order-preserving vector plus a hash map
                            // for duplicate detection instead of linear object scans.
                            std::unordered_map<std::string,std::size_t> seen_keys; result.object.reserve(a.size());
                            for(const auto& entry:a){
                                if(!entry.is_object()||entry.object.size()!=2||!entry.has("key")||!entry.has("value")){error="from_entries: each entry must be an object containing exactly key and value";return false;}
                                const json::Document *key=nullptr,*value=nullptr;
                                for(const auto& kv:entry.object){if(kv.first=="key")key=&kv.second;else if(kv.first=="value")value=&kv.second;}
                                std::string rendered;if(!key||!value||!generated_object_key(*key,rendered))return false;
                                if(seen_keys.count(rendered)){error="from_entries: duplicate generated key '"+rendered+"'";return false;}
                                seen_keys[rendered]=result.object.size(); result.object.emplace_back(rendered,*value);
                            }
                            out=std::move(result);return true;
                        }
                        auto invoke_value_cb=[&](const json::Document& cb,const std::vector<json::Document>& av,json::Document& result)->bool{
                            if(!cb.is_string()||cb.string.rfind("\x1fnift:callable:",0)!=0){error="transformation: callback must be callable";return false;}
                            const std::string tag=cb.string;
                            if(tag.rfind("\x1fnift:callable:lambda:",0)==0){
                                auto li=lambda_instances_.find(tag.substr(22));if(li==lambda_instances_.end()){error="invalid lambda";return false;}auto fn=li->second;
                                if(callable_call_depth_>=kMaxCallableDepth){error="callable recursion depth exceeded";return false;}++callable_call_depth_;
                                if(fn->variadic_param.empty()?av.size()!=fn->params.size():av.size()<fn->params.size()){--callable_call_depth_;error="callback argument count mismatch";return false;}
                                const auto saved_lambda_env=active_module_env_;if(fn->module_env)active_module_env_=fn->module_env;
                                push_variable_scope();auto& sc=variable_scopes_.back();for(const auto& kv:fn->captures)sc[kv.first]=kv.second;
                                for(std::size_t ai=0;ai<fn->params.size();++ai){auto sp=std::make_shared<json::Document>(av[ai]);sc[fn->params[ai]]=VariableBinding{sp,nift_binding_type(*sp),true,false};}
                                if(!fn->variadic_param.empty()){json::Document rest=json::Document::make_array();for(std::size_t ai=fn->params.size();ai<av.size();++ai)rest.array.push_back(av[ai]);auto sp=std::make_shared<json::Document>(std::move(rest));sc[fn->variadic_param]=VariableBinding{sp,nift_binding_type(*sp),true,false};}
                                bool okcall=true;
                                if(fn->block){std::string bt=trim_copy(fn->body);const bool rendered=!bt.empty()&&bt.front()=='<';if(rendered){auto nested=parse(fn->body,fn->source_path,1);if(!nested.ok){error=nested.error.message;okcall=false;}else result=json::Document(nested.output);}else{okcall=eval(fn->body,result,depth+1);if(!okcall&&error.rfind("callable recursion depth exceeded",0)!=0)error="lambda body error: "+error;}}
                                else{okcall=eval(fn->body,result,depth+1);if(!okcall&&error.rfind("callable recursion depth exceeded",0)!=0)error="lambda body error: "+error;}
                                pop_variable_scope();active_module_env_=saved_lambda_env;--callable_call_depth_;return okcall;
                            }
                            if(tag.rfind("\x1fnift:callable:named:",0)==0){
                                auto ci=callables_.find(tag.substr(21));if(ci==callables_.end()){error="invalid callable";return false;}const Callable& callee=ci->second;
                                if(callable_call_depth_>=kMaxCallableDepth){error="callable recursion depth exceeded";return false;}++callable_call_depth_;
                                if(callee.variadic_param.empty()?av.size()!=callee.params.size():av.size()<callee.params.size()){--callable_call_depth_;error="callback argument count mismatch";return false;}
                                const auto saved_env=active_module_env_;if(callee.module_env)active_module_env_=callee.module_env;
                                push_variable_scope();auto& scope=variable_scopes_.back();if(active_module_env_){for(const auto& kv:active_module_env_->vars){if(!scope.count(kv.first))scope[kv.first]=kv.second;}}
                                for(std::size_t ai=0;ai<callee.params.size();++ai){auto sp=std::make_shared<json::Document>(av[ai]);scope.emplace(callee.params[ai],VariableBinding{sp,nift_binding_type(*sp),true,false});}
                                if(!callee.variadic_param.empty()){json::Document rest=json::Document::make_array();for(std::size_t ai=callee.params.size();ai<av.size();++ai)rest.array.push_back(av[ai]);auto sp=std::make_shared<json::Document>(std::move(rest));scope.emplace(callee.variadic_param,VariableBinding{sp,nift_binding_type(*sp),true,false});}
                                const int caller_loop_depth=loop_depth_;loop_depth_=0;pending_control_={};
                                bool call_ok=true;std::string call_error;
                                if(callee.fragment){const bool saved_fragment=in_fragment_body_;in_fragment_body_=true;auto nested=parse(callee.body,callee.source_path,1);in_fragment_body_=saved_fragment;if(!nested.ok){call_ok=false;call_error=nested.error.message;}else result=json::Document(nested.output);}
                                else{++function_call_depth_;auto body_result=execute_native_program(callee.body,callee.source_path,1);--function_call_depth_;if(!body_result.ok){call_ok=false;call_error=body_result.error.message;}else if(pending_control_.kind==ControlFlow::None)result=json::Document(nullptr);else{result=pending_control_.value?std::move(*pending_control_.value):json::Document(nullptr);pending_control_={};}}
                                loop_depth_=caller_loop_depth;pop_variable_scope();active_module_env_=saved_env;--callable_call_depth_;
                                if(!call_ok){error=call_error;return false;}return true;
                            }
                            error="callback is not a callable";return false;};
                        auto invoke_value_callback=[&](const std::string& cbexpr,const std::vector<json::Document>& av,json::Document& result)->bool{
                            json::Document cb;if(!eval(cbexpr,cb,depth+1))return false;if(!cb.is_string()||cb.string.rfind("\x1fnift:callable:",0)!=0){error="transformation: callback must be callable";return false;}
                            return invoke_value_cb(cb,av,result);};
                        if(method=="index_by"){
                            if(args.size()!=1){error="index_by: expected one selector";return false;}
                            json::Document result=json::Document::make_object();
                            std::unordered_map<std::string,std::size_t> seen_keys; result.object.reserve(a.size());
                            for(const auto& v:a){
                                json::Document key;if(!invoke_value_callback(args[0],{v},key))return false;
                                std::string rendered;if(!generated_object_key(key,rendered))return false;
                                if(seen_keys.count(rendered)){error="index_by: duplicate generated key '"+rendered+"'";return false;}
                                seen_keys[rendered]=result.object.size(); result.object.emplace_back(rendered,v);
                            }
                            out=std::move(result);return true;
                        }
                        if(method=="partition"){
                            if(args.size()!=1){error="partition: expected one predicate";return false;}
                            json::Document result=json::Document::make_object(); result["matched"]=json::Document::make_array(); result["unmatched"]=json::Document::make_array();
                            for(const auto& v:a){json::Document r;if(!invoke_value_callback(args[0],{v},r))return false;if(!r.is_bool()){error="partition: callback must return bool";return false;}(r.boolean?result["matched"]:result["unmatched"]).push_back(v);}out=std::move(result);return true;
                        }
                        if(method=="unique_by"){
                            if(args.size()!=1){error="unique_by: expected one selector";return false;} out=json::Document::make_array(); std::unordered_set<std::string> seen;
                            // Canonical structural form exactly mirroring structural_equal,
                            // so O(n) dedup preserves the same semantics as the previous
                            // linear structural scan (numbers by exact double equality,
                            // object keys order-insensitive, arrays order-sensitive).
                            std::function<void(const json::Document&,std::string&)> canonical_key;
                            canonical_key=[&](const json::Document& v,std::string& out){
                                if(v.is_null()){out+="z";return;}
                                if(v.is_bool()){out+=v.boolean?"bt":"bf";return;}
                                if(v.is_number()){out+="n";char buf[64];auto r=std::to_chars(buf,buf+sizeof(buf),v.num);out.append(buf,r.ptr);return;}
                                if(v.is_string()){if(v.string.rfind("\x1fnift:",0)==0){out+="i";out+=v.string;return;}out+="s";out+=std::to_string(v.string.size());out+=':';out+=v.string;return;}
                                if(v.is_array()){out+="a(";for(const auto& e:v.array){canonical_key(e,out);out+=',';}out+=')';return;}
                                if(v.is_object()){
                                    out+="o(";std::vector<std::pair<std::string,const json::Document*>> pairs;
                                    for(const auto& kv:v.object)pairs.push_back({kv.first,&kv.second});
                                    std::sort(pairs.begin(),pairs.end(),[&](const auto& x,const auto& y){return x.first<y.first;});
                                    for(const auto& p:pairs){out+=std::to_string(p.first.size());out+=':';out+=p.first;out+='=';canonical_key(*p.second,out);out+=',';}
                                    out+=')';return;
                                }
                                out+='?';
                            };
                            for(const auto& v:a){json::Document k;if(!invoke_value_callback(args[0],{v},k))return false;std::string ck;canonical_key(k,ck);if(seen.insert(ck).second)out.array.push_back(v);}return true;
                        }
                        if(method=="min_by"||method=="max_by"){
                            if(args.size()!=1){error=method+": expected one selector";return false;}if(a.empty()){error=method+": cannot select from an empty array";return false;}json::Document best_key;if(!invoke_value_callback(args[0],{a.front()},best_key))return false;if(!(best_key.is_number()||best_key.is_string()||best_key.is_bool())){error=method+": key must be scalar";return false;}out=a.front();
                            for(size_t i=1;i<a.size();++i){json::Document k;if(!invoke_value_callback(args[0],{a[i]},k))return false;if(k.type!=best_key.type && !(k.is_number()&&best_key.is_number())){error=method+": keys must be comparable and homogeneous";return false;}bool take=false;if(k.is_number())take=method=="min_by"?k.num<best_key.num:k.num>best_key.num;else if(k.is_string())take=method=="min_by"?k.string<best_key.string:k.string>best_key.string;else if(k.is_bool())take=method=="min_by"?k.boolean<best_key.boolean:k.boolean>best_key.boolean;else{error=method+": key must be scalar";return false;}if(take){best_key=k;out=a[i];}}return true;
                        }
                        if(method=="count_by"){
                            if(args.size()!=1){error="count_by: expected one selector";return false;}json::Document result=json::Document::make_object();
                            std::unordered_map<std::string,std::size_t> pos; result.object.reserve(a.size());
                            for(const auto& v:a){json::Document k;if(!invoke_value_callback(args[0],{v},k))return false;std::string rendered;if(!generated_object_key(k,rendered))return false;auto it=pos.find(rendered);if(it==pos.end()){pos[rendered]=result.object.size();result.object.emplace_back(rendered,json::Document(0.0));result.object.back().second.num+=1;}else result.object[it->second].second.num+=1;}
                            out=std::move(result);return true;
                        }
                        if(method=="take"||method=="drop"||method=="chunk"){
                            if(args.size()!=1){error=method+": expected one count";return false;}json::Document n;if(!eval_arg(0,n)||!n.is_number()||std::trunc(n.num)!=n.num||n.num<0||(method=="chunk"&&n.num==0)){error=method+": count must be "+std::string(method=="chunk"?"a positive":"a non-negative")+" integer";return false;}size_t z=(size_t)n.num;out=json::Document::make_array();if(method=="take"){size_t e=std::min(z,a.size());out.array.assign(a.begin(),a.begin()+e);}else if(method=="drop"){size_t b=std::min(z,a.size());out.array.assign(a.begin()+b,a.end());}else{for(size_t b=0;b<a.size();b+=z){json::Document c=json::Document::make_array();size_t e=std::min(b+z,a.size());c.array.assign(a.begin()+b,a.begin()+e);out.array.push_back(std::move(c));}}return true;
                        }
                        if(method=="group_by_each"){
                            if(args.size()!=1){error="group_by_each: expected one selector";return false;}json::Document result=json::Document::make_object();
                            std::unordered_map<std::string,std::size_t> pos;
                            for(const auto& v:a){json::Document ks;if(!invoke_value_callback(args[0],{v},ks))return false;if(!ks.is_array()){error="group_by_each: selector must return an array";return false;}std::vector<std::string> item_keys;for(const auto& k:ks.array){std::string rendered;if(!generated_object_key(k,rendered))return false;if(std::find(item_keys.begin(),item_keys.end(),rendered)!=item_keys.end())continue;item_keys.push_back(rendered);auto it=pos.find(rendered);if(it==pos.end()){pos[rendered]=result.object.size();result.object.emplace_back(rendered,json::Document::make_array());result.object.back().second.array.push_back(v);}else result.object[it->second].second.array.push_back(v);}}
                            out=std::move(result);return true;
                        }
                        if(method=="sort_by"){
                            if(args.empty() || (args.size()!=1 && args.size()%2!=0)){error="sort_by: expected selector or selector/direction pairs";return false;}
                            struct SortSpec { std::string selector; bool desc=false; }; std::vector<SortSpec> specs;
                            if(args.size()==1) specs.push_back({args[0],false}); else {
                                for(size_t si=0;si<args.size();si+=2){if(si+1>=args.size()||si+1>=aq.size()||!aq[si+1]){error="sort_by: direction must be quoted 'asc' or 'desc'";return false;}std::string d=args[si+1];if(d!="asc"&&d!="desc"){error="sort_by: direction must be 'asc' or 'desc'";return false;}specs.push_back({args[si],d=="desc"});}
                            }
                            struct Decorated { std::vector<json::Document> keys; json::Document value; }; std::vector<Decorated> decorated; decorated.reserve(a.size());
                            for(const auto& v:a){Decorated d;d.value=v;for(const auto& sp:specs){json::Document k;if(!invoke_value_callback(sp.selector,{v},k))return false;if(!(k.is_string()||k.is_number()||k.is_bool())){error="sort_by: key must be scalar";return false;}d.keys.push_back(std::move(k));}decorated.push_back(std::move(d));}
                            auto cmp_key=[&](const json::Document& x,const json::Document& y,int& c)->bool{if(x.is_number()&&y.is_number())c=x.num<y.num?-1:x.num>y.num?1:0;else if(x.is_string()&&y.is_string())c=x.string<y.string?-1:x.string>y.string?1:0;else if(x.is_bool()&&y.is_bool())c=x.boolean<y.boolean?-1:x.boolean>y.boolean?1:0;else{error="sort_by: keys must be comparable and homogeneous";return false;}return true;};
                            bool compare_error=false;std::stable_sort(decorated.begin(),decorated.end(),[&](const Decorated& x,const Decorated& y){for(size_t si=0;si<specs.size();++si){int c=0;if(!cmp_key(x.keys[si],y.keys[si],c)){compare_error=true;return false;}if(c)return specs[si].desc?c>0:c<0;}return false;});if(compare_error)return false;out=json::Document::make_array();for(auto& d:decorated)out.array.push_back(std::move(d.value));return true;
                        }
                        if(method=="map"||method=="filter"||method=="reduce"||method=="any"||method=="all"||method=="find"||method=="find_index"||method=="count"||method=="group_by"){
                            if((method=="reduce"&&args.size()!=2)||(method!="reduce"&&args.size()!=1)){error=method+": invalid arguments";return false;}json::Document arr=json::Document::make_array(),acc;if(method=="reduce"&&!eval_arg(1,acc))return false;double cnt=0;std::vector<std::pair<json::Document,json::Document>> keyed;
                            json::Document cbv;if(!eval(args[0],cbv,depth+1))return false;if(!cbv.is_string()||cbv.string.rfind("\x1fnift:callable:",0)!=0){error="transformation: callback must be callable";return false;}
                            for(size_t vi=0;vi<a.size();++vi){const auto&v=a[vi];json::Document r;if(!invoke_value_cb(cbv,method=="reduce"?std::vector<json::Document>{acc,v}:std::vector<json::Document>{v},r))return false;if(method=="map")arr.array.push_back(r);else if(method=="filter"){if(!r.is_bool()){error="filter: callback must return bool";return false;}if(r.boolean)arr.array.push_back(v);}else if(method=="reduce")acc=r;else if(method=="group_by"){if(!(r.is_string()||r.is_number()||r.is_bool())){error=method+": key must be scalar";return false;}keyed.push_back({r,v});}else{if(!r.is_bool()){error=method+": callback must return bool";return false;}if(method=="any"&&r.boolean){out=json::Document(true);return true;}if(method=="all"&&!r.boolean){out=json::Document(false);return true;}if(method=="find"&&r.boolean){out=v;return true;}if(method=="find_index"&&r.boolean){out=json::Document((double)vi);return true;}if(method=="count"&&r.boolean)cnt+=1;}}
                            if(method=="group_by"){out=json::Document::make_object();for(auto&kv:keyed){std::string k=kv.first.is_string()?kv.first.string:render_expression_value(kv.first);if(!out.has(k))out[k]=json::Document::make_array();out[k].push_back(kv.second);}return true;}
                            if(method=="map"||method=="filter")out=arr;else if(method=="reduce")out=acc;else if(method=="any")out=json::Document(false);else if(method=="all")out=json::Document(true);else if(method=="find"||method=="find_index")out=json::Document(method=="find"?json::Document(nullptr):json::Document(-1.0));else out=json::Document(cnt);return true;}
                        if(method=="unique"||method=="flatten"||method=="sum"||method=="min"||method=="max"){if(!args.empty()){error=method+": expected no arguments";return false;}if(method=="unique"){out=json::Document::make_array();for(const auto&v:a){bool seen=false;for(const auto&w:out.array)if(structural_equal(v,w)){seen=true;break;}if(!seen)out.array.push_back(v);}return true;}if(method=="flatten"){out=json::Document::make_array();for(const auto&v:a){if(v.is_array())out.array.insert(out.array.end(),v.array.begin(),v.array.end());else out.array.push_back(v);}return true;}if(a.empty()){if(method=="sum"){out=json::Document(0.0);return true;}error=method+": cannot aggregate an empty array";return false;}if(method=="sum"){double total=0;for(const auto&v:a){if(!v.is_number()){error="sum: values must be numeric";return false;}total+=v.num;}out=json::Document(total);return true;}out=a.front();for(size_t ai=1;ai<a.size();++ai){const auto&v=a[ai];bool take=false;if(out.is_number()&&v.is_number())take=method=="min"?v.num<out.num:v.num>out.num;else if(out.is_string()&&v.is_string())take=method=="min"?v.string<out.string:v.string>out.string;else{error=method+": values must be comparable and homogeneous";return false;}if(take)out=v;}return true;}
                        if(method=="size"||method=="length"){if(!no_args())return false;out=json::Document((double)a.size());return true;}
                        if(method=="empty"){if(!no_args())return false;out=json::Document(a.empty());return true;}
                        if(method=="first"||method=="last"){if(!no_args())return false;if(a.empty()){error=method+": array is empty";return false;}out=method=="first"?a.front():a.back();return true;}
                        if(method=="indexOf"){if(args.size()!=1){error="indexOf: expected one value";return false;}json::Document v;if(!eval_arg(0,v))return false;std::size_t i=0;for(;i<a.size();++i)if(structural_equal(a[i],v))break;out=json::Document(i<a.size()?static_cast<double>(i):-1.0);return true;}
                        if(method=="contains"){if(args.size()!=1){error="contains: expected one value";return false;}json::Document v;if(!eval_arg(0,v))return false;for(const auto& x:a)if(structural_equal(x,v)){out=json::Document(true);return true;}out=json::Document(false);return true;}
                        if(method=="join"){if(args.size()!=1){error="join: expected separator";return false;}json::Document sep;if(!eval_arg(0,sep)||!sep.is_string()){error="join: separator must be a string";return false;}std::string r;for(std::size_t i=0;i<a.size();++i){if(i)r+=sep.string;if(a[i].is_array()||a[i].is_object()||(a[i].is_string()&&a[i].string.rfind("\x1fnift:",0)==0)){error="join: elements must be renderable scalar values";return false;}r+=render_expression_value(a[i]);}out=json::Document(r);return true;}
                        if(method=="slice"){if(args.empty()||args.size()>2){error="slice: expected start and optional end";return false;}json::Document st,en;if(!eval_arg(0,st)||!st.is_number()||std::trunc(st.num)!=st.num||st.num<0){error="slice: invalid start";return false;}std::size_t b=(std::size_t)st.num,e=a.size();if(args.size()==2){if(!eval_arg(1,en)||!en.is_number()||std::trunc(en.num)!=en.num||en.num<0){error="slice: invalid end";return false;}e=(std::size_t)en.num;}b=std::min(b,a.size());e=std::min(e,a.size());if(e<b)e=b;out=json::Document::make_array();out.array.assign(a.begin()+b,a.begin()+e);return true;}
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
                out=json::Document(std::string("\x1fnift:collection:")+id);return true;
            }
        }
        if (text.rfind("same(",0)==0 && text.back()==')') {
            bool ok=false; auto args=parse_parameters(text.substr(5,text.size()-6),ok); if(!ok||args.size()!=2){error="same: expected two values";return false;}
            const std::string an=trim_copy(args[0]),bn=trim_copy(args[1]); VariableBinding* ab=find_binding(an);VariableBinding* bb=find_binding(bn);
            if(ab&&bb&&ab->value&&bb->value&&ab->value->is_array()&&bb->value->is_array()){out=json::Document(ab->value==bb->value);return true;}
            json::Document a,b;if(!eval(args[0],a,depth+1)||!eval(args[1],b,depth+1))return false;
            auto identity=[](const json::Document& v)->std::string{if(!v.is_string())return {}; if(v.string.rfind("\x1fnift:struct:",0)==0||v.string.rfind("\x1fnift:callable:",0)==0||v.string.rfind("\x1fnift:collection:",0)==0||v.string.rfind("\x1fnift:file:",0)==0||v.string.rfind("\x1fnift:stream:",0)==0)return v.string;return {};};
            const auto ai=identity(a),bi=identity(b); if(ai.empty()||bi.empty()){error="same: operands must be identity-bearing values";return false;} out=json::Document(ai==bi);return true;
        }

        {
            const auto lp=text.find('(');
            if(lp!=std::string::npos&&text.back()==')'){std::size_t call_close=0;const std::string target=trim_copy(text.substr(0,lp));const auto dot=target.rfind('.');if(dot!=std::string::npos&&find_balanced(text,lp,'(',')',call_close)&&call_close==text.size()-1){
                const std::string root=target.substr(0,dot), method=target.substr(dot+1); VariableBinding* rb=find_binding(root);
                if(rb&&rb->value&&rb->value->is_string()&&rb->value->string.rfind("\x1fnift:collection:",0)==0){
                    auto ci=collection_instances_.find(rb->value->string.substr(17));if(ci==collection_instances_.end()){error="invalid collection";return false;}auto c=ci->second;
                    bool ok=false;std::vector<bool> quoted_args;auto args=parse_parameters(text.substr(lp+1,text.size()-lp-2),ok,&quoted_args);if(!ok){error="malformed collection method arguments";return false;}
                    auto need_mut=[&](){if(!rb->mutable_binding){error="cannot mutate const collection: "+root;return false;}return true;};
                    auto scalar_order=[](const json::Document&a,const json::Document&b,int& cmp){if(a.is_number()&&b.is_number()){cmp=a.num<b.num?-1:a.num>b.num?1:0;return true;}if(a.is_string()&&b.is_string()){cmp=a.string<b.string?-1:a.string>b.string?1:0;return true;}return false;};
                    auto invoke_callback=[&](const std::string& cbexpr,const std::vector<json::Document>& av,json::Document& result)->bool{
                        json::Document cb;if(!eval(cbexpr,cb,depth+1))return false;
                        if(!cb.is_string()||cb.string.rfind("\x1fnift:callable:",0)!=0){error="transformation: callback must be callable";return false;}
                        push_variable_scope();auto& sc=variable_scopes_.back();
                        auto csp=std::make_shared<json::Document>(cb);sc["__nift_cb"]=VariableBinding{csp,nift_binding_type(*csp),false,false};
                        std::string call="__nift_cb(";
                        for(size_t i=0;i<av.size();++i){if(i)call+=",";std::string n="__nift_arg"+std::to_string(i);auto sp=std::make_shared<json::Document>(av[i]);sc[n]=VariableBinding{sp,nift_binding_type(*sp),false,false};call+=n;}call+=")";
                        bool r=eval(call,result,depth+1);pop_variable_scope();return r;
                    };
                    if(method=="map"||method=="filter"||method=="reduce"||method=="any"||method=="all"||method=="find"||method=="find_index"||method=="count"||method=="sort_by"||method=="group_by"){
                        if((method=="reduce"&&args.size()!=2)||(method!="reduce"&&args.size()!=1)){error=method+": invalid arguments";return false;}
                        std::vector<std::vector<json::Document>> rows;
                        if(c->kind==CollectionKind::Map||c->kind==CollectionKind::SortedMap){for(const auto&e:c->entries)rows.push_back({e.first,e.second});}
                        else {auto vals=c->values;if(c->kind==CollectionKind::Stack)std::reverse(vals.begin(),vals.end());for(const auto&v:vals)rows.push_back({v});}
                        json::Document arr=json::Document::make_array();json::Document acc;
                        if(method=="reduce"&&!eval(args[1],acc,depth+1))return false;
                        if(method=="all"&&rows.empty()){out=json::Document(true);return true;} if(method=="any"&&rows.empty()){out=json::Document(false);return true;}
                        double cnt=0;
                        for(const auto&row:rows){json::Document r;std::vector<json::Document> ca=row;if(method=="reduce")ca.insert(ca.begin(),acc);if(!invoke_callback(args[0],ca,r))return false;
                            if(method=="map")arr.array.push_back(r);
                            else if(method=="filter"){if(!r.is_bool()){error="filter: callback must return bool";return false;}if(r.boolean)arr.array.push_back(row.size()==1?row[0]:json::Document::make_array());if(r.boolean&&row.size()==2){arr.array.back().array=row;}}
                            else if(method=="reduce")acc=r;
                            else {if(!r.is_bool()){error=method+": callback must return bool";return false;}if(method=="any"&&r.boolean){out=json::Document(true);return true;}if(method=="all"&&!r.boolean){out=json::Document(false);return true;}if(method=="find"&&r.boolean){out=row.size()==1?row[0]:row[1];return true;}if(method=="count"&&r.boolean)cnt+=1;}
                        }
                        if(method=="map"||method=="filter")out=arr;else if(method=="reduce")out=acc;else if(method=="any")out=json::Document(false);else if(method=="all")out=json::Document(true);else if(method=="find")out=json::Document(nullptr);else out=json::Document(cnt);return true;
                    }
                    if(method=="size"||method=="empty"||method=="clear"){if(!args.empty()){error=method+": expected no arguments";return false;}size_t n=(c->kind==CollectionKind::Map||c->kind==CollectionKind::SortedMap)?c->entries.size():c->values.size();if(method=="size")out=json::Document((double)n);else if(method=="empty")out=json::Document(n==0);else{if(!need_mut())return false;c->values.clear();c->entries.clear();c->scalar_keys.clear();c->has_huge_int=false;out=json::Document(nullptr);last_expression_mutation_=true;}return true;}
                    if(c->kind==CollectionKind::Stack||c->kind==CollectionKind::Queue||c->kind==CollectionKind::PriQue){
                        if(method=="push"){if(args.size()!=1){error="push: expected one value";return false;}if(!need_mut())return false;json::Document v;if(quoted_args.size()>0&&quoted_args[0])v=json::Document(args[0]);else if(!eval(args[0],v,depth+1))return false;if(v.is_string()&&(v.string.rfind("\x1fnift:struct:",0)==0||v.string.rfind("\x1fnift:collection:",0)==0)){if(reference_would_cycle(v.string,rb->value->string)){error="push would create a cyclic reference";return false;}}c->values.push_back(v);if(c->kind==CollectionKind::PriQue){std::stable_sort(c->values.begin(),c->values.end(),[&](const auto&a,const auto&b){int z=0;return scalar_order(a,b,z)&&z<0;});}out=v;last_expression_mutation_=true;return true;}
                        if(method=="pop"||method=="top"||method=="front"){if(!args.empty()){error=method+": expected no arguments";return false;}if(c->values.empty()){error=method+": collection is empty";return false;}size_t i=(c->kind==CollectionKind::Stack&&(method=="pop"||method=="top"))?c->values.size()-1:0;out=c->values[i];if(method=="pop"){if(!need_mut())return false;c->values.erase(c->values.begin()+i);last_expression_mutation_=true;}return true;}
                        if(method=="contains"){if(args.size()!=1){error="contains: expected one value";return false;}json::Document v;if(quoted_args.size()>0&&quoted_args[0])v=json::Document(args[0]);else if(!eval(args[0],v,depth+1))return false;bool f=false;for(auto&w:c->values)if(structural_equal(v,w)){f=true;break;}out=json::Document(f);return true;}
                    }
                    if(c->kind==CollectionKind::Set||c->kind==CollectionKind::SortedSet){
                        if(method=="add"||method=="contains"||method=="remove"){if(args.size()!=1){error=method+": expected one value";return false;}json::Document v;if(quoted_args.size()>0&&quoted_args[0])v=json::Document(args[0]);else if(!eval(args[0],v,depth+1))return false;if(!(v.is_bool()||v.is_number()||v.is_string())){error=method+": set values must be bool, number, or string";return false;}
                        auto set_scalar_key=[&](const json::Document& x)->std::string{if(x.is_bool())return std::string("b")+(x.boolean?"1":"0");if(x.is_string()){if(x.string.rfind("\x1fnift:",0)==0)return "";return std::string("s")+x.string;}if(x.type==json::Type::StrNumber)return std::string("N")+x.string;double nn=x.num;if(nn==0.0)nn=0.0;uint64_t bits=0;std::memcpy(&bits,&nn,sizeof(bits));return std::string("n")+std::to_string(bits);};
                        auto set_indexed=[&](const json::Document& x)->bool{const std::string k=set_scalar_key(x);return !k.empty()&&c->scalar_keys.count(k)!=0;};
                        auto set_find=[&](const json::Document& x)->size_t{size_t j=0;for(;j<c->values.size();++j)if(structural_equal(c->values[j],x))break;return j;};
                        // Fast path: plain scalars use the O(1) index. A Number
                        // add consults the linear scan when the set holds any
                        // big integer (StrNumber), because a Number and a
                        // StrNumber can be numerically equal despite different
                        // canonical keys.
                        const bool indexed=!set_scalar_key(v).empty() && !(v.is_number()&&v.type!=json::Type::StrNumber&&c->has_huge_int);
                        const size_t i=indexed?(set_indexed(v)?set_find(v):c->values.size()):set_find(v);
                        if(method=="contains"){out=json::Document(i<c->values.size());return true;}if(!need_mut())return false;
                        if(method=="add"&&i==c->values.size()){c->values.push_back(v);const std::string k=set_scalar_key(v);if(!k.empty())c->scalar_keys.insert(k);if(v.type==json::Type::StrNumber)c->has_huge_int=true;if(c->kind==CollectionKind::SortedSet)std::stable_sort(c->values.begin(),c->values.end(),[&](const auto&a,const auto&b){int z=0;if(!scalar_order(a,b,z)){error="sorted_set: incomparable values";return false;}return z<0;});}
                        else if(method=="remove"&&i<c->values.size()){const std::string k=set_scalar_key(v);if(!k.empty())c->scalar_keys.erase(k);c->values.erase(c->values.begin()+i);}out=v;last_expression_mutation_=true;return true;}
                    }
                    if(c->kind==CollectionKind::Map||c->kind==CollectionKind::SortedMap){
                        auto mkey=[&](const json::Document& x)->std::string{if(x.is_bool())return std::string("b")+(x.boolean?"1":"0");if(x.is_string()){if(x.string.rfind("\x1fnift:",0)==0)return "";return std::string("s")+x.string;}if(x.type==json::Type::StrNumber)return std::string("N")+x.string;double nn=x.num;if(nn==0.0)nn=0.0;uint64_t bits=0;std::memcpy(&bits,&nn,sizeof(bits));return std::string("n")+std::to_string(bits);};
                        if(method=="set"){if(args.size()!=2){error="set: expected key and value";return false;}if(!need_mut())return false;json::Document k,v;if(quoted_args.size()>0&&quoted_args[0])k=json::Document(args[0]);else if(!eval(args[0],k,depth+1))return false;if(quoted_args.size()>1&&quoted_args[1])v=json::Document(args[1]);else if(!eval(args[1],v,depth+1))return false;if(!(k.is_bool()||k.is_number()||k.is_string())){error="map: key must be bool, number, or string";return false;}if(v.is_string()&&(v.string.rfind("\x1fnift:struct:",0)==0||v.string.rfind("\x1fnift:collection:",0)==0)){if(reference_would_cycle(v.string,rb->value->string)){error="set would create a cyclic reference";return false;}}
                        const std::string mk=mkey(k);const bool mind=!mk.empty()&&!(k.is_number()&&k.type!=json::Type::StrNumber&&c->has_huge_int);
                        size_t i;if(mind&&c->scalar_keys.count(mk)){for(i=0;i<c->entries.size();++i)if(structural_equal(c->entries[i].first,k))break;}else if(mind){i=c->entries.size();}else{for(i=0;i<c->entries.size();++i)if(structural_equal(c->entries[i].first,k))break;}
                        if(i<c->entries.size())c->entries[i].second=v;else{c->entries.push_back({k,v});if(mind)c->scalar_keys.insert(mk);}if(k.type==json::Type::StrNumber)c->has_huge_int=true;if(c->kind==CollectionKind::SortedMap)std::stable_sort(c->entries.begin(),c->entries.end(),[&](const auto&a,const auto&b){int z=0;if(!scalar_order(a.first,b.first,z)){error="sorted_map: incomparable keys";return false;}return z<0;});out=v;last_expression_mutation_=true;return true;}
                        if(method=="contains"||method=="get"||method=="remove"){if(args.size()!=1){error=method+": expected one key";return false;}json::Document k;if(quoted_args.size()>0&&quoted_args[0])k=json::Document(args[0]);else if(!eval(args[0],k,depth+1))return false;
                        const std::string mk=mkey(k);const bool mind=!mk.empty()&&!(k.is_number()&&k.type!=json::Type::StrNumber&&c->has_huge_int);
                        size_t i;if(mind&&c->scalar_keys.count(mk)){for(i=0;i<c->entries.size();++i)if(structural_equal(c->entries[i].first,k))break;}else{for(i=0;i<c->entries.size();++i)if(structural_equal(c->entries[i].first,k))break;}
                        if(method=="contains"){out=json::Document(i<c->entries.size());return true;}if(i==c->entries.size()){error=method+": key not found";return false;}out=c->entries[i].second;if(method=="remove"){if(!need_mut())return false;if(mind)c->scalar_keys.erase(mk);c->entries.erase(c->entries.begin()+i);last_expression_mutation_=true;}return true;}
                    }
                }
            }}
        }

        {
            const auto lp=text.find('(');
            if(lp!=std::string::npos&&text.back()==')'){std::size_t call_close=0;const std::string target=trim_copy(text.substr(0,lp));const auto dot=target.rfind('.');if(dot!=std::string::npos&&find_balanced(text,lp,'(',')',call_close)&&call_close==text.size()-1){
                const std::string root=target.substr(0,dot), method=target.substr(dot+1); VariableBinding* rb=find_binding(root);
                if(rb&&rb->value&&rb->value->is_array()){bool ok=false;std::vector<bool> quoted_args;auto args=parse_parameters(text.substr(lp+1,text.size()-lp-2),ok,&quoted_args);if(!ok){error="malformed array method arguments";return false;}auto eval_arg=[&](size_t n,json::Document& v){if(n<quoted_args.size()&&quoted_args[n]){v=json::Document(args[n]);return true;}return eval(args[n],v,depth+1);};auto& a=rb->value->array;
                    auto invoke_callback=[&](const std::string& cbexpr,const std::vector<json::Document>& av,json::Document& result)->bool{
                        json::Document cb;if(!eval(cbexpr,cb,depth+1))return false;if(!cb.is_string()||cb.string.rfind("\x1fnift:callable:",0)!=0){error="transformation: callback must be callable";return false;}
                        push_variable_scope();auto& sc=variable_scopes_.back();auto csp=std::make_shared<json::Document>(cb);sc["__nift_cb"]=VariableBinding{csp,nift_binding_type(*csp),false,false};std::string call="__nift_cb(";
                        for(size_t i=0;i<av.size();++i){if(i)call+=",";std::string n="__nift_arg"+std::to_string(i);auto sp=std::make_shared<json::Document>(av[i]);sc[n]=VariableBinding{sp,nift_binding_type(*sp),false,false};call+=n;}call+=")";bool r=eval(call,result,depth+1);pop_variable_scope();return r;};
                    if(method=="map"||method=="filter"||method=="reduce"||method=="any"||method=="all"||method=="find"||method=="find_index"||method=="count"||method=="sort_by"||method=="group_by"){
                        if((method=="reduce"&&args.size()!=2)||(method!="reduce"&&args.size()!=1)){error=method+": invalid arguments";return false;}json::Document arr=json::Document::make_array(),acc;if(method=="reduce"&&!eval(args[1],acc,depth+1))return false;double cnt=0;std::vector<std::pair<json::Document,json::Document>> keyed;
                        for(size_t vi=0;vi<a.size();++vi){const auto&v=a[vi];json::Document r;if(!invoke_callback(args[0],method=="reduce"?std::vector<json::Document>{acc,v}:std::vector<json::Document>{v},r))return false;if(method=="map")arr.array.push_back(r);else if(method=="filter"){if(!r.is_bool()){error="filter: callback must return bool";return false;}if(r.boolean)arr.array.push_back(v);}else if(method=="reduce")acc=r;else if(method=="group_by"){if(!(r.is_string()||r.is_number()||r.is_bool())){error=method+": key must be scalar";return false;}keyed.push_back({r,v});}else{if(!r.is_bool()){error=method+": callback must return bool";return false;}if(method=="any"&&r.boolean){out=json::Document(true);return true;}if(method=="all"&&!r.boolean){out=json::Document(false);return true;}if(method=="find"&&r.boolean){out=v;return true;}if(method=="find_index"&&r.boolean){out=json::Document((double)vi);return true;}if(method=="count"&&r.boolean)cnt+=1;}}
                        if(method=="sort_by"){std::stable_sort(keyed.begin(),keyed.end(),[&](const auto&x,const auto&y){if(x.first.is_number()&&y.first.is_number())return x.first.num<y.first.num;if(x.first.is_string()&&y.first.is_string())return x.first.string<y.first.string;if(x.first.is_bool()&&y.first.is_bool())return x.first.boolean<y.first.boolean;return (int)x.first.type<(int)y.first.type;});out=json::Document::make_array();for(auto&kv:keyed)out.array.push_back(kv.second);return true;}
                        if(method=="group_by"){out=json::Document::make_object();for(auto&kv:keyed){std::string k=kv.first.is_string()?kv.first.string:render_expression_value(kv.first);json::Document* bucket=nullptr;for(auto&e:out.object)if(e.first==k){bucket=&e.second;break;}if(!bucket){out[k]=json::Document::make_array();for(auto&e:out.object)if(e.first==k){bucket=&e.second;break;}}bucket->array.push_back(kv.second);}return true;}
                        if(method=="map"||method=="filter")out=arr;else if(method=="reduce")out=acc;else if(method=="any")out=json::Document(false);else if(method=="all")out=json::Document(true);else if(method=="find"||method=="find_index")out=json::Document(method=="find"?json::Document(nullptr):json::Document(-1.0));else out=json::Document(cnt);return true;}
                    if(method=="unique"||method=="flatten"||method=="sum"||method=="min"||method=="max"){
                        if(!args.empty()){error=method+": expected no arguments";return false;}
                        if(method=="unique"){out=json::Document::make_array();for(const auto&v:a){bool seen=false;for(const auto&w:out.array)if(structural_equal(v,w)){seen=true;break;}if(!seen)out.array.push_back(v);}return true;}
                        if(method=="flatten"){out=json::Document::make_array();for(const auto&v:a){if(v.is_array())out.array.insert(out.array.end(),v.array.begin(),v.array.end());else out.array.push_back(v);}return true;}
                        if(a.empty()){if(method=="sum"){out=json::Document(0.0);return true;}error=method+": cannot aggregate an empty array";return false;}
                        if(method=="sum"){double total=0;for(const auto&v:a){if(!v.is_number()){error="sum: values must be numeric";return false;}total+=v.num;}out=json::Document(total);return true;}
                        out=a.front();for(size_t i=1;i<a.size();++i){const auto&v=a[i];bool take=false;if(out.is_number()&&v.is_number())take=method=="min"?v.num<out.num:v.num>out.num;else if(out.is_string()&&v.is_string())take=method=="min"?v.string<out.string:v.string>out.string;else{error=method+": values must be comparable and homogeneous";return false;}if(take)out=v;}return true;
                    }
                    if(method=="sort"){
                        if(args.size()>1){error="sort: expected zero or one comparator";return false;}if(!rb->mutable_binding){error="cannot mutate const array: "+root;return false;}bool failed=false;std::string ferr;
                        std::stable_sort(a.begin(),a.end(),[&](const auto&x,const auto&y){if(failed)return false;if(args.empty()){if(x.is_number()&&y.is_number())return x.num<y.num;if(x.is_string()&&y.is_string())return x.string<y.string;failed=true;ferr="sort: incomparable values";return false;}json::Document r;if(!invoke_callback(args[0],{x,y},r)){failed=true;ferr=error;return false;}if(!r.is_bool()){failed=true;ferr="sort: comparator must return bool";return false;}return r.boolean;});if(failed){error=ferr;return false;}out=*rb->value;last_expression_mutation_=true;return true;}
                    if(method=="size"||method=="empty"||method=="first"||method=="last"||method=="pop"||method=="clear"||method=="push"||method=="insert"||method=="remove"||method=="indexOf"||method=="contains"||method=="join"||method=="slice"||method=="splice"||method=="reverse"){
                        if((method=="size"||method=="empty"||method=="first"||method=="last"||method=="pop"||method=="clear")&&!args.empty()){error=method+": expected no arguments";return false;}
                        if(method=="join"){if(args.size()!=1){error="join: expected separator";return false;}json::Document sep;if(!eval_arg(0,sep)||!sep.is_string()){error="join: separator must be a string";return false;}std::string joined;for(size_t j=0;j<a.size();++j){if(j)joined+=sep.string;if(a[j].is_array()||a[j].is_object()||(a[j].is_string()&&(a[j].string.rfind("\x1fnift:",0)==0))){error="join: elements must be renderable scalar values";return false;}joined+=render_expression_value(a[j]);}out=json::Document(joined);return true;}
                        if(method=="slice"){if(args.empty()||args.size()>2){error="slice: expected start and optional end";return false;}json::Document st,en;if(!eval(args[0],st,depth+1)||!st.is_number()||std::trunc(st.num)!=st.num||st.num<0){error="slice: invalid start";return false;}size_t b=(size_t)st.num,e=a.size();if(args.size()==2){if(!eval(args[1],en,depth+1)||!en.is_number()||std::trunc(en.num)!=en.num||en.num<0){error="slice: invalid end";return false;}e=(size_t)en.num;}if(b>a.size())b=a.size();if(e>a.size())e=a.size();if(e<b)e=b;out=json::Document::make_array();out.array.assign(a.begin()+b,a.begin()+e);return true;}
                        if(method=="splice"){if(args.size()<2||args.size()>3){error="splice: expected start, delete_count, optional replacement array";return false;}if(!rb->mutable_binding){error="cannot mutate const array: "+root;return false;}json::Document st,dc,repl;if(!eval(args[0],st,depth+1)||!eval(args[1],dc,depth+1)||!st.is_number()||!dc.is_number()||std::trunc(st.num)!=st.num||std::trunc(dc.num)!=dc.num||st.num<0||dc.num<0){error="splice: invalid index/count";return false;}size_t b=(size_t)st.num;if(b>a.size())b=a.size();size_t n=std::min((size_t)dc.num,a.size()-b);out=json::Document::make_array();out.array.assign(a.begin()+b,a.begin()+b+n);a.erase(a.begin()+b,a.begin()+b+n);if(args.size()==3){if(!eval(args[2],repl,depth+1)||!repl.is_array()){error="splice: replacements must be an array";return false;}a.insert(a.begin()+b,repl.array.begin(),repl.array.end());}last_expression_mutation_=true;return true;}
                        if(method=="reverse"){if(!args.empty()){error="reverse: expected no arguments";return false;}if(!rb->mutable_binding){error="cannot mutate const array: "+root;return false;}std::reverse(a.begin(),a.end());out=*rb->value;last_expression_mutation_=true;return true;}
                        if(method=="size"){out=json::Document(static_cast<double>(a.size()));return true;} if(method=="empty"){out=json::Document(a.empty());return true;}
                        if(method=="first"||method=="last"||method=="pop"){if(a.empty()){error=method+": array is empty";return false;}out=method=="first"?a.front():a.back();if(method=="pop"){if(!rb->mutable_binding){error="cannot mutate const array: "+root;return false;}a.pop_back();last_expression_mutation_=true;}return true;}
                        if(method=="clear"){if(!rb->mutable_binding){error="cannot mutate const array: "+root;return false;}a.clear();out=json::Document(nullptr);last_expression_mutation_=true;return true;}
                        if(method=="push"){if(args.size()!=1){error="push: expected one argument";return false;}if(!rb->mutable_binding){error="cannot mutate const array: "+root;return false;}json::Document v;if(quoted_args.size()>0&&quoted_args[0])v=json::Document(args[0]);else if(!eval(args[0],v,depth+1))return false;a.push_back(v);out=v;last_expression_mutation_=true;return true;}
                        if(method=="insert"){if(args.size()!=2){error="insert: expected index and value";return false;}if(!rb->mutable_binding){error="cannot mutate const array: "+root;return false;}json::Document ix,v;if(!eval(args[0],ix,depth+1)||!eval(args[1],v,depth+1)||!ix.is_number()){error="insert: invalid index";return false;}size_t i=static_cast<size_t>(ix.num);if(i>a.size()){error="insert: index out of range";return false;}a.insert(a.begin()+i,v);out=v;last_expression_mutation_=true;return true;}
                        if(method=="remove"){if(args.size()!=1){error="remove: expected index";return false;}if(!rb->mutable_binding){error="cannot mutate const array: "+root;return false;}json::Document ix;if(!eval(args[0],ix,depth+1)||!ix.is_number()){error="remove: invalid index";return false;}size_t i=static_cast<size_t>(ix.num);if(i>=a.size()){error="remove: index out of range";return false;}out=a[i];a.erase(a.begin()+i);last_expression_mutation_=true;return true;}
                        if(method=="indexOf"||method=="contains"){if(args.size()!=1){error=method+": expected one value";return false;}json::Document v;if(!eval(args[0],v,depth+1))return false;std::size_t i=0;for(;i<a.size();++i)if(structural_equal(a[i],v))break;if(method=="contains")out=json::Document(i<a.size());else out=json::Document(i<a.size()?static_cast<double>(i):-1.0);return true;}
                    }
                }
            }}
        }

        {
            const auto lp=text.find('(');
            if(lp!=std::string::npos&&text.back()==')'){std::size_t call_close=0;const std::string target=trim_copy(text.substr(0,lp));const auto dot=target.rfind('.');if(dot!=std::string::npos&&find_balanced(text,lp,'(',')',call_close)&&call_close==text.size()-1){
                const std::string root=target.substr(0,dot),method=target.substr(dot+1);VariableBinding* rb=find_binding(root);
                if(rb&&rb->value&&rb->value->is_string()&&rb->value->string.rfind("\x1fnift:stream:",0)==0){auto it=stream_instances_.find(rb->value->string.substr(13));if(it==stream_instances_.end()||it->second->closed){error="stream is closed or invalid";return false;}auto st=it->second;bool ok=false;std::vector<bool> qq;auto aa=parse_parameters(text.substr(lp+1,text.size()-lp-2),ok,&qq);if(!ok){error="malformed stream method arguments";return false;}
                    if(method=="eof"){if(!aa.empty()||st->kind!=StreamInstance::Kind::Input){error="eof: expected input stream and no arguments";return false;}out=json::Document(st->input->peek()==std::char_traits<char>::eof());return true;}
                    if(method=="read_line"){if(!aa.empty()||st->kind!=StreamInstance::Kind::Input){error="read_line: expected input stream and no arguments";return false;}std::string line;if(!std::getline(*st->input,line)){if(st->input->eof()){st->input->clear();out=json::Document(nullptr);return true;}error="read_line: input failure";return false;}out=json::Document(line);return true;}
                    if(method=="read_all"){if(!aa.empty()||st->kind!=StreamInstance::Kind::Input){error="read_all: expected input stream and no arguments";return false;}std::ostringstream ss;ss<<st->input->rdbuf();out=json::Document(ss.str());return true;}
                    if(method=="read"){if(aa.size()!=1||st->kind!=StreamInstance::Kind::Input){error="read: expected byte count on input stream";return false;}json::Document n;if(!eval(aa[0],n,depth+1)||!n.is_number()||std::trunc(n.num)!=n.num||n.num<0){error="read: invalid byte count";return false;}std::string b((size_t)n.num,'\0');st->input->read(b.data(),(std::streamsize)b.size());b.resize((size_t)st->input->gcount());out=json::Document(b);return true;}
                    if(method=="read_val"){if(!aa.empty()||st->kind!=StreamInstance::Kind::Input){error="read_val: expected input stream and no arguments";return false;}*st->input>>std::ws;if(st->input->peek()==std::char_traits<char>::eof()){out=json::Document(nullptr);return true;}std::string token;char first=(char)st->input->peek();if(first=='"'||first=='['||first=='{'){char open=first,close=first=='['?']':first=='{'?'}':'"';int dep=0;bool quoted=false,esc=false;char c;while(st->input->get(c)){token+=c;if(open=='"'){if(esc){esc=false;continue;}if(c=='\\'){esc=true;continue;}if(token.size()>1&&c=='"')break;}else{if(quoted){if(esc)esc=false;else if(c=='\\')esc=true;else if(c=='"')quoted=false;}else if(c=='"')quoted=true;else if(c==open)++dep;else if(c==close&&--dep==0)break;}}}else{while(st->input->peek()!=std::char_traits<char>::eof()&&!std::isspace((unsigned char)st->input->peek()))token+=(char)st->input->get();}json::Document v;std::string ee;if(!eval(token,v,depth+1)){error=std::string("read_val: ")+(error.empty()?("cannot parse value '"+token+"'"):error);return false;}out=v;return true;}
                    if(method=="write_val"){if(aa.size()!=1||st->kind!=StreamInstance::Kind::Output){error="write_val: expected one value on output stream";return false;}json::Document v;if(!((!qq.empty()&&qq[0])?(v=json::Document(aa[0]),true):eval(aa[0],v,depth+1)))return false;if(v.is_string()&&!qq.empty()&&qq[0]&&v.string.find("$[")!=std::string::npos){std::string r,e;if(!interpolate_parameter(v.string,r,e)){error="write_val: "+e;return false;}v=json::Document(r);}std::string serialized;if(!serialize_value(v,false,serialized,error))return false;*st->output<<serialized<<'\n';if(!*st->output){error="write_val: output failure";return false;}out=json::Document(nullptr);return true;}
                    if(method=="write"||method=="write_line"){if(aa.size()!=1||st->kind!=StreamInstance::Kind::Output){error=method+": expected one value on output stream";return false;}json::Document v;if(!((!qq.empty()&&qq[0])?(v=json::Document(aa[0]),true):eval(aa[0],v,depth+1)))return false;if(v.is_string()&&!qq.empty()&&qq[0]&&v.string.find("$[")!=std::string::npos){std::string r,e;if(!interpolate_parameter(v.string,r,e)){error=method+": "+e;return false;}v=json::Document(r);}if(v.is_array()||v.is_object()||(v.is_string()&&v.string.rfind("\x1fnift:",0)==0)){error=method+": value is not directly renderable";return false;}*st->output<<render_expression_value(v);if(method=="write_line")*st->output<<'\n';if(!*st->output){error=method+": output failure";return false;}out=json::Document(nullptr);return true;}
                    if(method=="flush"){if(!aa.empty()||st->kind!=StreamInstance::Kind::Output){error="flush: expected output stream and no arguments";return false;}st->output->flush();if(!*st->output){error="flush: output failure";return false;}out=json::Document(nullptr);return true;}
                }
            }}
            if(lp!=std::string::npos&&text.back()==')'){std::size_t call_close=0;const std::string target=trim_copy(text.substr(0,lp));const auto dot=target.rfind('.');if(dot!=std::string::npos&&find_balanced(text,lp,'(',')',call_close)&&call_close==text.size()-1){
                const std::string root=target.substr(0,dot),method=target.substr(dot+1);VariableBinding* rb=find_binding(root);
                if(rb&&rb->value&&rb->value->is_string()&&method=="substr") { bool ok=false;auto args=parse_parameters(text.substr(lp+1,text.size()-lp-2),ok);if(!ok||args.empty()||args.size()>2){error="substr: expected pos and optional length";return false;}json::Document pos,len;if(!eval(args[0],pos,depth+1)||!pos.is_number()||std::trunc(pos.num)!=pos.num||pos.num<0){error="substr: invalid pos";return false;}size_t p=(size_t)pos.num;if(p>rb->value->string.size())p=rb->value->string.size();size_t n=std::string::npos;if(args.size()==2){if(!eval(args[1],len,depth+1)||!len.is_number()||std::trunc(len.num)!=len.num||len.num<0){error="substr: invalid length";return false;}n=(size_t)len.num;}out=json::Document(rb->value->string.substr(p,n));return true;}
            }}
        }

        {
            const auto lp = text.find('(');
            std::size_t call_close = 0;
            const bool terminal_call = lp != std::string::npos && text.back() == ')' &&
                find_balanced(text, lp, '(', ')', call_close) && call_close == text.size() - 1;
            if (terminal_call && text.substr(0,lp).find('.') != std::string::npos) {
                const std::string target=trim_copy(text.substr(0,lp)); const auto dot=target.rfind('.'); const std::string root=target.substr(0,dot), mn=target.substr(dot+1);
                VariableBinding* rb=nullptr;for(auto scope=variable_scopes_.rbegin();scope!=variable_scopes_.rend();++scope){auto it=scope->find(root);if(it!=scope->end()){rb=&it->second;break;}}
                if(rb&&rb->value->is_string()&&rb->value->string.rfind("\x1fnift:struct:",0)==0){auto inst=struct_instances_.find(rb->value->string.substr(13));if(inst==struct_instances_.end()){error="invalid struct instance";return false;}auto sd=structs_.find(inst->second->type_name);if(sd==structs_.end()){error="struct type is not available in this scope: "+inst->second->type_name;return false;}auto mi=sd->second.methods.find(mn);if((mi==sd->second.methods.end()||mi->second.constructor)){
                // A callable FIELD acts as a method (module-style values such as
                // vips.resize(...)). Invoke the field's callable directly.
                auto ff=inst->second->fields.find(mn);
                if(ff!=inst->second->fields.end()&&ff->second.value&&ff->second.value->is_string()&&ff->second.value->string.rfind("\x1fnift:callable:",0)==0){
                    // Reconstruct a call that preserves quoted string arguments
                    // (parse_parameters strips quotes and unescapes; re-escape
                    // so inner quotes/backslashes/control characters survive).
                    auto reescape_parameter=[&](const std::string& raw)->std::string{std::string o;o.reserve(raw.size());for(char c:raw){if(c=='\\'){o+="\\\\";}else if(c=='"'){o+="\\\"";}else if(c=='\n'){o+="\\n";}else if(c=='\r'){o+="\\r";}else if(c=='\t'){o+="\\t";}else{o+=c;}}return o;};
                    std::string call = "("; bool ok=false; std::vector<bool> qq; auto ar=parse_parameters(text.substr(lp+1,text.size()-lp-2),ok,&qq); if(!ok){error="malformed arguments";return false;} for(size_t i=0;i<ar.size();++i){if(i)call+=","; if(i<qq.size()&&qq[i])call+="\""+reescape_parameter(ar[i])+"\""; else call+=ar[i];} call+=")";
                    json::Document cb=*ff->second.value; push_variable_scope(); auto& sc=variable_scopes_.back(); auto csp=std::make_shared<json::Document>(std::move(cb)); sc["__nift_module_field"]=VariableBinding{csp,nift_binding_type(*csp),false,false};
                    bool r=eval("__nift_module_field"+call,out,depth+1); pop_variable_scope(); return r;
                }
                error="struct has no method: "+mn;return false;}if(mi->second.private_member&&(receiver_stack_.empty()||receiver_stack_.back()!=inst->second)){error="private struct method: "+mn;return false;}bool aok=false;std::vector<bool> aq;auto ar=parse_parameters(text.substr(lp+1,text.size()-lp-2),aok,&aq);if(!aok){error="malformed method arguments";return false;}std::vector<json::Document> av;for(std::size_t ai=0;ai<ar.size();++ai){json::Document v;if(ai<aq.size()&&aq[ai])v=json::Document(ar[ai]);else if(!eval(ar[ai],v,depth+1))return false;av.push_back(std::move(v));}return invoke_struct_method(inst->second,mi->second,av,ar,out,error);}
            }
            if (terminal_call && valid_binding_identifier(trim_copy(text.substr(0, lp)))) {
                const std::string call_name = trim_copy(text.substr(0, lp));
                auto si = structs_.find(call_name);
                if (si != structs_.end()) {
                    bool args_ok=false; std::vector<bool> q; auto args=parse_parameters(text.substr(lp+1,text.size()-lp-2),args_ok,&q);
                    auto ctor=si->second.methods.find(call_name); const std::size_t expected=ctor==si->second.methods.end()?0:ctor->second.callable.params.size();
                    if(!args_ok||args.size()!=expected){error="struct constructor argument count mismatch: "+call_name;return false;}
                    auto instance=std::make_shared<StructInstance>(); instance->type_name=call_name;
                    for(const auto& field:si->second.fields){ json::Document fv; if(!eval(field.initializer,fv,depth+1))return false; auto sp=std::make_shared<json::Document>(std::move(fv)); instance->fields.emplace(field.name,VariableBinding{sp,nift_binding_type_from_text(field.initializer,*sp),true,false}); }
                    const std::string id=std::to_string(next_struct_instance_id_++); struct_instances_[id]=instance;
                    if(ctor!=si->second.methods.end()){std::vector<json::Document> av;for(std::size_t ai=0;ai<args.size();++ai){json::Document v;if(ai<q.size()&&q[ai])v=json::Document(args[ai]);else if(!eval(args[ai],v,depth+1))return false;av.push_back(std::move(v));}json::Document ignored;if(!invoke_struct_method(instance,ctor->second,av,args,ignored,error))return false;}
                    out=json::Document(std::string("\x1fnift:struct:")+id); return true;
                }
                auto ci = callables_.find(call_name);
                if(ci==callables_.end()&&!receiver_stack_.empty()){auto sd=structs_.find(receiver_stack_.back()->type_name);if(sd!=structs_.end()){auto mi=sd->second.methods.find(call_name);if(mi!=sd->second.methods.end()&&!mi->second.constructor){bool aok=false;std::vector<bool> aq;auto ar=parse_parameters(text.substr(lp+1,text.size()-lp-2),aok,&aq);if(!aok){error="malformed method arguments";return false;}std::vector<json::Document> av;for(std::size_t ai=0;ai<ar.size();++ai){json::Document v;if(ai<aq.size()&&aq[ai])v=json::Document(ar[ai]);else if(!eval(ar[ai],v,depth+1))return false;av.push_back(std::move(v));}return invoke_struct_method(receiver_stack_.back(),mi->second,av,ar,out,error);}}}
                // Indirect first-class callable invocation.
                if (ci == callables_.end()) {
                    VariableBinding* cb=find_binding(call_name);
                    if(cb&&cb->value&&cb->value->is_string()&&cb->value->string.rfind("\x1fnift:callable:",0)==0){
                        const std::string tag=cb->value->string;
                        if(tag.rfind("\x1fnift:callable:named:",0)==0){ci=callables_.find(tag.substr(21));}
                        else if(tag.rfind("\x1fnift:callable:lambda:",0)==0){
                            auto li=lambda_instances_.find(tag.substr(22));if(li==lambda_instances_.end()){error="invalid lambda";return false;}auto fn=li->second;
                            if (callable_call_depth_ >= kMaxCallableDepth) { error = "callable recursion depth exceeded"; return false; }
                            ++callable_call_depth_;
                            bool aok=false;std::vector<bool> aq;auto ar=parse_parameters(text.substr(lp+1,text.size()-lp-2),aok,&aq);if(aok){std::vector<std::string> ex;std::vector<bool> eq;for(size_t ai=0;ai<ar.size();++ai){if(!(ai<aq.size()&&aq[ai])&&trim_copy(ar[ai]).rfind("...",0)==0){json::Document sv;if(!eval(trim_copy(ar[ai]).substr(3),sv,depth+1)){--callable_call_depth_;return false;}if(!sv.is_array()){error="spread value must be an array";--callable_call_depth_;return false;}for(const auto& item:sv.array){std::string enc;if(!serialize_value(item,false,enc,error)){--callable_call_depth_;return false;}ex.push_back(enc);eq.push_back(false);}}else{ex.push_back(ar[ai]);eq.push_back(ai<aq.size()&&aq[ai]);}}ar.swap(ex);aq.swap(eq);}if(!aok||(!fn->variadic_param.empty()?ar.size()<fn->params.size():ar.size()!=fn->params.size())){error="lambda argument count mismatch";--callable_call_depth_;return false;}
                            std::vector<json::Document> av;for(size_t ai=0;ai<ar.size();++ai){json::Document v;if(ai<aq.size()&&aq[ai])v=json::Document(ar[ai]);else if(!eval(ar[ai],v,depth+1)){--callable_call_depth_;return false;}av.push_back(std::move(v));}
                            const auto saved_lambda_env=active_module_env_; if(fn->module_env) active_module_env_=fn->module_env;
                            push_variable_scope();auto& sc=variable_scopes_.back();for(const auto& kv:fn->captures)sc[kv.first]=kv.second;for(size_t ai=0;ai<fn->params.size();++ai){auto sp=std::make_shared<json::Document>(std::move(av[ai]));sc[fn->params[ai]]=VariableBinding{sp,nift_binding_type(*sp),true,false};}if(!fn->variadic_param.empty()){json::Document rest=json::Document::make_array();for(size_t ai=fn->params.size();ai<av.size();++ai)rest.array.push_back(std::move(av[ai]));auto sp=std::make_shared<json::Document>(std::move(rest));sc[fn->variadic_param]=VariableBinding{sp,nift_binding_type(*sp),true,false};}
                            bool okcall=true;
                            if(fn->block){std::string bt=trim_copy(fn->body);const bool rendered=!bt.empty()&&bt.front()=='<';if(rendered){auto nested=parse(fn->body,fn->source_path,1);if(!nested.ok){error=nested.error.message;okcall=false;}else out=json::Document(nested.output);}else{++function_call_depth_;auto nested=execute_native_program(fn->body,fn->source_path,1);--function_call_depth_;if(!nested.ok){error=nested.error.message;okcall=false;}else if(pending_control_.kind==ControlFlow::Return){out=pending_control_.value?*pending_control_.value:json::Document(nullptr);pending_control_={};}else out=json::Document(nullptr);}}
                            else { okcall=eval(fn->body,out,depth+1); if(!okcall&&error.rfind("callable recursion depth exceeded",0)!=0) error="lambda body error: "+error; }
                            pop_variable_scope();active_module_env_=saved_lambda_env;--callable_call_depth_;return okcall;
                        }
                    }
                }
                // A callable exported from an @import module may reference the module's
                // private callables and top-level bindings. Resolve through the active
                // module environment when the name is not locally defined; local
                // callables and indirect callable bindings take precedence.
                const Callable* callee = (ci != callables_.end()) ? &ci->second : nullptr;
                if (!callee && active_module_env_) { auto mit=active_module_env_->callables.find(call_name); if (mit!=active_module_env_->callables.end()) callee=&mit->second; }
                if (callee) {
                    if (callable_call_depth_ >= kMaxCallableDepth) { error = "callable recursion depth exceeded: " + call_name; return false; }
                    bool args_ok=false; std::vector<bool> quoted_args; auto args=parse_parameters(text.substr(lp+1,text.size()-lp-2),args_ok,&quoted_args); if(args_ok){std::vector<std::string> ex;std::vector<bool> eq;for(size_t ai=0;ai<args.size();++ai){if(!(ai<quoted_args.size()&&quoted_args[ai])&&trim_copy(args[ai]).rfind("...",0)==0){json::Document sv;if(!eval(trim_copy(args[ai]).substr(3),sv,depth+1))return false;if(!sv.is_array()){error="spread value must be an array";return false;}for(const auto& item:sv.array){std::string enc;if(!serialize_value(item,false,enc,error))return false;ex.push_back(enc);eq.push_back(false);}}else{ex.push_back(args[ai]);eq.push_back(ai<quoted_args.size()&&quoted_args[ai]);}}args.swap(ex);quoted_args.swap(eq);}if(!args_ok||(!callee->variadic_param.empty()?args.size()<callee->params.size():args.size()!=callee->params.size())){error="callable argument count mismatch: "+call_name;return false;}
                    std::vector<json::Document> values; for(std::size_t ai=0;ai<args.size();++ai){json::Document v;if(ai<quoted_args.size()&&quoted_args[ai])v=json::Document(args[ai]);else if(!eval(args[ai],v,depth+1))return false;values.push_back(std::move(v));}
                    ++callable_call_depth_;
                    const int caller_loop_depth = loop_depth_;
                    loop_depth_ = 0;
                    const auto saved_env = active_module_env_;
                    if (callee->module_env) active_module_env_ = callee->module_env;
                    push_variable_scope(); auto& scope=variable_scopes_.back(); if(active_module_env_){for(const auto& kv:active_module_env_->vars){if(!scope.count(kv.first))scope[kv.first]=kv.second;}} for(std::size_t ai=0;ai<callee->params.size();++ai){auto sp=std::make_shared<json::Document>(std::move(values[ai]));scope.emplace(callee->params[ai],VariableBinding{sp,nift_binding_type_from_text(args[ai],*sp),true,false});}if(!callee->variadic_param.empty()){json::Document rest=json::Document::make_array();for(std::size_t ai=callee->params.size();ai<values.size();++ai)rest.array.push_back(std::move(values[ai]));auto sp=std::make_shared<json::Document>(std::move(rest));scope.emplace(callee->variadic_param,VariableBinding{sp,nift_binding_type(*sp),true,false});}
                    const bool saved_mutation = last_expression_mutation_;
                    bool call_ok = true; std::string call_error;
                    if (callee->fragment) {
                        const bool saved_fragment = in_fragment_body_; in_fragment_body_ = true;
                        auto nested = parse(callee->body, callee->source_path, 1);
                        in_fragment_body_ = saved_fragment;
                        if (!nested.ok) { call_ok = false; call_error = nested.error.message; }
                        else { out = json::Document(nested.output); if (pending_control_.kind == ControlFlow::Return) pending_control_ = {}; }
                    } else {
                        ++function_call_depth_;
                        if (pending_control_.kind != ControlFlow::None) { pending_control_ = {}; }
                        auto body_result = execute_native_program(callee->body, callee->source_path, 1);
                        --function_call_depth_;
                        if (!body_result.ok) { call_ok = false; call_error = body_result.error.message; }
                        else if (pending_control_.kind == ControlFlow::None) { out = json::Document(nullptr); }
                        else { out = pending_control_.value ? std::move(*pending_control_.value) : json::Document(nullptr); pending_control_ = {}; }
                    }
                    last_expression_mutation_ = saved_mutation;
                    pop_variable_scope();
                    active_module_env_ = saved_env;
                    loop_depth_ = caller_loop_depth;
                    --callable_call_depth_;
                    if (!call_ok) { error = call_error; return false; }
                    return true;
                }
                if (call_name != "inject" && call_name != "validate" && call_name != "copy" && call_name != "deepcopy") { error = "undefined callable: " + call_name; return false; }
            }
        }

        if (text.rfind("copy(",0)==0 && text.back()==')') {
            json::Document source; if(!eval(text.substr(5,text.size()-6),source,depth+1))return false;
            if(source.is_string()&&source.string.rfind("\x1fnift:collection:",0)==0){auto it=collection_instances_.find(source.string.substr(17));if(it==collection_instances_.end()){error="copy: invalid collection";return false;}auto clone=std::make_shared<CollectionInstance>(*it->second);const std::string id=std::to_string(next_collection_instance_id_++);collection_instances_[id]=clone;out=json::Document(std::string("\x1fnift:collection:")+id);return true;}
            if(source.is_string()&&source.string.rfind("\x1fnift:struct:",0)==0){auto it=struct_instances_.find(source.string.substr(13));if(it==struct_instances_.end()){error="copy: invalid struct instance";return false;}auto clone=std::make_shared<StructInstance>();clone->type_name=it->second->type_name;for(const auto& f:it->second->fields){auto sp=std::make_shared<json::Document>(*f.second.value);clone->fields.emplace(f.first,VariableBinding{sp,f.second.type,f.second.mutable_binding,f.second.deep_readonly});}const std::string id=std::to_string(next_struct_instance_id_++);struct_instances_[id]=clone;out=json::Document(std::string("\x1fnift:struct:")+id);return true;}
            out=source; return true;
        }

        if (text.rfind("deepcopy(",0)==0 && text.back()==')') {
            json::Document source; if(!eval(text.substr(9,text.size()-10),source,depth+1))return false;
            std::unordered_map<std::string,std::string> seen;
            std::function<bool(const json::Document&,json::Document&)> clone;
            clone=[&](const json::Document& in,json::Document& dst)->bool{
                if(in.is_string()&&in.string.rfind("\x1fnift:collection:",0)==0){auto it=collection_instances_.find(in.string.substr(17));if(it==collection_instances_.end()){error="deepcopy: invalid collection";return false;}auto nc=std::make_shared<CollectionInstance>();nc->kind=it->second->kind;for(const auto& v:it->second->values){json::Document cv;if(!clone(v,cv))return false;nc->values.push_back(std::move(cv));if(nc->kind==CollectionKind::Set||nc->kind==CollectionKind::SortedSet){auto ck=[&](const json::Document& x)->std::string{if(x.is_bool())return std::string("b")+(x.boolean?"1":"0");if(x.is_string()){if(x.string.rfind("\x1fnift:",0)==0)return "";return std::string("s")+x.string;}if(x.type==json::Type::StrNumber)return std::string("N")+x.string;double nn=x.num;if(nn==0.0)nn=0.0;uint64_t bits=0;std::memcpy(&bits,&nn,sizeof(bits));return std::string("n")+std::to_string(bits);};std::string k=ck(cv);if(!k.empty())nc->scalar_keys.insert(k);if(cv.type==json::Type::StrNumber)nc->has_huge_int=true;}}for(const auto& e:it->second->entries){json::Document ck,cv;if(!clone(e.first,ck)||!clone(e.second,cv))return false;nc->entries.push_back({std::move(ck),std::move(cv)});if(nc->kind==CollectionKind::Map||nc->kind==CollectionKind::SortedMap){auto ck2=[&](const json::Document& x)->std::string{if(x.is_bool())return std::string("b")+(x.boolean?"1":"0");if(x.is_string()){if(x.string.rfind("\x1fnift:",0)==0)return "";return std::string("s")+x.string;}if(x.type==json::Type::StrNumber)return std::string("N")+x.string;double nn=x.num;if(nn==0.0)nn=0.0;uint64_t bits=0;std::memcpy(&bits,&nn,sizeof(bits));return std::string("n")+std::to_string(bits);};std::string kk=ck2(nc->entries.back().first);if(!kk.empty())nc->scalar_keys.insert(kk);if(nc->entries.back().first.type==json::Type::StrNumber)nc->has_huge_int=true;}}const std::string id=std::to_string(next_collection_instance_id_++);collection_instances_[id]=nc;dst=json::Document(std::string("\x1fnift:collection:")+id);return true;}
                if(in.is_string()&&in.string.rfind("\x1fnift:struct:",0)==0){const std::string old=in.string.substr(13);auto sit=seen.find(old);if(sit!=seen.end()){dst=json::Document(std::string("\x1fnift:struct:")+sit->second);return true;}auto it=struct_instances_.find(old);if(it==struct_instances_.end()){error="deepcopy: invalid struct instance";return false;}auto ni=std::make_shared<StructInstance>();ni->type_name=it->second->type_name;const std::string id=std::to_string(next_struct_instance_id_++);seen[old]=id;struct_instances_[id]=ni;for(const auto& f:it->second->fields){json::Document cv;if(!clone(*f.second.value,cv))return false;auto sp=std::make_shared<json::Document>(std::move(cv));ni->fields.emplace(f.first,VariableBinding{sp,f.second.type,f.second.mutable_binding,f.second.deep_readonly});}dst=json::Document(std::string("\x1fnift:struct:")+id);return true;}
                dst=in;if(in.is_array()){dst.array.clear();for(const auto& v:in.array){json::Document cv;if(!clone(v,cv))return false;dst.array.push_back(std::move(cv));}}else if(in.is_object()){dst.object.clear();for(const auto& e:in.object){json::Document cv;if(!clone(e.second,cv))return false;dst.object.emplace_back(e.first,std::move(cv));}}return true;
            };
            return clone(source,out);
        }

        if (text.rfind("validate(", 0) == 0 && text.back() == ')') {
            bool ok_params = false; auto args = parse_parameters(text.substr(9, text.size() - 10), ok_params);
            if (!ok_params || args.size() != 2) { error = "validate: expected schema and value"; return false; }
            json::Document schema, candidate; if (!eval(args[0], schema, depth + 1) || !eval(args[1], candidate, depth + 1)) return false;
            std::string validation_error; if (!jsonschema::validate(candidate, schema, validation_error)) { error = "validate: " + validation_error; return false; }
            out = std::move(candidate); return true;
        }

        if (text.rfind("inject(", 0) == 0 && text.back() == ')') {
            std::string arg = trim_copy(text.substr(7, text.size() - 8));
            if (arg.size() >= 2 && ((arg.front() == '"' && arg.back() == '"') || (arg.front() == '\'' && arg.back() == '\''))) arg = arg.substr(1, arg.size() - 2);
            else { json::Document av; if (!eval(arg, av, depth + 1) || !av.is_string()) { error = "inject: expected string path"; return false; } arg = av.string; }
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
            json::Document injected_value; std::string direct_json_error;
            if (nift_json::parse(*injected, injected_value, direct_json_error) && !injected_value.is_string()) {
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
                out=json::Document::make_array();
                if(inner.empty())return true;
                bool ok=false;std::vector<bool> quoted;auto elements=parse_parameters(inner,ok,&quoted);
                if(!ok){error="array literal: malformed elements";return false;}
                for(std::size_t i=0;i<elements.size();++i){json::Document v;if(i<quoted.size()&&quoted[i])v=json::Document(elements[i]);else if(!eval(elements[i],v,depth+1))return false;out.array.push_back(std::move(v));}
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
                if(nift_json::parse(text,out,json_error))return true;
                const std::string inner=trim_copy(text.substr(1,text.size()-2));
                out=json::Document::make_object();
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
                    json::Document v;
                    if(value.size()>=2&&((value.front()=='"'&&value.back()=='"')||(value.front()=='\''&&value.back()=='\''))){v=json::Document(unescape_parameter_string(value.substr(1,value.size()-2)));}
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
            auto compose_postfix = [&](const std::string& raw, json::Document& out) -> bool {
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
                json::Document base;
                if (!eval(receiver, base, depth + 1)) return false;
                if (kind == 1) {
                    if (base.is_string() && base.string.rfind("\x1fnift:page:", 0) == 0) {
                        result_.dependencies.insert(host_.relative(host_.root() / ".nift/hierarchy.fingerprint"));
                        json::Document resolved; std::string perr;
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
                            json::Document computed;
                            if (!resolve_direct(token, computed) || !computed.is_number() ||
                                computed.num < 0 || std::trunc(computed.num) != computed.num) {
                                error = "JSON array indices must be non-negative integers in '" + text + "'";
                                return false;
                            }
                            index = static_cast<std::size_t>(computed.num);
                        }
                        if (index >= base.array.size()) {
                            error = "JSON array index " + std::to_string(index) + " is out of range in '" + text + "'";
                            return false;
                        }
                        out = base.array[index];
                        return true;
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
                                json::Document computed;
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
                        json::Document cond;
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
                json::Document left;if(!eval(text.substr(0,p),left,depth+1))return false;
                if(!left.is_null()){out=std::move(left);return true;}
                return eval(text.substr(p+2),out,depth+1);
            }
            // CP207: safe access is null propagation only. Evaluate the receiver;
            // when non-null, retry the same expression with the first safe marker
            // converted to ordinary access so normal missing/type errors survive.
            {
                bool quoted=false;char quote=0;int pa=0,br=0,bc=0;std::size_t sp=std::string::npos;
                for(std::size_t z=0;z+1<text.size();++z){char c=text[z];if(quoted){if(c=='\\'&&z+1<text.size())++z;else if(c==quote)quoted=false;continue;}if(c=='\''||c=='"'){quoted=true;quote=c;continue;}if(c=='(')++pa;else if(c==')'&&pa)--pa;else if(c=='[')++br;else if(c==']'&&br)--br;else if(c=='{')++bc;else if(c=='}'&&bc)--bc;if(!pa&&!br&&!bc&&(text.compare(z,2,"?.")==0||text.compare(z,2,"?[")==0)){sp=z;break;}}
                if(sp!=std::string::npos){json::Document recv;if(!eval(text.substr(0,sp),recv,depth+1))return false;if(recv.is_null()){out=json::Document(nullptr);return true;}std::string ordinary=text;ordinary.erase(sp,1);return eval(ordinary,out,depth+1);}
            }
            if (compose_postfix(text, out)) return true;
            if (!error.empty()) return false;
            if (resolve_direct(text,out)) return true; if (!error.empty()) return false;
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

        auto numeric_binary = [&](const json::Document& left, const json::Document& right, char op, json::Document& result)->bool {
            auto as_i64=[](const json::Document& d,std::int64_t& v)->bool{
                if(d.type==json::Type::StrNumber){auto r=std::from_chars(d.string.data(),d.string.data()+d.string.size(),v);return r.ec==std::errc()&&r.ptr==d.string.data()+d.string.size();}
                if(d.is_number()&&std::trunc(d.num)==d.num&&d.num>=static_cast<double>(std::numeric_limits<std::int64_t>::min())&&d.num<9223372036854775808.0){v=static_cast<std::int64_t>(d.num);return true;}return false;};
            std::int64_t a=0,b=0; if(as_i64(left,a)&&as_i64(right,b)&&op!='/'){std::int64_t r=0;bool overflow=false;
                if(op=='+')overflow=__builtin_add_overflow(a,b,&r);else if(op=='-')overflow=__builtin_sub_overflow(a,b,&r);else if(op=='*')overflow=__builtin_mul_overflow(a,b,&r);else if(op=='%'){if(b==0){error="modulo by zero";return false;}if(a==std::numeric_limits<std::int64_t>::min()&&b==-1)r=0;else r=a%b;}else return false;
                if(overflow){error="signed 64-bit integer overflow";return false;}result=json::Document(static_cast<double>(r));if(r>9007199254740992LL||r<-9007199254740992LL){result.type=json::Type::StrNumber;result.string=std::to_string(r);}return true;}
            if(!left.is_number()||!right.is_number()){error="arithmetic operators require numeric operands";return false;}double r=0;if(op=='+')r=left.num+right.num;else if(op=='-')r=left.num-right.num;else if(op=='*')r=left.num*right.num;else if(op=='/'){if(right.num==0){error="division by zero";return false;}r=left.num/right.num;}else if(op=='%'){if(right.num==0){error="modulo by zero";return false;}if(std::trunc(left.num)!=left.num||std::trunc(right.num)!=right.num){error="modulo requires integer-valued operands";return false;}r=std::fmod(left.num,right.num);}if(!std::isfinite(r)){error="arithmetic result is not finite";return false;}result=json::Document(r);return true;
        };

        // Compound assignment is a true mutation expression. Current assignable
        // paths are identifiers and struct member paths; both are side-effect-free
        // to resolve, so the target is read once and written once. For a plain
        // identifier target, assign directly instead of rebuilding "name = value"
        // and re-parsing it through the evaluator (a full string round-trip on
        // every += in a hot loop).
        auto assign_identifier = [&](const std::string& name, json::Document&& value) -> bool {
            VariableBinding* tb = find_binding(name);
            if (!tb) { error = "assignment to undefined binding: " + name; return false; }
            if (!tb->mutable_binding) { error = "cannot assign to const binding: " + name; return false; }
            const int at = nift_binding_type(value);
            if (!nift_type_assignable(at, tb->type)) {
                error = "cannot assign " + std::string(nift_binding_type_name(at)) +
                        " to " + std::string(nift_binding_type_name(tb->type)) + " binding '" + name + "'";
                return false;
            }
            auto rebound = std::make_shared<json::Document>(std::move(value));
            tb->rebind(rebound);
            out = *rebound;
            return true;
        };
        for(const std::string cop:{"+=","-=","*=","/=","%="}){const auto p=find_top_level_op(cop);if(p!=std::string::npos){const std::string target=trim_copy(text.substr(0,p));
            {bool decl=false;bool iq=false;char iqc=0;int ip=0,ib=0;for(std::size_t k=0;k+1<target.size();++k){char cc=target[k];if(iq){if(cc=='\\')++k;else if(cc==iqc)iq=false;continue;}if(cc=='\''||cc=='"'){iq=true;iqc=cc;continue;}if(cc=='(')++ip;else if(cc==')')--ip;else if(cc=='[')++ib;else if(cc==']')--ib;if(ip||ib)continue;if(target.compare(k,2,":=")==0||target.compare(k,2,"=>")==0){decl=true;break;}}if(decl)break;}
            json::Document oldv,rhs,next;if(!eval(target,oldv,depth+1)||!eval(text.substr(p+2),rhs,depth+1))return false;if(cop=="+="&&oldv.is_string()&&rhs.is_string())next=json::Document(oldv.string+rhs.string);else if(cop=="+="&&oldv.is_array()&&rhs.is_array()){next=json::Document::make_array();next.array.reserve(oldv.array.size()+rhs.array.size());next.array.insert(next.array.end(),oldv.array.begin(),oldv.array.end());next.array.insert(next.array.end(),rhs.array.begin(),rhs.array.end());}else if(!numeric_binary(oldv,rhs,cop[0],next))return false;
            if (target.find_first_of(".([") == std::string::npos) {
                if (!assign_identifier(target, std::move(next))) return false;
            } else {
                const std::string assignment=target+" = "+next.dump(0);
                if(!eval(assignment,out,depth+1))return false;
            }
            if(depth==0)last_expression_mutation_=true;return true;}}

        const bool prefix_inc=(text.rfind("++",0)==0||text.rfind("--",0)==0);
        const bool postfix_inc=(text.size()>2&&(text.compare(text.size()-2,2,"++")==0||text.compare(text.size()-2,2,"--")==0) && find_top_level_op(":=")==std::string::npos && find_top_level_assignment()==std::string::npos);
        if(prefix_inc||postfix_inc){const bool inc=prefix_inc?text[0]=='+':text[text.size()-2]=='+';const std::string target=trim_copy(prefix_inc?text.substr(2):text.substr(0,text.size()-2));json::Document oldv,one(1.0),next;if(!eval(target,oldv,depth+1))return false;if(!oldv.is_number()){error="increment/decrement requires a numeric lvalue";return false;}if(!numeric_binary(oldv,one,inc?'+':'-',next))return false;
            if (target.find_first_of(".([") == std::string::npos) {
                if (!assign_identifier(target, std::move(next))) return false;
                if (!prefix_inc) out = oldv; // postfix yields the pre-increment value
            } else {
                json::Document assigned;if(!eval(target+" = "+next.dump(0),assigned,depth+1))return false;out=prefix_inc?assigned:oldv;
            }
            if(depth==0)last_expression_mutation_=true;return true;}

        // CP208-209: deliberately small, flat destructuring surface.
        // Arrays are exact-length; objects require named keys and ignore extras.
        // No nesting, defaults, rest, or renaming in v4.3. Both declaration and
        // assignment validate/evaluate completely before mutating bindings.
        auto destructure=[&](bool declaration,std::size_t p)->bool{
            std::string lhs=trim_copy(text.substr(0,p));
            if(lhs.size()<2||!((lhs.front()=='['&&lhs.back()==']')||(lhs.front()=='{'&&lhs.back()=='}')))return false;
            bool pok=false;auto names=parse_parameters(lhs.substr(1,lhs.size()-2),pok);if(!pok||names.empty()){error="destructuring pattern must contain identifiers";return true;}
            for(auto& n:names){n=trim_copy(n);if(!valid_binding_identifier(n)){error="destructuring patterns support only flat identifiers";return true;}}
            json::Document rhs;if(!eval(text.substr(p+(declaration?2:1)),rhs,depth+1))return true;
            std::vector<json::Document> values;
            if(lhs.front()=='['){if(!rhs.is_array()){error="array destructuring requires an array";return true;}if(rhs.array.size()!=names.size()){error="array destructuring requires exactly "+std::to_string(names.size())+" items";return true;}values=rhs.array;}
            else{if(!rhs.is_object()){error="object destructuring requires an object";return true;}for(const auto& n:names){if(!rhs.has(n)){error="object destructuring missing key: "+n;return true;}values.push_back(rhs[n]);}}
            if(variable_scopes_.empty())variable_scopes_.emplace_back();
            if(declaration){std::set<std::string> seen;for(const auto& n:names){if(!seen.insert(n).second){error="duplicate destructuring target: "+n;return true;}if(reserved_binding_name(n)||host_.is_contract_name(n)){error="destructuring name is reserved: "+n;return true;}if(variable_scopes_.back().count(n)){error="binding already declared in this scope: "+n;return true;}}
                for(std::size_t j=0;j<names.size();++j){auto sp=std::make_shared<json::Document>(values[j]);variable_scopes_.back().emplace(names[j],VariableBinding{sp,nift_binding_type_from_text(values[j].dump(0),values[j]),true,false});}}
            else{std::vector<VariableBinding*> targets;for(std::size_t j=0;j<names.size();++j){VariableBinding* b=nullptr;for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(names[j]);if(it!=sc->end()){b=&it->second;break;}}if(!b){error="assignment to undefined binding: "+names[j];return true;}if(!b->mutable_binding){error="cannot assign to const binding: "+names[j];return true;}const int at=nift_binding_type_from_text(values[j].dump(0),values[j]);if(!nift_type_assignable(at,b->type)){error="cannot change binding type in destructuring: "+names[j];return true;}targets.push_back(b);}for(std::size_t j=0;j<targets.size();++j)targets[j]->rebind(std::make_shared<json::Document>(values[j]));}
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
                // The injected script `args` binding may be shadowed by a
                // script that intentionally declares its own.
                auto& existing = variable_scopes_.back().find(name)->second;
                if (!existing.is_script_args) { error = "binding already declared in this scope: " + name; return false; }
            }
            json::Document assigned;
            if (!eval(text.substr(p + 2), assigned, depth + 1)) return false;
            std::shared_ptr<json::Document> stored;
            const std::string rhs_name=trim_copy(text.substr(p+2)); VariableBinding* alias=find_binding(rhs_name);
            if(alias&&alias->value&&alias->value->is_array()) stored=alias->value; else stored=std::make_shared<json::Document>(assigned);
            variable_scopes_.back()[name] = VariableBinding{stored, nift_binding_type_from_text(text.substr(p + 2), assigned), mutable_binding, deep_readonly};
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
                json::Document current=*rb->value;std::size_t mp=dot+1;std::shared_ptr<StructInstance> parent;std::string member;
                while(mp<name.size()){std::size_t me=name.find('.',mp);member=name.substr(mp,me==std::string::npos?std::string::npos:me-mp);auto ii=struct_instances_.find(current.string.substr(13));if(ii==struct_instances_.end()){error="invalid struct instance";return false;}parent=ii->second;auto fit=parent->fields.find(member);if(fit==parent->fields.end()){error="struct has no field: "+member;return false;}if(me==std::string::npos)break;current=*fit->second.value;if(!current.is_string()||current.string.rfind("\x1fnift:struct:",0)!=0){error="member path is not a struct: "+member;return false;}mp=me+1;}
                auto fit=parent->fields.find(member);auto sd=structs_.find(parent->type_name);bool priv=false;if(sd!=structs_.end())for(const auto& f:sd->second.fields)if(f.name==member)priv=f.private_member;if(priv&&(receiver_stack_.empty()||receiver_stack_.back()!=parent)){error="private struct field: "+member;return false;}
                json::Document assigned;if(!eval(text.substr(p+1),assigned,depth+1))return false;const int at=nift_binding_type_from_text(text.substr(p+1),assigned);if(!nift_type_assignable(at,fit->second.type)){error="cannot change struct field type: "+member;return false;}
                if(assigned.is_string()&&(assigned.string.rfind("\x1fnift:struct:",0)==0||assigned.string.rfind("\x1fnift:collection:",0)==0)){std::string parent_id;for(const auto& e:struct_instances_)if(e.second==parent){parent_id=e.first;break;}if(reference_would_cycle(assigned.string,std::string("\x1fnift:struct:")+parent_id)){error="assignment would create a cyclic reference: "+member;return false;}}
                fit->second.value=std::make_shared<json::Document>(std::move(assigned));out=*fit->second.value;if(depth==0)last_expression_mutation_=true;return true;
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
                json::Document assigned;
                if (!eval(text.substr(p + 1), assigned, depth + 1)) return false;
                json::Document* node = rb->value.get();
                for (std::size_t k = 0; k + 1 < indexes.size(); ++k) {
                    json::Document idx;
                    if (!eval(indexes[k], idx, depth + 1)) return false;
                    if (idx.is_number() && node->is_array()) {
                        if (idx.num < 0) { error = "negative index in assignment: " + indexes[k]; return false; }
                        const std::size_t i = static_cast<std::size_t>(idx.num);
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
                json::Document last;
                if (!eval(indexes.back(), last, depth + 1)) return false;
                if (last.is_number() && node->is_array()) {
                    if (last.num < 0) { error = "negative index in assignment: " + indexes.back(); return false; }
                    const std::size_t i = static_cast<std::size_t>(last.num);
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
                    json::Document assigned;
                    if (!eval(text.substr(p + 1), assigned, depth + 1)) return false;
                    const int assigned_type = nift_binding_type_from_text(text.substr(p + 1), assigned);
                    if (!nift_type_assignable(assigned_type, rec->second.type)) {
                        error = "cannot assign " + std::string(nift_binding_type_name(assigned_type)) +
                                " to " + nift_binding_type_name(rec->second.type) + " struct field '" + name + "'";
                        return false;
                    }
                    if (assigned.is_string()&&(assigned.string.rfind("\x1fnift:struct:",0)==0||assigned.string.rfind("\x1fnift:collection:",0)==0)){std::string recv_id;for(const auto& e:struct_instances_)if(e.second==receiver_stack_.back()){recv_id=e.first;break;}if(reference_would_cycle(assigned.string,std::string("\x1fnift:struct:")+recv_id)){error="assignment would create a cyclic reference: "+name;return false;}}
                    auto rebound = std::make_shared<json::Document>(std::move(assigned));
                    rec->second.value = rebound;
                    if (depth == 0) last_expression_mutation_ = true;
                    out = *rebound;
                    return true;
                }
            }
            if (!binding) { error = "assignment to undefined binding: " + name; return false; }
            if (!binding->mutable_binding) { error = "cannot assign to const binding: " + name; return false; }
            json::Document assigned;
            if (!eval(text.substr(p + 1), assigned, depth + 1)) return false;
            const int assigned_type = nift_binding_type_from_text(text.substr(p + 1), assigned);
            if (!nift_type_assignable(assigned_type, binding->type)) {
                error = "cannot assign " + std::string(nift_binding_type_name(assigned_type)) +
                        " to " + nift_binding_type_name(binding->type) + " binding '" + name + "'";
                return false;
            }
            auto rebound = std::make_shared<json::Document>(std::move(assigned));
            binding->rebind(rebound);
            if (depth == 0) last_expression_mutation_ = true;
            out = *rebound;
            return true;
        }

        if (const auto p=find_top_level_op("||"); p!=std::string::npos) {
            json::Document left;
            if (!eval(text.substr(0,p),left,depth+1)) return false;
            if (truthy_value(left)) { out=json::Document(true); return true; }
            json::Document right; if (!eval(text.substr(p+2),right,depth+1)) return false;
            out=json::Document(truthy_value(right)); return true;
        }
        if (const auto p=find_top_level_op("&&"); p!=std::string::npos) {
            json::Document left;
            if (!eval(text.substr(0,p),left,depth+1)) return false;
            if (!truthy_value(left)) { out=json::Document(false); return true; }
            json::Document right; if (!eval(text.substr(p+2),right,depth+1)) return false;
            out=json::Document(truthy_value(right)); return true;
        }
        for (const std::string op : {"==","!=","<=",">=","<",">"}) {
            const auto p=find_top_level_op(op);
            if (p==std::string::npos) continue;
            json::Document left,right;
            if (!eval(text.substr(0,p),left,depth+1) || !eval(text.substr(p+op.size()),right,depth+1)) return false;
            bool result=false;
            if (op=="==" || op=="!=") {
                bool equal=false;
                equal = structural_equal(left,right);
                result = op=="==" ? equal : !equal;
            } else {
                if((left.is_string()&&left.string.rfind("\x1fnift:enum:",0)==0)||(right.is_string()&&right.string.rfind("\x1fnift:enum:",0)==0)){error="enum ordering is not defined; compare explicit to_int() values if intended";return false;}
                if ((!left.is_number() || !right.is_number()) && !(left.is_string() && right.is_string())) { error="ordering comparisons require two numbers or two strings"; return false; }
                int ordering=0;
                if (left.is_number() && right.is_number()) {
                    ordering=numeric_compare(left,right);
                } else ordering=left.string<right.string?-1:(left.string>right.string?1:0);
                if (op=="<") result=ordering<0; else if (op=="<=") result=ordering<=0; else if (op==">") result=ordering>0; else result=ordering>=0;
            }
            out=json::Document(result); return true;
        }
        if (text.front()=='!' && (text.size()<2 || text[1]!='=')) {
            json::Document operand; if (!eval(text.substr(1),operand,depth+1)) return false;
            out=json::Document(!truthy_value(operand)); return true;
        }

        { std::size_t cp=std::string::npos;bool q=false;char qc=0;int pa=0,br=0,bc=0;for(size_t z=text.size();z-- >0;){char c=text[z];if(q){if(c==qc&&(z==0||text[z-1]!='\\'))q=false;continue;}if(c=='\''||c=='"'){q=true;qc=c;continue;}if(c==')')++pa;else if(c=='(')--pa;else if(c==']')++br;else if(c=='[')--br;else if(c=='}')++bc;else if(c=='{')--bc;else if(c=='+'&&!pa&&!br&&!bc){if(z>0&&std::string("+-*/%(<>=!&|?:,").find(text[z-1])!=std::string::npos)continue;cp=z;break;}}if(cp!=std::string::npos){json::Document l,r;if(!eval(text.substr(0,cp),l,depth+1)||!eval(text.substr(cp+1),r,depth+1))return false;if(l.is_array()&&r.is_array()){out=json::Document::make_array();out.array.reserve(l.array.size()+r.array.size());out.array.insert(out.array.end(),l.array.begin(),l.array.end());out.array.insert(out.array.end(),r.array.begin(),r.array.end());return true;}if(l.is_string()||r.is_string()){auto safe=[&](const json::Document& v,std::string& z){if(v.is_array()||v.is_object()||(v.is_string()&&v.string.rfind("\x1fnift:",0)==0))return false;z=render_expression_value(v);return true;};std::string a,b;if(!safe(l,a)||!safe(r,b)){error="string concatenation requires renderable scalar values";return false;}out=json::Document(a+b);return true;}return numeric_binary(l,r,'+',out);}}

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
                if ((c=='+' || c=='-') && (i==0 || std::string("+-*/%(<>=!&|?:,").find(text[i-1])!=std::string::npos)) continue;
                return i;
            }
            return std::string::npos;
        };

        std::size_t pos=find_binary("+-");
        if (pos==std::string::npos) pos=find_binary("*/%");
        if (pos!=std::string::npos) {
            json::Document left,right;
            if (!eval(text.substr(0,pos),left,depth+1) || !eval(text.substr(pos+1),right,depth+1)) return false;
            const char op=text[pos];
            if(op=='+' && left.is_array() && right.is_array()){out=json::Document::make_array();out.array.reserve(left.array.size()+right.array.size());out.array.insert(out.array.end(),left.array.begin(),left.array.end());out.array.insert(out.array.end(),right.array.begin(),right.array.end());return true;}
            if(op=='+' && (left.is_string()||right.is_string())) { auto safe=[&](const json::Document& v,std::string& r){if(v.is_array()||v.is_object()||(v.is_string()&&v.string.rfind("\x1fnift:",0)==0))return false;r=render_expression_value(v);return true;};std::string l,r;if(!safe(left,l)||!safe(right,r)){error="string concatenation requires renderable scalar values";return false;}out=json::Document(l+r);return true; }
            if (!left.is_number() || !right.is_number()) { error="arithmetic operators require numeric operands"; return false; }
            return numeric_binary(left,right,op,out);
        }

        if ((text.front()=='+' || text.front()=='-') && text.size()>1) {
            json::Document operand;
            if (!eval(text.substr(1),operand,depth+1)) return false;
            if (!operand.is_number()) { error="unary arithmetic operators require a numeric operand"; return false; }
            out=json::Document(text.front()=='-' ? -operand.num : operand.num); return true;
        }

        error="unknown value or malformed expression: " + text;
        return false;
    };
    return eval(expression,value,0);
}

bool Parser::evaluate_condition(const std::string& expression, bool& value, std::string& error) {
    auto resolve_operand = [&](const std::string& operand,
                               std::shared_ptr<const json::Document>& document) -> bool {
        json::Document evaluated;
        if (!evaluate_expression(operand, evaluated, error)) return false;
        document = std::make_shared<const json::Document>(std::move(evaluated));
        return true;
    };

    auto truthy = [](const json::Document& document) {
        if (document.is_bool()) return document.boolean;
        if (document.is_null()) return false;
        if (document.is_number()) return document.num != 0.0;
        if (document.is_string()) return !document.string.empty();
        if (document.is_array()) return !document.array.empty();
        if (document.is_object()) return !document.object.empty();
        return false;
    };

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
            std::shared_ptr<const json::Document> left, right;
            if (!resolve_operand(condition.substr(0, op_position), left) ||
                !resolve_operand(condition.substr(op_position + op.size()), right)) return false;

            if (op == "==" || op == "!=") {
                bool equal = false;
                if (left->type == right->type) {
                    if (left->is_null()) equal = true;
                    else if (left->is_bool()) equal = left->boolean == right->boolean;
                    else if (left->is_number()) equal = left->num == right->num;
                    else if (left->is_string()) equal = left->string == right->string;
                    else { error = "@if comparisons are only supported for scalar JSON values"; return false; }
                }
                result = op == "==" ? equal : !equal;
                return true;
            }

            if (left->type != right->type || (!left->is_number() && !left->is_string())) {
                error = "@if ordering comparisons require two numbers or two strings of the same type";
                return false;
            }
            int ordering = 0;
            if (left->is_number()) ordering = left->num < right->num ? -1 : (left->num > right->num ? 1 : 0);
            else ordering = left->string < right->string ? -1 : (left->string > right->string ? 1 : 0);
            if (op == "<") result = ordering < 0;
            else if (op == "<=") result = ordering <= 0;
            else if (op == ">") result = ordering > 0;
            else result = ordering >= 0;
            return true;
        }

        std::shared_ptr<const json::Document> document;
        if (!resolve_operand(condition, document)) return false;
        result = truthy(*document);
        return true;
    };

    return eval(expression, value);
}

std::string Parser::path_to(const std::string& argument, const std::string& directive) {
    if (!host_.has_output_context()) {
        result_.ok = false;
        result_.error = {tracked_info_.name, {}, 0, "@" + directive + " requires a path context; set the current output on the render context to resolve '" + argument + "'"};
        return {};
    }
    const fs::path output = host_.output_path(tracked_info_);
    const bool absolute_404 = (tracked_info_.name == "404");
    const fs::path base = output.parent_path();
    fs::path destination;
    bool index_page = false;

    if (const auto target = host_.tracked_output_path(argument)) {
        destination = target->path;
        index_page = target->index_page;
    } else {
        destination = (host_.root() / argument).lexically_normal();
        if (!filesystem::path_within(host_.root(), destination)) {
            result_.ok = false;
            result_.error = {tracked_info_.name, {}, 0, directive + ": path must stay inside the Nift project: " + argument};
            return {};
        }
        if (!host_.source_exists(destination)) {
            result_.ok = false;
            result_.error = {tracked_info_.name, {}, 0, "'" + argument + "' is neither a tracked name nor a file that exists"};
            return {};
        }
    }

    // A deployed 404 document is served at any request depth, so a relative
    // path from its on-disk location is meaningless: the browser might think it
    // is at /docs/foo/bar while the file physically lives at /404.html. Emit a
    // root-absolute web path instead. All existence/dependency checking above
    // is unchanged; only the path representation differs. Targets outside the
    // output directory keep the ordinary relative behaviour.
    if (absolute_404) {
        const fs::path web_root = (host_.root() / host_.output_dir()).lexically_normal();
        fs::path web_rel = destination.lexically_normal().lexically_relative(web_root.lexically_normal());
        const std::string web_rel_str = web_rel.generic_string();
        const bool escapes_web_root = web_rel_str == ".." || web_rel_str.rfind("../", 0) == 0;
        if (!escapes_web_root) {
            if (index_page) {
                const std::string dir = web_rel.parent_path().generic_string();
                if (dir.empty() || dir == ".") return "/";
                std::string p = "/" + dir;
                if (p.back() != '/') p += '/';
                return p;
            }
            return "/" + web_rel_str;
        }
    }

    fs::path relative_path = destination.lexically_normal().lexically_relative(base.lexically_normal());
    std::string relative = relative_path.empty() ? destination.generic_string() : relative_path.generic_string();

    if (index_page && destination.filename().generic_string().rfind("index", 0) == 0) {
        relative_path = destination.parent_path().lexically_normal().lexically_relative(base.lexically_normal());
        relative = relative_path.empty() ? destination.parent_path().generic_string() : relative_path.generic_string();
        if (relative.empty() || relative == ".") return "./";
        if (relative.back() != '/') relative += '/';
        return relative;
    }
    if (relative.find('/') == std::string::npos && relative.rfind("..", 0) != 0) relative = "./" + relative;
    return relative;
}

bool Parser::translate_function_program(const std::string& source, std::string& translated, std::string& error) const {
    std::function<bool(const std::string&, std::string&)> convert;
    convert = [&](const std::string& in, std::string& out) -> bool {
        std::size_t i=0;
        auto boundary=[&](std::size_t p,const std::string& kw){return in.compare(p,kw.size(),kw)==0 && (p+kw.size()==in.size() || !std::isalnum((unsigned char)in[p+kw.size()]) && in[p+kw.size()]!='_');};
        while(i<in.size()) {
            while(i<in.size() && std::isspace((unsigned char)in[i])) ++i;
            if(i>=in.size()) break;
            if(boundary(i,"fn")) {
                std::size_t p=i+2; while(p<in.size()&&std::isspace((unsigned char)in[p]))++p; std::size_t pc=0;
                if(p>=in.size()||in[p]!='('||!find_balanced(in,p,'(',')',pc)){error="fn requires '(name(args))'";return false;}
                std::size_t bo=pc+1; while(bo<in.size()&&std::isspace((unsigned char)in[bo]))++bo; std::size_t bc=0;
                if(bo>=in.size()||in[bo]!='{'||!find_balanced(in,bo,'{','}',bc)){error="fn requires a block";return false;}
                out += "@fn("+in.substr(p+1,pc-p-1)+"){"+in.substr(bo+1,bc-bo-1)+"}"; i=bc+1; continue;
            }
            if(boundary(i,"enum")) {
                std::size_t p=i+4;while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;std::size_t ns=p;while(p<in.size()&&(std::isalnum((unsigned char)in[p])||in[p]=='_'))++p;std::string name=in.substr(ns,p-ns);while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;std::size_t bc=0;
                if(name.empty()||p>=in.size()||in[p]!='{'||!find_balanced(in,p,'{','}',bc)){error="enum requires 'Name { members }'";return false;}out+="@enum("+name+"){"+in.substr(p+1,bc-p-1)+"}";i=bc+1;continue;
            }
            if(boundary(i,"struct")) {
                std::size_t p=i+6; while(p<in.size()&&std::isspace((unsigned char)in[p]))++p; std::size_t pc=0;
                if(p>=in.size()||in[p]!='('||!find_balanced(in,p,'(',')',pc)){error="struct requires '(name)'";return false;}
                std::size_t bo=pc+1; while(bo<in.size()&&std::isspace((unsigned char)in[bo]))++bo; std::size_t bc=0;
                if(bo>=in.size()||in[bo]!='{'||!find_balanced(in,bo,'{','}',bc)){error="struct requires a block";return false;}
                out += "@struct("+in.substr(p+1,pc-p-1)+"){"+in.substr(bo+1,bc-bo-1)+"}"; i=bc+1; continue;
            }
            if(boundary(i,"export")) {
                std::size_t p=i+6; while(p<in.size()&&std::isspace((unsigned char)in[p]))++p; std::size_t pc=0;
                if(p>=in.size()||in[p]!='('||!find_balanced(in,p,'(',')',pc)){error="export requires '(binding)'";return false;}
                out += "@__export("+in.substr(p+1,pc-p-1)+")"; i=pc+1; if(i<in.size()&&in[i]==';')++i; continue;
            }
            // A single-line @// comment must be consumed to end-of-line BEFORE
            // statement splitting, so a ';' inside the comment cannot turn the
            // rest of the comment into a statement.
            if (in.compare(i, 3, "@//") == 0) {
                std::size_t line_end = in.find('\n', i);
                out += '\n';
                i = (line_end == std::string::npos) ? in.size() : line_end;
                continue;
            }
            // Script-land single-line comment without the template '@' prefix.
            if (in.compare(i, 2, "//") == 0) {
                std::size_t line_end = in.find('\n', i);
                out += '\n';
                i = (line_end == std::string::npos) ? in.size() : line_end;
                continue;
            }
            if (in.compare(i, 3, "@/*") == 0) {
                std::size_t block_end = in.find("*/", i + 3);
                if (block_end == std::string::npos) { error = "open comment '@/*' has no close '*/'"; return false; }
                i = block_end + 2;
                continue;
            }
            // Script-land block comment without the template '@' prefix.
            if (in.compare(i, 2, "/*") == 0) {
                std::size_t block_end = in.find("*/", i + 2);
                if (block_end == std::string::npos) { error = "open comment '/*' has no close '*/'"; return false; }
                out += '\n';
                i = block_end + 2;
                continue;
            }
            if(boundary(i,"while")) {
                std::size_t p=i+5;while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;std::size_t pc=0;
                if(p>=in.size()||in[p]!='('||!find_balanced(in,p,'(',')',pc)){error="function while requires '(...)'";return false;}
                std::size_t bo=pc+1;while(bo<in.size()&&std::isspace((unsigned char)in[bo]))++bo;std::size_t bc=0;
                if(bo>=in.size()||in[bo]!='{'||!find_balanced(in,bo,'{','}',bc)){error="function while requires a block";return false;}
                std::string body;if(!convert(in.substr(bo+1,bc-bo-1),body))return false;out+="@while("+in.substr(p+1,pc-p-1)+"){"+body+"}";i=bc+1;continue;
            }
            if(boundary(i,"for")) {
                std::size_t p=i+3; while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;
                std::size_t pc=0; if(p>=in.size()||in[p]!='('||!find_balanced(in,p,'(',')',pc)){error="function for requires '(...)'";return false;}
                std::size_t bo=pc+1;while(bo<in.size()&&std::isspace((unsigned char)in[bo]))++bo;std::size_t bc=0;
                if(bo>=in.size()||in[bo]!='{'||!find_balanced(in,bo,'{','}',bc)){error="function for requires a block";return false;}
                std::string body;if(!convert(in.substr(bo+1,bc-bo-1),body))return false;
                out += "@for("+in.substr(p+1,pc-p-1)+"){"+body+"}";i=bc+1;continue;
            }
            if(boundary(i,"if")) {
                std::size_t p=i+2; while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;
                if(p>=in.size()||in[p]!='('){error="function if requires '(...)'";return false;}
                std::size_t pc=0; if(!find_balanced(in,p,'(',')',pc)){error="function if has no matching ')'";return false;}
                std::size_t bo=pc+1; while(bo<in.size()&&std::isspace((unsigned char)in[bo]))++bo;
                std::size_t bc=0; if(bo>=in.size()||in[bo]!='{'||!find_balanced(in,bo,'{','}',bc)){error="function if requires a block";return false;}
                std::string body; if(!convert(in.substr(bo+1,bc-bo-1),body))return false;
                out += "@if("+in.substr(p+1,pc-p-1)+"){"+body+"}"; i=bc+1;
                while(true){std::size_t e=i;while(e<in.size()&&std::isspace((unsigned char)in[e]))++e;if(!boundary(e,"else"))break;e+=4;while(e<in.size()&&std::isspace((unsigned char)in[e]))++e;
                    if(boundary(e,"if")){std::size_t q=e+2;while(q<in.size()&&std::isspace((unsigned char)in[q]))++q;std::size_t qc=0;if(q>=in.size()||in[q]!='('||!find_balanced(in,q,'(',')',qc)){error="function else if malformed";return false;}std::size_t eb=qc+1;while(eb<in.size()&&std::isspace((unsigned char)in[eb]))++eb;std::size_t ec=0;if(eb>=in.size()||in[eb]!='{'||!find_balanced(in,eb,'{','}',ec)){error="function else if requires block";return false;}std::string body2;if(!convert(in.substr(eb+1,ec-eb-1),body2))return false;out+=" else if("+in.substr(q+1,qc-q-1)+"){"+body2+"}";i=ec+1;continue;}
                    std::size_t ec=0;if(e>=in.size()||in[e]!='{'||!find_balanced(in,e,'{','}',ec)){error="function else requires block";return false;}std::string body2;if(!convert(in.substr(e+1,ec-e-1),body2))return false;out+=" else {"+body2+"}";i=ec+1;break;}
                continue;
            }
            if(boundary(i,"return")) {
                std::size_t e=i+6; bool quoted=false;char quote=0;int par=0,br=0;
                while(e<in.size()&&in[e]!='\n'&&in[e]!=';' ) { char c=in[e]; if(quoted){if(c=='\\'&&e+1<in.size())++e;else if(c==quote)quoted=false;}else if(c=='\''||c=='"'){quoted=true;quote=c;}else if(c=='(')++par;else if(c==')')--par;else if(c=='[')++br;else if(c==']')--br;++e; }
                std::string expr=trim_copy(in.substr(i+6,e-(i+6))); out += expr.empty()?"@__bare_return()":"@return("+expr+")"; i=e<in.size()?e+1:e; continue;
            }
            if(boundary(i,"break")) { std::size_t e=i+5; while(e<in.size()&&std::isspace((unsigned char)in[e])&&in[e]!='\n')++e; if(e==in.size()||in[e]==';'||in[e]=='\n') { out += "break"; i=e<in.size()?e+1:e; continue; } }
            if(boundary(i,"continue")) { std::size_t e=i+8; while(e<in.size()&&std::isspace((unsigned char)in[e])&&in[e]!='\n')++e; if(e==in.size()||in[e]==';'||in[e]=='\n') { out += "continue"; i=e<in.size()?e+1:e; continue; } }
            std::size_t start=i; bool quoted=false;char quote=0;int par=0,br=0,bc=0;
            for(;i<in.size();++i){char c=in[i];if(quoted){if(c=='\\'&&i+1<in.size())++i;else if(c==quote)quoted=false;continue;}if(c=='\''||c=='"'){quoted=true;quote=c;continue;}if(c=='(')++par;else if(c==')')--par;else if(c=='[')++br;else if(c==']')--br;else if(c=='{')++bc;else if(c=='}')--bc;if(!par&&!br&&!bc&&(c==';'||c=='\n'))break;}
            std::string stmt=trim_copy(in.substr(start,i-start));
            if(!stmt.empty()){
                // Template-grammar statements (legacy @-directives and $[...]
                // values) are already parseable directly and would otherwise be
                // double-wrapped as $[@...]/$[$...] and routed through the full
                // expression evaluator. Pass them through verbatim; only bare
                // v4.2 function-program statements need the $[...] wrapper.
                if(stmt[0]=='@'||stmt[0]=='$'){
                    out+=stmt;
                    // A verbatim single-line comment has no terminating newline
                    // once inter-statement whitespace is stripped; emit one so it
                    // cannot swallow the following statement.
                    if(stmt.rfind("@//",0)==0) out+='\n';
                } else {
                    // Executable-path command style: a statement whose first
                    // token is a filesystem path (./x, ../x, /x, dir/x) runs the
                    // executable as an ordinary external process (executable .f
                    // scripts, .sh, .py, binaries). This is process execution,
                    // never in-process Nift evaluation.
                    std::size_t f0=stmt.find_first_of(" \t");
                    const std::string first=f0==std::string::npos?stmt:stmt.substr(0,f0);
                    const bool path_first = (first.rfind("./",0)==0||first.rfind("../",0)==0||(!first.empty()&&first[0]=='/')||first.find('/')!=std::string::npos);
                    // Command-style also covers a plain word + arguments
                    // (git status, gh repo view, printf one): a multi-token
                    // line without Nift syntax cannot be a single expression,
                    // so it is ordinary external process execution. A single
                    // bare token stays an expression (bindings have precedence).
                    const bool multi_token = f0 != std::string::npos;
                    // Reject Nift declaration/assignment forms: x := 5, x = 5,
                    // x=5, ./x = 5. A '=' inside a later command argument
                    // (printf a=b, git log --format=%H) stays a command.
                    bool assignment_form = first.find('=')!=std::string::npos;
                    if(!assignment_form && f0!=std::string::npos){
                        const std::string rest=trim_copy(stmt.substr(f0));
                        assignment_form = rest.rfind(":=",0)==0 || (rest.rfind("=",0)==0 && rest.size()>1 && rest[1]!='=');
                    }
                    // Only a plain command line (no unquoted parens/brackets/
                    // braces/assignment) is external command style. This keeps
                    // ./tool(args) call attempts and `./x = ...` out of the
                    // process path so the shell's callable-first routing still
                    // works.
                    bool cmd_like=(path_first||multi_token)&&!assignment_form; bool qq=false;char qc=0;int pp=0,bb=0,cc2=0;bool saw_delim=false;
                    for(std::size_t k=0;k<stmt.size()&&cmd_like;++k){char c=stmt[k];if(qq){if(c=='\\')++k;else if(c==qc)qq=false;continue;}if(c=='\''||c=='"'){qq=true;qc=c;continue;}if(c=='('){++pp;saw_delim=true;}else if(c==')'){--pp;saw_delim=true;}else if(c=='['){++bb;saw_delim=true;}else if(c==']'){--bb;saw_delim=true;}else if(c=='{'){++cc2;saw_delim=true;}else if(c=='}'){--cc2;saw_delim=true;}else if((c=='='||c==':')&&pp==0&&bb==0&&cc2==0){cmd_like=false;}}
                    if(cmd_like&&!saw_delim&&pp==0&&bb==0&&cc2==0) out += "@__nift_cmd(" + stmt + ")";
                    else out+="$["+stmt+"]";
                }
            }
            if(i<in.size())++i;
        }
        return true;
    };
    translated.clear(); return convert(source,translated);
}

Parser::StatementState Parser::statement_state(const std::string& raw) const {
    const std::string source = trim_copy(raw);
    if (source.empty()) return StatementState::Complete;
    // Structural completeness: a quoted string and every (parenthesis, bracket
    // or brace) must be closed by the end of the submitted text. This mirrors
    // the delimiter conventions used by the function-program translator
    // (quote-aware, with no comment grammar in statement land). An unmatched
    // closing delimiter is malformed rather than merely incomplete.
    bool quoted = false; char qc = 0; int par = 0, br = 0, bc = 0;
    for (std::size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        if (quoted) { if (c == '\\' && i + 1 < source.size()) ++i; else if (c == qc) quoted = false; continue; }
        if (c == '\'' || c == '"') { quoted = true; qc = c; continue; }
        if (c == '(') ++par; else if (c == ')') --par;
        else if (c == '[') ++br; else if (c == ']') --br;
        else if (c == '{') ++bc; else if (c == '}') --bc;
        if (par < 0 || br < 0 || bc < 0) return StatementState::Invalid;
    }
    if (quoted || par != 0 || br != 0 || bc != 0) return StatementState::Incomplete;
    // Balanced input must still translate into a valid statement program;
    // otherwise it is invalid (a complete but malformed form).
    std::string program, error;
    if (!translate_function_program(source, program, error)) return StatementState::Invalid;
    return StatementState::Complete;
}

std::vector<std::string> Parser::shell_completions(const std::string& prefix) const {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto& kv : callables_) if (kv.first.rfind(prefix, 0) == 0 && seen.insert(kv.first).second) out.push_back(kv.first);
    for (const auto& kv : structs_) if (kv.first.rfind(prefix, 0) == 0 && seen.insert(kv.first).second) out.push_back(kv.first);
    for (auto scope = variable_scopes_.rbegin(); scope != variable_scopes_.rend(); ++scope)
        for (const auto& kv : *scope) if (kv.first.rfind(prefix, 0) == 0 && seen.insert(kv.first).second) out.push_back(kv.first);
    return out;
}

RenderResult Parser::run_script(const std::string& source, const fs::path& source_path) {
    result_ = RenderResult{};
    variable_scopes_.clear(); variable_scopes_.emplace_back();
    // Standalone scripts receive their user arguments as an immutable `args`
    // array (the script path itself is not included). The binding is always
    // present (empty when no arguments were passed) so scripts can read `args`
    // unconditionally; a script that declares its own `args` shadows it.
    {
        auto arr = std::make_shared<json::Document>(json::Document::make_array());
        for (const auto& a : script_args_) arr->array.emplace_back(a);
        VariableBinding vb{arr, nift_binding_type(*arr), false, true};
        vb.is_script_args = true;
        variable_scopes_.back()["args"] = std::move(vb);
    }
    callables_.clear(); structs_.clear(); requested_exports_.clear(); pending_control_={};
    in_import_program_=false; standalone_script_host_=true; strict_script_mode_=true; function_call_depth_=1;
    auto rr=execute_native_program(source,source_path,0);
    function_call_depth_=0; strict_script_mode_=false;
    rr.output.clear();
    if(rr.ok && pending_control_.kind==ControlFlow::Return && pending_control_.value) {
        const auto& v=*pending_control_.value;
        if(v.is_array()||v.is_object()||(v.is_string()&&v.string.rfind("\x1fnift:",0)==0)) { rr.ok=false; rr.error.message="script return value is not directly renderable"; }
        else rr.output=render_expression_value(v);
    }
    pending_control_={};
    std::string resource_error;
    if(!finalize_script_resources(resource_error) && rr.ok){rr.ok=false;rr.error.message=resource_error;}
    return rr;
}

RenderResult Parser::run_statement(const std::string& source, const fs::path& source_path) {
    if(variable_scopes_.empty()) variable_scopes_.emplace_back();
    standalone_script_host_=true; strict_script_mode_=true; result_ = RenderResult{}; pending_control_={}; function_call_depth_=1;
    const std::string t=trim_copy(source);
    // A REPL is also an inspector: a single expression is evaluated directly so
    // arrays/collections/structs can be safely displayed without routing through
    // template rendering (which intentionally rejects compound values).
    const bool declaration = t.rfind("fn(",0)==0 || t.rfind("struct(",0)==0 || t.rfind("if(",0)==0 ||
        t.rfind("for(",0)==0 || t.rfind("while(",0)==0 || t.rfind("return",0)==0 || t.rfind("export(",0)==0;
    bool assignment=false; bool quoted=false; char qc=0; int par=0,br=0,bc=0;
    for(std::size_t i=0;i<t.size();++i){char c=t[i];if(quoted){if(c=='\\')++i;else if(c==qc)quoted=false;continue;}if(c=='\''||c=='"'){quoted=true;qc=c;continue;}if(c=='(')++par;else if(c==')')--par;else if(c=='[')++br;else if(c==']')--br;else if(c=='{')++bc;else if(c=='}')--bc;else if(!par&&!br&&!bc&&c=='='){char a=i?t[i-1]:0,b=i+1<t.size()?t[i+1]:0;if(a!='='&&a!='!'&&a!='<'&&a!='>'&&b!='='){assignment=true;break;}}}
    if(!declaration&&!assignment&&!t.empty()){
        json::Document v; std::string error;
        if(evaluate_expression(t,v,error)){
            RenderResult rr; std::string shown;
            // REPL commands commonly return null (for example cd()) and an empty
            // string has nothing useful to inspect. Keep both silent rather than
            // displaying `null` or an empty quoted string.
            if(v.is_null() || (v.is_string() && v.string.empty())) {
                function_call_depth_=0; strict_script_mode_=false; return rr;
            }
            // Presentation modifiers are composable and order-independent. The evaluator
            // has already serialized their original value into an ANSI-free string; here
            // the REPL only decides whether that representation should be highlighted.
            std::string presentation; bool presentation_method=false, highlight=false;
            { bool p=false,h=false; if(strip_presentation_chain(t,presentation,p,h)){presentation_method=true;highlight=h;} }
            if(presentation_method&&v.is_string()) shown=v.string;
            else if(!serialize_value(v,false,shown,error)){rr.ok=false;rr.error.message=error;function_call_depth_=0;strict_script_mode_=false;return rr;}
            if(highlight && console::stdout_colour_enabled()) shown=console::highlight_nift_value(shown);
            rr.output=shown; function_call_depth_=0; strict_script_mode_=false; return rr;
        }
        // If direct evaluation fails, let the native statement path provide the
        // canonical diagnostic; it may be a valid statement form not recognized above.
    }
    auto rr=execute_native_program(source,source_path,0); function_call_depth_=0; strict_script_mode_=false; pending_control_={}; return rr;
}

void Parser::reset_script_control() { pending_control_={}; result_=RenderResult{}; }

RenderResult Parser::execute_native_program(const std::string& source, const fs::path& source_path, int depth) {
    std::string program, error;
    if (!translate_function_program(source, program, error)) {
        RenderResult failed; failed.ok=false; failed.error.message=error; return failed;
    }
    return parse(program, source_path, depth);
}

bool Parser::execute_import_file(const std::string& argument, const fs::path& caller_path, int depth, std::string& error) {
    fs::path path=argument;
    const bool package_name = argument.find('/') == std::string::npos && argument.find('\\') == std::string::npos && fs::path(argument).extension().empty();
    if (package_name && standalone_script_host_) {
        fs::path root = fs::current_path()/".nift"/"packages"/argument;
        json::Document manifest; std::string me;
        if (load_json_file(root/"manifest.json", manifest, me) && manifest.is_object() && manifest.has("entry") && manifest["entry"].is_string()) path=root/manifest["entry"].string;
        else { error="package is not installed or has invalid manifest: "+argument; return false; }
    } else if(path.is_relative()) {
        if(standalone_script_host_ && caller_path == fs::path("<nift-sh>")) path=fs::current_path()/path;
        fs::path local=caller_path.parent_path()/path;
        if(!caller_path.parent_path().empty() && host_.source_exists(local)) path=local;
        else path=host_.root()/path;
    }
    path=fs::absolute(path).lexically_normal();
    if(!standalone_script_host_ && !host_.root().empty() && !filesystem::path_within(fs::absolute(host_.root()).lexically_normal(),path)){error="path must stay inside the Nift project";return false;}
    if(std::find(input_stack_.begin(),input_stack_.end(),path)!=input_stack_.end()){error="script import cycle through "+path.generic_string();return false;}
    auto src=host_.read_shared_source(path); if(src.status==nift::HostStatus::Error||!src.content){error=src.error.empty()?"script is not readable":src.error;return false;}

    auto saved_scopes=std::move(variable_scopes_); auto saved_callables=std::move(callables_); auto saved_structs=std::move(structs_);
    auto saved_exports=std::move(requested_exports_); const bool saved_import=in_import_program_; auto saved_control=pending_control_; const bool saved_strict=strict_script_mode_;
    variable_scopes_.clear(); variable_scopes_.emplace_back(); callables_.clear(); structs_.clear(); requested_exports_.clear(); pending_control_={}; in_import_program_=true; strict_script_mode_=true;
    std::unordered_set<std::string> pre_import_files; for(const auto& kv:file_instances_)pre_import_files.insert(kv.first);
    input_stack_.push_back(path); result_.dependencies.insert(host_.relative(path)); ++function_call_depth_;
    auto rr=execute_native_program(*src.content,path,depth+1);
    --function_call_depth_; input_stack_.pop_back();
    if(rr.ok){for(auto& kv:file_instances_)if(!pre_import_files.count(kv.first)&&kv.second->open){auto f=kv.second;f->open=false;f->dirty=false;f->mode.clear();f->working.clear();f->saved.clear();f->cursor=0;rr.ok=false;rr.error.message="managed file left open at @import completion: "+f->path.generic_string();break;}}
    auto isolated_scope=std::move(variable_scopes_.back()); auto isolated_callables=std::move(callables_); auto isolated_structs=std::move(structs_); auto exports=requested_exports_; auto completion=pending_control_;
    variable_scopes_=std::move(saved_scopes); callables_=std::move(saved_callables); structs_=std::move(saved_structs); requested_exports_=std::move(saved_exports); in_import_program_=saved_import; pending_control_=saved_control; strict_script_mode_=saved_strict;
    if(!rr.ok){error=rr.error.message;return false;}
    if(completion.kind==ControlFlow::Return && completion.value){error="return with a value is not allowed in @import";return false;}

    std::unordered_map<std::string,VariableBinding> vars;
    std::unordered_map<std::string,Callable> funcs;
    std::unordered_map<std::string,StructDefinition> types;
    for(const auto& name:exports){
        bool collision=false; for(auto it=variable_scopes_.rbegin();it!=variable_scopes_.rend();++it) if(it->count(name)){collision=true;break;}
        if(collision||callables_.count(name)||structs_.count(name)){error="export collides with existing binding: "+name;return false;}
        auto vi=isolated_scope.find(name); if(vi!=isolated_scope.end()){
            // Exporting a struct instance must also make its type resolvable in
            // the importer so module-style values such as vips.resize(...) work.
            if(vi->second.value && vi->second.value->is_string() && vi->second.value->string.rfind("\x1fnift:struct:",0)==0){
                auto ii=struct_instances_.find(vi->second.value->string.substr(13));
                if(ii!=struct_instances_.end()){
                    auto sit=isolated_structs.find(ii->second->type_name);
                    if(sit!=isolated_structs.end() && !types.count(ii->second->type_name)) types.emplace(ii->second->type_name, sit->second);
                }
            }
            vars.emplace(name,vi->second);continue;
        }
        auto fi=isolated_callables.find(name); if(fi!=isolated_callables.end()){funcs.emplace(name,fi->second);continue;}
        auto si=isolated_structs.find(name); if(si!=isolated_structs.end()){types.emplace(name,si->second);continue;}
        error="export names no existing binding: "+name;return false;
    }
    // Exported callables and struct methods may reference the module's private
    // callables and top-level bindings. Attach a shared module environment so
    // those references resolve while the module stays isolated from the importer.
    if (!funcs.empty() || !types.empty()) {
        auto module_env = std::make_shared<ModuleEnv>();
        module_env->callables = isolated_callables;
        module_env->vars = isolated_scope;
        for (auto& kv : funcs) kv.second.module_env = module_env;
        for (auto& kv : types) for (auto& m : kv.second.methods) m.second.callable.module_env = module_env;
    }
    // Install only after the complete export set has validated.
    auto& dst=variable_scopes_.back(); for(auto& kv:vars)dst.emplace(kv.first,std::move(kv.second));
    for(auto& kv:funcs) callables_.emplace(kv.first,std::move(kv.second));
    for(auto& kv:types) structs_.emplace(kv.first,std::move(kv.second));
    return true;
}

RenderResult Parser::parse(const std::string& source, const fs::path& source_path, int depth) {
    if (depth > 64) {
        fail(source_path, source, 0, "maximum template parse depth exceeded (possible recursion)");
        return result_;
    }

    const int base_code_block_depth = code_block_depth_;
    std::size_t open_code_offset = 0;
    std::string output;
    output.reserve(source.size() + 64);

    for (std::size_t i = 0; i < source.size() && result_.ok;) {
        if (pending_control_.kind != ControlFlow::None) break;
        if (in_fragment_body_ && source.compare(i, 6, "return") == 0 &&
            (i + 6 == source.size() || source[i + 6] == ';' || source[i + 6] == '\n' || source[i + 6] == '\r' || std::isspace(static_cast<unsigned char>(source[i + 6])))) {
            std::size_t e = i + 6;
            while (e < source.size() && (source[e] == ' ' || source[e] == '\t')) ++e;
            if (e < source.size() && source[e] != ';' && source[e] != '\n' && source[e] != '\r') {
                fail(source_path, source, i, "fragment return cannot have a value"); break;
            }
            pending_control_.kind = ControlFlow::Return; pending_control_.value.reset(); break;
        }
        if (source.compare(i, 5, "break") == 0 &&
            (i + 5 == source.size() || source[i + 5] == ';' || source[i + 5] == '\n' || source[i + 5] == '\r')) {
            if (loop_depth_ <= 0) { fail(source_path, source, i, "break is only valid inside a loop"); break; }
            pending_control_.kind = ControlFlow::Break;
            break;
        }
        if (source.compare(i, 8, "continue") == 0 &&
            (i + 8 == source.size() || source[i + 8] == ';' || source[i + 8] == '\n' || source[i + 8] == '\r')) {
            if (loop_depth_ <= 0) { fail(source_path, source, i, "continue is only valid inside a loop"); break; }
            pending_control_.kind = ControlFlow::Continue;
            break;
        }
        if (i + 1 < source.size() && source[i] == '\\' && (source[i + 1] == '@' || source[i + 1] == '$' || source[i + 1] == '#')) {
            output += source[i + 1];
            i += 2;
            continue;
        }

        if (source.compare(i, 3, "@/*") == 0) {
            const auto end = source.find("*/", i + 3);
            if (end == std::string::npos) { fail(source_path, source, i, "open comment '@/*' has no close '*/'"); break; }
            i = end + 2;
            continue;
        }
        if (source.compare(i, 3, "@//") == 0) {
            const auto end = source.find('\n', i);
            i = end == std::string::npos ? source.size() : end;
            continue;
        }


        // Match stripped Nift's <pre*> handling. While inside a pre block,
        // literal '<' characters are escaped except for <code>/</code> tags and
        // the outer </pre>. Nift expressions still parse normally, so \@ / \$
        // remain the way to show Nift syntax literally in code examples.
        if (source.compare(i, 4, "<!--") == 0) {
            ++html_comment_depth_;
        }
        if (source.compare(i, 3, "-->") == 0 && html_comment_depth_ > 0) {
            --html_comment_depth_;
        }
        if (source[i] == '<' && html_comment_depth_ == 0) {
            const bool closes_pre = source.compare(i + 1, 4, "/pre") == 0 &&
                i + 5 < source.size() && source[i + 5] == '>';
            const bool opens_pre = source.compare(i + 1, 3, "pre") == 0 &&
                i + 4 < source.size() &&
                (source[i + 4] == '>' || source[i + 4] == ' ' || source[i + 4] == '\t' ||
                 source[i + 4] == '\r' || source[i + 4] == '\n');

            if (closes_pre) {
                --code_block_depth_;
                if (code_block_depth_ < base_code_block_depth) {
                    fail(source_path, source, i, "</pre> close tag has no preceding <pre*> open tag");
                    code_block_depth_ = base_code_block_depth;
                    break;
                }
            }

            const bool code_tag = source.compare(i + 1, 4, "code") == 0 ||
                                  source.compare(i + 1, 5, "/code") == 0;
            if (code_block_depth_ > 0 && !code_tag) output += "&lt;";
            else output.push_back('<');

            if (opens_pre) {
                if (code_block_depth_ == base_code_block_depth) open_code_offset = i;
                ++code_block_depth_;
            }
            ++i;
            continue;
        }

        if (source.compare(i, 7, "@script") == 0 && (i+7==source.size() || std::isspace(static_cast<unsigned char>(source[i+7])) || source[i+7]=='{')) {
            std::size_t bo=i+7; while(bo<source.size()&&std::isspace(static_cast<unsigned char>(source[bo])))++bo; std::size_t bc=0;
            if(bo>=source.size()||source[bo]!='{'||!find_balanced(source,bo,'{','}',bc)){fail(source_path,source,i,"@script requires a balanced block");break;}
            if(pending_control_.kind!=ControlFlow::None) pending_control_={};
            std::unordered_set<std::string> pre_script_files; for(const auto& kv:file_instances_)pre_script_files.insert(kv.first);
            ++function_call_depth_;
            const bool saved_strict=strict_script_mode_; strict_script_mode_=true;
            auto nested=execute_native_program(source.substr(bo+1,bc-bo-1),source_path,depth+1);
            strict_script_mode_=saved_strict;
            --function_call_depth_;
            if(nested.ok){for(auto& kv:file_instances_)if(!pre_script_files.count(kv.first)&&kv.second->open){auto f=kv.second;f->open=false;f->dirty=false;f->mode.clear();f->working.clear();f->saved.clear();f->cursor=0;nested.ok=false;nested.error.message="managed file left open at @script completion: "+f->path.generic_string();break;}}
            if(!nested.ok){result_=nested;break;}
            if(pending_control_.kind==ControlFlow::Return){
                if(pending_control_.value) { const auto& rv=*pending_control_.value;if(rv.is_array()||rv.is_object()||(rv.is_string()&&rv.string.rfind("\x1fnift:",0)==0)){pending_control_={};fail(source_path,source,i,"@script return value is not directly renderable");break;}output += render_expression_value(rv); }
                pending_control_={};
            }
            i=bc+1; continue;
        }

        if (source.compare(i, 16, "@__bare_return()") == 0) {
            if(function_call_depth_<=0){fail(source_path,source,i,"return is only valid in script/function execution");break;}
            pending_control_.kind=ControlFlow::Return; pending_control_.value.reset(); i+=16; break;
        }

        if (source.compare(i, 12, "@__nift_cmd(") == 0) {
            // Executable-path command style from script land (e.g. ./generate.f
            // foo bar): ordinary OS process execution. Never in-process Nift
            // evaluation. Output is streamed; a non-zero exit status does not
            // abort the script (matching the REPL shell), but a process that
            // cannot be launched is a hard error.
            std::size_t cc=0; if(!find_balanced(source,i+11,'(',')',cc)){fail(source_path,source,i,"@__nift_cmd has no matching ')'");break;}
            const std::string raw=source.substr(i+12,cc-(i+12));
            if(std::getenv("NIFT_NO_PROCESS")){fail(source_path,source,i,"external process execution disabled");break;}
            // Quote-aware tokenization of the command line.
            std::vector<std::string> toks; std::string cur; bool sq=false,dq=false,esc=false;
            for(std::size_t t=0;t<raw.size();++t){char c=raw[t];if(esc){cur+=c;esc=false;continue;}if(c=='\\'&&!sq){esc=true;continue;}if(c=='\''&&!dq){sq=!sq;continue;}if(c=='"'&&!sq){dq=!dq;continue;}if((c==' '||c=='\t'||c=='\r'||c=='\n')&&!sq&&!dq){if(!cur.empty()){toks.push_back(cur);cur.clear();}continue;}cur+=c;}if(!cur.empty())toks.push_back(cur);
            if(toks.empty()){fail(source_path,source,i,"empty command");break;}
            ProcessSpec spec; spec.program=toks.front(); spec.args.assign(toks.begin()+1,toks.end());
            auto pr=nift_run_process(spec,true,true);
            if(!pr.launched){fail(source_path,source,i,pr.error.empty()?("cannot launch command: "+spec.program):pr.error);break;}
            i=cc+1; continue;
        }

        if (source.compare(i, 10, "@__export(") == 0) {
            std::size_t ec=0; if(!find_balanced(source,i+9,'(',')',ec)){fail(source_path,source,i,"export has no matching ')'");break;}
            if(!in_import_program_ && !standalone_script_host_){fail(source_path,source,i,"export() is only valid in an imported/external script");break;}
            const std::string name=trim_copy(source.substr(i+10,ec-(i+10)));
            if(!valid_binding_identifier(name)){fail(source_path,source,i,"export requires a binding name");break;}
            if(std::find(requested_exports_.begin(),requested_exports_.end(),name)!=requested_exports_.end()){fail(source_path,source,i,"duplicate export: "+name);break;}
            bool export_found=false;
            for(auto scope=variable_scopes_.rbegin();scope!=variable_scopes_.rend()&&!export_found;++scope) if(scope->count(name))export_found=true;
            if(callables_.count(name)||structs_.count(name))export_found=true;
            if(!export_found){fail(source_path,source,i,"export names no existing binding: "+name);break;}
            requested_exports_.push_back(name); i=ec+1; continue;
        }

        if (source.compare(i, 8, "@import(") == 0) {
            std::size_t ec=0; if(!find_balanced(source,i+7,'(',')',ec)){fail(source_path,source,i,"@import has no matching ')'");break;}
            std::string arg=trim_copy(source.substr(i+8,ec-(i+8))); json::Document pv; std::string pe;
            if(!evaluate_expression(arg,pv,pe)||!pv.is_string()){fail(source_path,source,i,"@import path must be a string expression");break;}
            if(!execute_import_file(pv.string,source_path,depth+1,pe)){fail(source_path,source,i,"@import: "+pe);break;}
            i=ec+1; continue;
        }

        if(source.compare(i,6,"@enum(")==0){
            std::size_t hc=0;if(!find_balanced(source,i+5,'(',')',hc)){fail(source_path,source,i,"enum definition has no matching ')'");break;}const std::string name=trim_copy(source.substr(i+6,hc-(i+6)));if(!valid_binding_identifier(name)){fail(source_path,source,i,"invalid enum name");break;}std::size_t bo=hc+1;while(bo<source.size()&&std::isspace((unsigned char)source[bo]))++bo;std::size_t bc=0;if(bo>=source.size()||source[bo]!='{'||!find_balanced(source,bo,'{','}',bc)){fail(source_path,source,i,"enum definition requires a block");break;}
            if(variable_scopes_.empty())variable_scopes_.emplace_back();if(variable_scopes_.back().count(name)){fail(source_path,source,i,"binding already declared in this scope: "+name);break;}
            std::string body=source.substr(bo+1,bc-bo-1);for(char& c:body)if(c=='\n'||c==';')c=',';bool ok=false;auto items=parse_parameters(body,ok);if(!ok||items.empty()){fail(source_path,source,i,"enum requires at least one member");break;}json::Document ns=json::Document::make_object();std::set<std::int64_t> used;std::int64_t next=0;bool good=true;
            for(auto raw:items){raw=trim_copy(raw);if(raw.empty())continue;auto eq=raw.find('=');std::string member=trim_copy(eq==std::string::npos?raw:raw.substr(0,eq));if(!valid_binding_identifier(member)||ns.has(member)){fail(source_path,source,i,"invalid or duplicate enum member: "+member);good=false;break;}std::int64_t val=next;if(eq!=std::string::npos){std::string vs=trim_copy(raw.substr(eq+1));auto pr=std::from_chars(vs.data(),vs.data()+vs.size(),val);if(pr.ec!=std::errc()||pr.ptr!=vs.data()+vs.size()){fail(source_path,source,i,"enum values must be signed integer literals");good=false;break;}}if(!used.insert(val).second){fail(source_path,source,i,"duplicate enum integer value: "+std::to_string(val));good=false;break;}ns[member]=json::Document(std::string("\x1fnift:enum:")+name+":"+member+":"+std::to_string(val));if(val==std::numeric_limits<std::int64_t>::max()){next=val;}else next=val+1;}
            if(ns.object.empty()){fail(source_path,source,i,"enum requires at least one member");good=false;}
            if(!good)break;auto sp=std::make_shared<json::Document>(std::move(ns));variable_scopes_.back().emplace(name,VariableBinding{sp,nift_binding_type_from_text("{}",*sp),false,true});i=bc+1;continue;
        }

        if (source.compare(i, 8, "@struct(") == 0) {
            std::size_t hc = 0;
            if (!find_balanced(source, i + 7, '(', ')', hc)) { fail(source_path, source, i, "struct definition has no matching ')'"); break; }
            const std::string struct_name = trim_copy(source.substr(i + 8, hc - (i + 8)));
            if (!valid_binding_identifier(struct_name)) { fail(source_path, source, i, "invalid struct name"); break; }
            std::size_t bo = hc + 1; while (bo < source.size() && std::isspace(static_cast<unsigned char>(source[bo]))) ++bo;
            std::size_t bc = 0;
            if (bo >= source.size() || source[bo] != '{' || !find_balanced(source, bo, '{', '}', bc)) { fail(source_path, source, i, "struct definition requires a block"); break; }
            StructDefinition def; def.name = struct_name;
            const std::string body = source.substr(bo + 1, bc - bo - 1);
            std::size_t p = 0; bool struct_ok = true;
            while (p < body.size()) {
                while (p < body.size() && std::isspace(static_cast<unsigned char>(body[p]))) ++p;
                if (p >= body.size()) break;
                bool priv = false;
                if (body.compare(p, 8, "private ") == 0) { priv = true; p += 8; while (p < body.size() && std::isspace(static_cast<unsigned char>(body[p]))) ++p; }
                if (body.compare(p, 3, "fn(") == 0) {
                    std::size_t mhc = 0; if (!find_balanced(body, p + 2, '(', ')', mhc)) { fail(source_path, source, i, "struct method has malformed signature"); struct_ok=false; break; }
                    const std::string sig = trim_copy(body.substr(p + 3, mhc - (p + 3))); const auto lp=sig.find('(');
                    if(lp==std::string::npos||sig.back()!=')'){fail(source_path,source,i,"struct method signature must be name(args)");struct_ok=false;break;}
                    const std::string mn=trim_copy(sig.substr(0,lp)); bool pok=false; auto ps=parse_parameters(sig.substr(lp+1,sig.size()-lp-2),pok);
                    std::size_t mbo=mhc+1; while(mbo<body.size()&&std::isspace(static_cast<unsigned char>(body[mbo])))++mbo; std::size_t mbc=0;
                    if(!valid_binding_identifier(mn)||!pok||mbo>=body.size()||body[mbo]!='{'||!find_balanced(body,mbo,'{','}',mbc)){fail(source_path,source,i,"invalid struct method");struct_ok=false;break;}
                    const bool ctor=mn==struct_name; if(ctor && def.methods.find(struct_name)!=def.methods.end()){fail(source_path,source,i,"struct may define at most one constructor");struct_ok=false;break;}
                    const auto mb=normalize_control_block_body(body.substr(mbo+1,mbc-mbo-1));
                    def.methods[mn]=StructMethod{Callable{ps,"",mb.text,source_path,false},priv,ctor}; p=mbc+1; continue;
                }
                // Struct fields are separated by top-level newlines or
                // semicolons; a ';' or newline inside a nested lambda/block must
                // not split the field. Scan with paren/bracket/brace/quote depth.
                std::size_t eol=p; { int par=0,br=0,bc=0; bool q=false; char qc=0; for(;eol<body.size();++eol){char ch=body[eol];if(q){if(ch=='\\'){++eol;continue;}if(ch==qc)q=false;continue;}if(ch=='\''||ch=='"'){q=true;qc=ch;continue;}if(ch=='(')++par;else if(ch==')')--par;else if(ch=='[')++br;else if(ch==']')--br;else if(ch=='{')++bc;else if(ch=='}')--bc;else if(!par&&!br&&!bc&&(ch=='\n'||ch==';'))break;} }
                std::string line=trim_copy(body.substr(p,eol-p)); const auto dp=line.find(":=");
                if(dp==std::string::npos){fail(source_path,source,i,"struct fields require 'name := initializer'");struct_ok=false;break;}
                std::string fn=trim_copy(line.substr(0,dp)), init=trim_copy(line.substr(dp+2));
                if(!valid_binding_identifier(fn)||init.empty()){fail(source_path,source,i,"invalid struct field declaration");struct_ok=false;break;}
                for(const auto& f:def.fields)if(f.name==fn){fail(source_path,source,i,"duplicate struct field: "+fn);struct_ok=false;break;}
                if(!struct_ok) break;
                def.fields.push_back(StructField{fn,init,priv}); p=eol<body.size()?eol+1:eol;
            }
            if(!struct_ok) break;
            structs_[struct_name]=std::move(def); i=bc+1; continue;
        }

        if (source.compare(i, 4, "@fn(") == 0 || source.compare(i, 10, "@fragment(") == 0) {
            const bool fragment = source.compare(i, 10, "@fragment(") == 0; const std::size_t open = i + (fragment ? 9 : 3);
            std::size_t header_close = 0; if (!find_balanced(source, open, '(', ')', header_close)) { fail(source_path, source, i, "callable definition has no matching ')'"); break; }
            const std::string signature = trim_copy(source.substr(open + 1, header_close - open - 1)); const auto lp = signature.find('(');
            if (lp == std::string::npos || signature.back() != ')') { fail(source_path, source, i, "callable signature must be name(args)"); break; }
            const std::string name = trim_copy(signature.substr(0, lp)); bool params_ok=false; auto params=parse_parameters(signature.substr(lp+1, signature.size()-lp-2), params_ok);
            std::string variadic_param;
            if(params_ok){
                for(std::size_t pi=0;pi<params.size();++pi){
                    std::string p=trim_copy(params[pi]);
                    if(p.rfind("...",0)==0){
                        if(!variadic_param.empty()||pi+1!=params.size()||!valid_binding_identifier(trim_copy(p.substr(3)))){params_ok=false;break;}
                        variadic_param=trim_copy(p.substr(3)); params.pop_back(); break;
                    }
                    if(!valid_binding_identifier(p)){params_ok=false;break;} params[pi]=p;
                }
            }
            if (!valid_binding_identifier(name) || !params_ok) { fail(source_path, source, i, "invalid callable signature"); break; }
            std::size_t bo=header_close+1; while(bo<source.size()&&std::isspace(static_cast<unsigned char>(source[bo])))++bo; std::size_t bc=0;
            if(bo>=source.size()||source[bo]!='{'||!find_balanced(source,bo,'{','}',bc)){fail(source_path,source,i,"callable definition requires a block");break;}
            const auto def_body = normalize_control_block_body(source.substr(bo+1,bc-bo-1));
            callables_[name]=Callable{params,variadic_param,def_body.text,source_path,fragment}; i=bc+1; continue;
        }

        if (source.compare(i, 4, "@:=(") == 0) {
            std::size_t header_close = 0;
            if (!find_balanced(source, i + 3, '(', ')', header_close)) { fail(source_path, source, i, "@:= has no matching ')'"); break; }
            const std::string name = trim_copy(source.substr(i + 4, header_close - (i + 4)));
            std::size_t block_open = header_close + 1; while (block_open < source.size() && std::isspace(static_cast<unsigned char>(source[block_open]))) ++block_open;
            std::size_t block_close = 0;
            if (block_open >= source.size() || source[block_open] != '{' || !find_balanced(source, block_open, '{', '}', block_close)) { fail(source_path, source, i, "@:= requires a balanced '{...}' expression block"); break; }
            json::Document declared; std::string declaration_error;
            if (!evaluate_expression(name + " := " + source.substr(block_open + 1, block_close - block_open - 1), declared, declaration_error)) { fail(source_path, source, i, "@:= " + declaration_error); break; }
            i = block_close + 1; continue;
        }

        if (source.compare(i, 2, "$[") == 0) {
            std::size_t end = i + 2;
            std::size_t nested_brackets = 0;
            bool value_quoted = false;
            char value_quote = 0;
            for (; end < source.size(); ++end) {
                const char c = source[end];
                if (value_quoted) {
                    if (c == '\\' && end + 1 < source.size()) ++end;
                    else if (c == value_quote) value_quoted = false;
                    continue;
                }
                if (c == '\'' || c == '"') { value_quoted = true; value_quote = c; continue; }
                if (c == '[') {
                    ++nested_brackets;
                } else if (c == ']') {
                    if (nested_brackets == 0) break;
                    --nested_brackets;
                }
            }

            if (end < source.size()) {
                const std::string key = source.substr(i + 2, end - i - 2);

                auto split_ternary = [&](const std::string& expression,
                                         std::string& condition,
                                         std::string& when_true,
                                         std::string& when_false,
                                         bool& malformed) -> bool {
                    malformed = false;
                    bool quoted = false; char quote = 0;
                    int parens = 0, brackets = 0, braces = 0;
                    std::size_t question = std::string::npos;
                    int nested_ternary = 0;
                    for (std::size_t pos = 0; pos < expression.size(); ++pos) {
                        const char c = expression[pos];
                        if (quoted) {
                            if (c == '\\' && pos + 1 < expression.size()) ++pos;
                            else if (c == quote) quoted = false;
                            continue;
                        }
                        if (c == '\'' || c == '"') { quoted = true; quote = c; continue; }
                        if (c == '(') { ++parens; continue; }
                        if (c == ')') { if (parens) --parens; continue; }
                        if (c == '[') { ++brackets; continue; }
                        if (c == ']') { if (brackets) --brackets; continue; }
                        if (c == '{') { ++braces; continue; }
                        if (c == '}') { if (braces) --braces; continue; }
                        if (parens || brackets || braces) continue;
                        if (c == '?') {
                            // '??', '?.' and '?[' are optionality markers, not
                            // ternary; they fall through to evaluate_expression.
                            if (pos + 1 < expression.size() && (expression[pos + 1] == '?' || expression[pos + 1] == '.' || expression[pos + 1] == '[')) continue;
                            if (pos > 0 && expression[pos - 1] == '?') continue;
                            if (question == std::string::npos) question = pos;
                            else ++nested_ternary;
                            continue;
                        }
                        if (c == ':' && question != std::string::npos) {
                            if (nested_ternary > 0) { --nested_ternary; continue; }
                            condition = trim_copy(expression.substr(0, question));
                            when_true = expression.substr(question + 1, pos - question - 1);
                            when_false = expression.substr(pos + 1);
                            return true;
                        }
                    }
                    if (question != std::string::npos) {
                        condition = trim_copy(expression.substr(0, question));
                        when_true = expression.substr(question + 1);
                        when_false.clear();
                        malformed = condition.empty();
                        return !malformed;
                    }
                    return false;
                };

                std::string ternary_condition, when_true, when_false;
                bool malformed_ternary = false;
                if (split_ternary(key, ternary_condition, when_true, when_false, malformed_ternary)) {
                    bool condition_value = false;
                    std::string condition_error;
                    if (!evaluate_condition(ternary_condition, condition_value, condition_error)) {
                        fail(source_path, source, i, condition_error);
                        break;
                    }
                    const std::string& selected = condition_value ? when_true : when_false;

                    // Ternary branches are normally parsed lazily as Nift source so a
                    // selected branch can contain directives such as @input(...). A
                    // branch that is itself a quoted scalar literal is different: the
                    // quotes delimit the value and must not become rendered output.
                    // This keeps constructs such as class="x$[active ? ' active' : '']"
                    // consistent with the scalar-value semantics used elsewhere in
                    // $[...] while preserving lazy source parsing for non-literal
                    // branches.
                    json::Document selected_literal;
                    std::string selected_literal_error;
                    if (scalar_literal(trim_copy(selected), selected_literal, selected_literal_error) &&
                        selected_literal.is_string()) {
                        output += selected_literal.string;
                    } else {
                        const auto nested = parse(selected, source_path, depth + 1);
                        if (!nested.ok) break;
                        output += nested.output;
                    }
                    i = end + 1;
                    continue;
                }
                if (malformed_ternary) {
                    fail(source_path, source, i, "ternary expression has '?' without a matching ':'");
                    break;
                }

                json::Document expression_value;
                std::string expression_error;
                if (evaluate_expression(trim_copy(key), expression_value, expression_error)) {
                    if (last_expression_mutation_) { i = end + 1; continue; }
                    if (expression_value.is_array()) {
                        // Script land: a bare call statement that returns a
                        // non-renderable collection is fire-and-forget.
                        const std::string ck=trim_copy(key);
                        if (standalone_script_host_ && !ck.empty() && ck.back()==')') { i = end + 1; continue; }
                        fail(source_path, source, i, "cannot render JSON array $[" + key + "]; select an element first");
                        break;
                    }
                    if (expression_value.is_object()) {
                        const std::string ck=trim_copy(key);
                        if (standalone_script_host_ && !ck.empty() && ck.back()==')') { i = end + 1; continue; }
                        fail(source_path, source, i, "cannot render JSON object $[" + key + "]; select a member first");
                        break;
                    }
                    if (function_call_depth_ == 0 &&
                        expression_value.is_string() &&
                        (expression_value.string.rfind("\x1fnift:struct:", 0) == 0 ||
                         expression_value.string.rfind("\x1fnift:callable:", 0) == 0 ||
                         expression_value.string.rfind("\x1fnift:collection:", 0) == 0 ||
                         expression_value.string.rfind("\x1fnift:file:", 0) == 0 ||
                         expression_value.string.rfind("\x1fnift:stream:", 0) == 0)) {
                        fail(source_path, source, i, "cannot render a struct, callable or collection instance $[" + key + "]; select a member or a value-returning call first");
                        break;
                    }
                    output += render_expression_value(expression_value);
                    i = end + 1;
                    continue;
                }
                if (strict_script_mode_ || (!expression_error.empty() && expression_error.rfind("unknown value or malformed expression:", 0) != 0)) {
                    if (expression_error.empty()) expression_error = "unknown value or malformed expression: " + trim_copy(key);
                    fail(source_path, source, i, expression_error);
                    break;
                }
                // A direct legacy lookup below preserves its established diagnostics.
                std::shared_ptr<const json::Document> pagination_value;
                if (resolve_pagination_value(trim_copy(key), pagination_value)) {
                    output += pagination_value->is_string() ? pagination_value->string : pagination_value->dump(0);
                    i = end + 1;
                    continue;
                }

                const std::string metadata_value = metadata(key);
                const bool known_metadata = built_in_metadata_name(key);

                if (known_metadata) {
                    output += metadata_value;
                    i = end + 1;
                    continue;
                }

                std::string json_output;
                std::string json_error;
                if (json_value(key, json_output, json_error)) {
                    if (!json_error.empty()) {
                        fail(source_path, source, i, json_error);
                        break;
                    }
                    output += json_output;
                    i = end + 1;
                    continue;
                }
            }
        }

        if (source.compare(i, 5, "@item") == 0 &&
            (i + 5 == source.size() || source[i + 5] == '{' || std::isspace(static_cast<unsigned char>(source[i + 5])))) {
            if (!pagination_collecting_) { fail(source_path, source, i, "@item requires pagination on the tracked item"); break; }
            std::size_t block_open = i + 5;
            while (block_open < source.size() && std::isspace(static_cast<unsigned char>(source[block_open]))) ++block_open;
            if (block_open >= source.size() || source[block_open] != '{') { fail(source_path, source, i, "@item must be followed by a '{...}' block"); break; }
            std::size_t block_close = 0;
            if (!find_balanced(source, block_open, '{', '}', block_close)) { fail(source_path, source, block_open, "@item block has no matching '}'"); break; }
            const auto body = normalize_control_block_body(source.substr(block_open + 1, block_close - block_open - 1));
            push_json_scope();
            const auto nested = parse(body.text, source_path, depth + 1);
            pop_json_scope();
            if (!nested.ok) break;
            result_.pagination_items.push_back(nested.output);
            i = block_close + 1;
            continue;
        }

        if (source.compare(i, 9, "@paginate") == 0 &&
            (i + 9 == source.size() || !(std::isalnum(static_cast<unsigned char>(source[i + 9])) || source[i + 9] == '_'))) {
            if (!pagination_collecting_) { fail(source_path, source, i, "@paginate requires pagination on the tracked item"); break; }
            if (++result_.paginate_count > 1) { fail(source_path, source, i, "paginated tracked items must execute exactly one @paginate"); break; }
            output += "\x1dNIFT_PAGINATE\x1d";
            i += 9;
            continue;
        }

        if (source.compare(i, 4, "@if(") == 0) {
            std::size_t condition_close = 0;
            if (!find_balanced(source, i + 3, '(', ')', condition_close)) {
                fail(source_path, source, i, "@if has no matching ')' for its condition");
                break;
            }

            std::size_t block_open = condition_close + 1;
            while (block_open < source.size() &&
                   (source[block_open] == ' ' || source[block_open] == '\t' ||
                    source[block_open] == '\r' || source[block_open] == '\n')) {
                ++block_open;
            }
            if (block_open >= source.size() || source[block_open] != '{') {
                fail(source_path, source, i, "@if(...) must be followed by a '{...}' block");
                break;
            }

            std::size_t block_close = 0;
            if (!find_balanced(source, block_open, '{', '}', block_close)) {
                fail(source_path, source, block_open, "@if block has no matching '}'");
                break;
            }

            bool selected = false;
            bool condition_value = false;
            std::string condition_error;
            if (!evaluate_condition(
                    source.substr(i + 4, condition_close - (i + 4)),
                    condition_value,
                    condition_error)) {
                fail(source_path, source, i, condition_error);
                break;
            }

            const std::string control_indent = insertion_indent(output);
            const int insertion_code_block_depth = code_block_depth_;
            std::size_t chain_end = block_close + 1;
            if (condition_value) {
                const auto body = normalize_control_block_body(
                    source.substr(block_open + 1, block_close - block_open - 1));
                push_json_scope();
                const auto nested = parse(body.text, source_path, depth + 1);
                pop_json_scope();
                if (!nested.ok) break;
                append_indented(output, nested.output, control_indent, insertion_code_block_depth);
                selected = true;
            }

            std::size_t cursor = block_close + 1;
            while (cursor < source.size()) {
                const std::size_t whitespace_start = cursor;
                while (cursor < source.size() &&
                       (source[cursor] == ' ' || source[cursor] == '\t' ||
                        source[cursor] == '\r' || source[cursor] == '\n')) {
                    ++cursor;
                }

                if (source.compare(cursor, 4, "else") != 0 ||
                    (cursor + 4 < source.size() &&
                     (std::isalnum(static_cast<unsigned char>(source[cursor + 4])) ||
                      source[cursor + 4] == '_'))) {
                    chain_end = whitespace_start;
                    break;
                }
                cursor += 4;

                while (cursor < source.size() &&
                       (source[cursor] == ' ' || source[cursor] == '\t' ||
                        source[cursor] == '\r' || source[cursor] == '\n')) {
                    ++cursor;
                }

                bool branch_condition = true;
                bool is_else_if = false;
                if (source.compare(cursor, 2, "if") == 0 &&
                    cursor + 2 < source.size() &&
                    (source[cursor + 2] == '(' ||
                     source[cursor + 2] == ' ' || source[cursor + 2] == '\t' ||
                     source[cursor + 2] == '\r' || source[cursor + 2] == '\n')) {
                    is_else_if = true;
                    cursor += 2;
                    while (cursor < source.size() &&
                           (source[cursor] == ' ' || source[cursor] == '\t' ||
                            source[cursor] == '\r' || source[cursor] == '\n')) {
                        ++cursor;
                    }
                    if (cursor >= source.size() || source[cursor] != '(') {
                        fail(source_path, source, cursor, "else if must contain a parenthesised condition");
                        break;
                    }
                    std::size_t else_condition_close = 0;
                    if (!find_balanced(source, cursor, '(', ')', else_condition_close)) {
                        fail(source_path, source, cursor, "else if has no matching ')' for its condition");
                        break;
                    }

                    if (!selected) {
                        std::string else_error;
                        if (!evaluate_condition(
                                source.substr(cursor + 1, else_condition_close - cursor - 1),
                                branch_condition,
                                else_error)) {
                            fail(source_path, source, cursor, else_error);
                            break;
                        }
                    }
                    cursor = else_condition_close + 1;
                }

                while (cursor < source.size() &&
                       (source[cursor] == ' ' || source[cursor] == '\t' ||
                        source[cursor] == '\r' || source[cursor] == '\n')) {
                    ++cursor;
                }

                if (cursor >= source.size() || source[cursor] != '{') {
                    fail(source_path, source, cursor, "else/else if must be followed by a '{...}' block");
                    break;
                }

                std::size_t else_block_close = 0;
                if (!find_balanced(source, cursor, '{', '}', else_block_close)) {
                    fail(source_path, source, cursor, "else/else if block has no matching '}'");
                    break;
                }

                if (!selected && branch_condition) {
                    const auto body = normalize_control_block_body(
                        source.substr(cursor + 1, else_block_close - cursor - 1));
                    push_json_scope();
                    ++loop_depth_;
                    const auto nested = parse(body.text, source_path, depth + 1);
                    --loop_depth_;
                    pop_json_scope();
                    if (!nested.ok) break;
                    append_indented(output, nested.output, control_indent, insertion_code_block_depth);
                    // A break/continue/return raised inside an else branch must
                    // propagate to the enclosing loop/function; do not consume
                    // it here or the control-flow signal is silently lost.
                    if (pending_control_.kind != ControlFlow::None) break;
                    selected = true;
                }

                cursor = else_block_close + 1;
                chain_end = cursor;

                if (!is_else_if) {
                    std::size_t after_else = cursor;
                    while (after_else < source.size() &&
                           (source[after_else] == ' ' || source[after_else] == '\t' ||
                            source[after_else] == '\r' || source[after_else] == '\n')) {
                        ++after_else;
                    }
                    if (source.compare(after_else, 4, "else") == 0) {
                        fail(source_path, source, after_else,
                             "plain else must be the final branch of an @if chain");
                    }
                    chain_end = cursor;
                    break;
                }
            }

            if (!result_.ok) break;
            i = chain_end;
            continue;
        }

        // Shared prepared-loop infrastructure (CP16 + runtime completion):
        // prepare a supported loop body once into statement trees, then execute
        // those trees against live bindings each iteration. Unsupported syntax
        // falls back to the compatibility evaluator, which remains the oracle.
            // Split a simple binding.method(args) call into root/method/arg trees
            // once at preparation time so hot loops do not re-parse the call text.
            auto split_native_call=[&](const std::string& text,std::string& root,std::string& method,std::vector<std::unique_ptr<nift::ast::Expr>>& args)->bool{
                const std::size_t lp=text.find('(');
                if(lp==std::string::npos||text.empty()||text.back()!=')')return false;
                std::string target=trim_copy(text.substr(0,lp));
                const std::size_t dot=target.rfind('.');
                if(dot==std::string::npos)return false;
                root=trim_copy(target.substr(0,dot));method=trim_copy(target.substr(dot+1));
                if(!valid_binding_identifier(root)||!valid_binding_identifier(method))return false;
                const std::string body=text.substr(lp+1,text.size()-lp-2);
                std::vector<std::string> arg_texts;std::string cur;int dep=0;bool q=false;char qc=0;
                for(std::size_t k=0;k<body.size();++k){char ch=body[k];if(q){if(ch=='\\'&&k+1<body.size())++k;else if(ch==qc)q=false;cur+=ch;continue;}if(ch=='\''||ch=='"'){q=true;qc=ch;cur+=ch;continue;}if(ch=='('||ch=='[')++dep;else if(ch==')'||ch==']')--dep;if(ch==','&&dep==0){arg_texts.push_back(cur);cur.clear();continue;}cur+=ch;}
                arg_texts.push_back(cur);
                for(const auto& at:arg_texts){std::string a=trim_copy(at);if(a.empty())continue;auto r=nift::ast::parse_expression(a);if(!r.supported)return false;args.push_back(std::move(r.expr));}
                return true;};
        std::function<bool(const std::string&,std::vector<std::unique_ptr<nift::ast::Stmt>>&)> prepare_loop_body;
        prepare_loop_body=[&](const std::string& text,std::vector<std::unique_ptr<nift::ast::Stmt>>& out_stmts)->bool{
                        out_stmts.clear();
            for(std::size_t bp=0;bp<text.size();){
                while(bp<text.size()&&std::isspace((unsigned char)text[bp]))++bp;
                if(bp>=text.size())return true;
                if(text.compare(bp,2,"$[")==0){std::size_t close=0;if(!find_balanced(text,bp+1,'[',']',close))return false;auto ps=nift::ast::parse_statement(text.substr(bp+2,close-bp-2));if(!ps.supported)return false;const auto k=ps.stmt->kind;if(k==nift::ast::StmtKind::Declaration)ps.stmt->decl_type=expression_type(ps.stmt->text.substr(ps.stmt->text.find(":=")+2));if(k==nift::ast::StmtKind::Assignment||k==nift::ast::StmtKind::CompoundAssignment||k==nift::ast::StmtKind::Increment||k==nift::ast::StmtKind::Declaration){out_stmts.push_back(std::move(ps.stmt));bp=close+1;continue;}if(k==nift::ast::StmtKind::Expression&&ps.stmt->expr&&ps.stmt->expr->kind==nift::ast::Kind::Call){std::string nroot,nmethod;std::vector<std::unique_ptr<nift::ast::Expr>> nargs;if(split_native_call(ps.stmt->expr->text,nroot,nmethod,nargs)){ps.stmt->name=nroot;ps.stmt->op=nmethod;ps.stmt->call_args=std::move(nargs);out_stmts.push_back(std::move(ps.stmt));bp=close+1;continue;}return false;}return false;}
                if(text.compare(bp,4,"@if(")==0){std::size_t hc=0;if(!find_balanced(text,bp+3,'(',')',hc))return false;std::size_t bo=hc+1;while(bo<text.size()&&std::isspace((unsigned char)text[bo]))++bo;std::size_t bc=0;if(bo>=text.size()||text[bo]!='{'||!find_balanced(text,bo,'{','}',bc))return false;auto cond=nift::ast::parse_expression(text.substr(bp+4,hc-(bp+4)));if(!cond.supported)return false;auto st=std::make_unique<nift::ast::Stmt>();st->kind=nift::ast::StmtKind::If;st->condition=std::move(cond.expr);if(!prepare_loop_body(text.substr(bo+1,bc-bo-1),st->body))return false;out_stmts.push_back(std::move(st));bp=bc+1;continue;}
                if(text.compare(bp,7,"@while(")==0){std::size_t hc=0;if(!find_balanced(text,bp+6,'(',')',hc))return false;std::size_t bo=hc+1;while(bo<text.size()&&std::isspace((unsigned char)text[bo]))++bo;std::size_t bc=0;if(bo>=text.size()||text[bo]!='{'||!find_balanced(text,bo,'{','}',bc))return false;auto cond=nift::ast::parse_expression(text.substr(bp+7,hc-(bp+7)));if(!cond.supported)return false;auto st=std::make_unique<nift::ast::Stmt>();st->kind=nift::ast::StmtKind::While;st->condition=std::move(cond.expr);if(!prepare_loop_body(text.substr(bo+1,bc-bo-1),st->body))return false;out_stmts.push_back(std::move(st));bp=bc+1;continue;}
                if(text.compare(bp,5,"@for(")==0){std::size_t hc=0;if(!find_balanced(text,bp+4,'(',')',hc))return false;std::size_t bo=hc+1;while(bo<text.size()&&std::isspace((unsigned char)text[bo]))++bo;std::size_t bc=0;if(bo>=text.size()||text[bo]!='{'||!find_balanced(text,bo,'{','}',bc))return false;std::string header=trim_copy(text.substr(bp+5,hc-(bp+5)));auto sep=header.find(':');if(sep==std::string::npos)return false;auto st=std::make_unique<nift::ast::Stmt>();st->kind=nift::ast::StmtKind::For;st->name=trim_copy(header.substr(0,sep));auto it=nift::ast::parse_expression(trim_copy(header.substr(sep+1)));if(!it.supported||!valid_binding_identifier(st->name))return false;st->iterable=std::move(it.expr);if(!prepare_loop_body(text.substr(bo+1,bc-bo-1),st->body))return false;out_stmts.push_back(std::move(st));bp=bc+1;continue;}
                if(text.compare(bp,5,"break")==0&&(bp+5>=text.size()||std::isspace((unsigned char)text[bp+5]))){auto st=std::make_unique<nift::ast::Stmt>();st->kind=nift::ast::StmtKind::Break;out_stmts.push_back(std::move(st));bp+=5;continue;}
                if(text.compare(bp,8,"continue")==0&&(bp+8>=text.size()||std::isspace((unsigned char)text[bp+8]))){auto st=std::make_unique<nift::ast::Stmt>();st->kind=nift::ast::StmtKind::Continue;out_stmts.push_back(std::move(st));bp+=8;continue;}
                if(text.compare(bp,8,"@return(")==0){std::size_t hc=0;if(!find_balanced(text,bp+7,'(',')',hc))return false;auto st=std::make_unique<nift::ast::Stmt>();st->kind=nift::ast::StmtKind::Return;std::string ex=trim_copy(text.substr(bp+8,hc-(bp+8)));if(!ex.empty()){auto r=nift::ast::parse_expression(ex);if(!r.supported)return false;st->expr=std::move(r.expr);}out_stmts.push_back(std::move(st));bp=hc+1;continue;}
                return false;
            }
            return true;};
        std::function<bool(const std::vector<std::unique_ptr<nift::ast::Stmt>>&,std::string&)> execute_body;
        std::function<bool(const nift::ast::Stmt&,std::string&)> execute_prepared;
        auto ast_context=[&](){nift::ast::Context c;c.resolve=[&](const std::string& name,json::Document& out,std::string& e){for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(name);if(it!=sc->end()){it->second.sync();out=*it->second.value;return true;}}e="unknown value or malformed expression: "+name;return false;};c.legacy=[&](const std::string& x,json::Document& out,std::string& e){return evaluate_expression(x,out,e);};c.call=[&](const std::string& name,std::vector<json::Document>&& args,json::Document& out,std::string& e)->bool{
            // Prepared dispatch for user callables: bind args, run the prepared
            // body once, propagate the return value. Any failure falls back to
            // the legacy evaluator (the oracle) via the Call handler.
            const Callable* callee=nullptr;
            auto ci=callables_.find(name);
            if(ci!=callables_.end())callee=&ci->second;
            if(!callee&&active_module_env_){auto mit=active_module_env_->callables.find(name);if(mit!=active_module_env_->callables.end())callee=&mit->second;}
            if(!callee||callee->fragment)return false;
            if(callee->variadic_param.empty()?args.size()!=callee->params.size():args.size()<callee->params.size())return false;
            auto& prep=prepared_callables_[callee];
            if(!prep.ready){
                std::string program,perr;
                if(!translate_function_program(callee->body,program,perr))return false;
                if(!prepare_loop_body(program,prep.stmts))return false;
                prep.ready=true;
            }
            if(callable_call_depth_>=kMaxCallableDepth){e="callable recursion depth exceeded: "+name;return false;}
            push_variable_scope();auto& scope=variable_scopes_.back();
            for(std::size_t ai=0;ai<callee->params.size()&&ai<args.size();++ai){auto sp=std::make_shared<json::Document>(std::move(args[ai]));scope.emplace(callee->params[ai],VariableBinding{sp,nift_binding_type(*sp),true,false});}
            if(!callee->variadic_param.empty()){json::Document rest=json::Document::make_array();for(std::size_t ai=callee->params.size();ai<args.size();++ai)rest.array.push_back(std::move(args[ai]));auto sp=std::make_shared<json::Document>(std::move(rest));scope.emplace(callee->variadic_param,VariableBinding{sp,nift_binding_type(*sp),true,false});}
            ++callable_call_depth_;const int saved_loop_depth=loop_depth_;loop_depth_=0;pending_control_={};
            std::string pe;const bool ok=execute_body(prep.stmts,pe);
            loop_depth_=saved_loop_depth;--callable_call_depth_;
            pop_variable_scope();
            if(!ok){e=pe;return false;}
            if(pending_control_.kind==ControlFlow::Return){out=pending_control_.value?std::move(*pending_control_.value):json::Document(nullptr);pending_control_={};}
            else out=json::Document(nullptr);
            return true;};c.resolve_ref=[&](const std::string& name,std::shared_ptr<const json::Document>& out,std::string& e){for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(name);if(it!=sc->end()){it->second.sync();out=it->second.value;return true;}}e="unknown value or malformed expression: "+name;return false;};c.native_method=[&](const json::Document& recv,const std::string& method,std::vector<json::Document>&& args,json::Document& out,std::string& e)->bool{
            if(method=="to_string"&&recv.is_number()){out=json::Document(render_expression_value(recv));return true;}
            if(method=="to_string"&&recv.is_string()){out=recv;return true;}
            if(method=="to_int"&&recv.is_number()){out=json::Document((double)(long long)recv.num);return true;}
            if(method=="to_double"&&recv.is_number()){out=recv;return true;}
            if(method=="abs"&&recv.is_number()){out=json::Document(std::fabs(recv.num));return true;}
            if(method=="floor"&&recv.is_number()){out=json::Document(std::floor(recv.num));return true;}
            if(method=="ceil"&&recv.is_number()){out=json::Document(std::ceil(recv.num));return true;}
            if(method=="round"&&recv.is_number()){out=json::Document(std::round(recv.num));return true;}
            if(method=="length"&&recv.is_string()){out=json::Document((double)recv.string.size());return true;}
            if(method=="trim"&&recv.is_string()){std::string s=recv.string;auto b=s.find_first_not_of(" \t\r\n");if(b==std::string::npos)s.clear();else{s=s.substr(b);auto epos=s.find_last_not_of(" \t\r\n");s=s.substr(0,epos+1);}out=json::Document(s);return true;}
            if(method=="to_lower"&&recv.is_string()){std::string s=recv.string;for(auto&ch:s)ch=(char)std::tolower((unsigned char)ch);out=json::Document(s);return true;}
            if(method=="to_upper"&&recv.is_string()){std::string s=recv.string;for(auto&ch:s)ch=(char)std::toupper((unsigned char)ch);out=json::Document(s);return true;}
            if(method=="size"){if(recv.is_array()){out=json::Document((double)recv.array.size());return true;}if(recv.is_object()){out=json::Document((double)recv.object.size());return true;}}
            if(method=="length"){if(recv.is_array()){out=json::Document((double)recv.array.size());return true;}if(recv.is_string()){out=json::Document((double)recv.string.size());return true;}if(recv.is_object()){out=json::Document((double)recv.object.size());return true;}}
            if(method=="empty"){if(recv.is_array()){out=json::Document(recv.array.empty());return true;}if(recv.is_string()){out=json::Document(recv.string.empty());return true;}if(recv.is_object()){out=json::Document(recv.object.empty());return true;}}
            if(method=="first"&&recv.is_array()){if(recv.array.empty()){e="first: array is empty";return false;}out=recv.array.front();return true;}
            if(method=="last"&&recv.is_array()){if(recv.array.empty()){e="last: array is empty";return false;}out=recv.array.back();return true;}
            return false;};c.render=[&](const json::Document& v){return render_expression_value(v);};return c;};
        auto assign_plain=[&](const std::string& name,json::Document v,std::string& e)->bool{for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(name);if(it==sc->end())continue;if(!it->second.mutable_binding){e="cannot assign to const binding: "+name;return false;}const int at=nift_binding_type(v);if(!nift_type_assignable(at,it->second.type)){e="cannot change binding type: "+name;return false;}it->second.rebind(std::make_shared<json::Document>(std::move(v)));return true;}e="assignment to undefined binding: "+name;return false;};
            // Resolve an Index/Member chain to a non-const Document reference so
            // that assignment mutates the aliased collection element in place
            // (json-mutate's e["total"] = e.v * 2 must write back into arr).
            std::function<bool(const nift::ast::Expr&,json::Document*&,std::string&)> resolve_target_ref;
            resolve_target_ref=[&](const nift::ast::Expr& t,json::Document*& out,std::string& e)->bool{
                if(t.kind==nift::ast::Kind::Binding){for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(t.name);if(it!=sc->end()){it->second.sync();if(!it->second.mutable_binding){e="cannot assign to const binding: "+t.name;return false;}out=&*it->second.value;return true;}}e="assignment to undefined binding: "+t.name;return false;}
                if(t.kind==nift::ast::Kind::Index){json::Document* b=nullptr;if(!resolve_target_ref(*t.left,b,e))return false;json::Document i;auto c=ast_context();if(!nift::ast::evaluate(*t.right,c,i,e))return false;if(b->is_array()&&i.is_number()&&i.num>=0&&std::trunc(i.num)==i.num&&(std::size_t)i.num<b->array.size()){out=&b->array[(std::size_t)i.num];return true;}if(b->is_object()&&i.is_string()){out=&(*b)[i.string];return true;}e="invalid index target";return false;}
                if(t.kind==nift::ast::Kind::Member){json::Document* b=nullptr;if(!resolve_target_ref(*t.left,b,e))return false;if(!b->is_object()){e="value has no member: "+t.name;return false;}out=&(*b)[t.name];return true;}
                e="unsupported assignment target";return false;};
            auto assign_indexed=[&](const nift::ast::Expr& target,json::Document rhs,std::string& e)->bool{json::Document* slot=nullptr;if(!resolve_target_ref(target,slot,e))return false;*slot=std::move(rhs);last_expression_mutation_=true;return true;};
        auto large_num=[&](const json::Document& d)->bool{return d.is_number()&&(d.type==json::Type::StrNumber||d.num>=9007199254740992.0||d.num<=-9007199254740992.0);};
            // Execute a prepared native collection call (push/size/pop/empty/etc.)
            // against the live receiver binding. Falls back to the legacy evaluator
            // when the receiver is not a plain array or the method is unsupported.
            auto execute_native_call=[&](const nift::ast::Stmt& st,std::string& e)->bool{
                auto c=ast_context();std::vector<json::Document> av;
                VariableBinding* rb=nullptr;
                for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(st.name);if(it!=sc->end()){rb=&it->second;break;}}
                for(const auto& a:st.call_args){json::Document v;if(!nift::ast::evaluate(*a,c,v,e))return false;av.push_back(std::move(v));}
                if(!rb||!rb->value){json::Document lege;return c.legacy(st.text,lege,e);}
                if(rb->value->is_string()&&rb->value->string.rfind("\x1fnift:file:",0)==0){
                    auto fid=rb->value->string.substr(11);
                    auto fi=file_instances_.find(fid);
                    if(fi==file_instances_.end()){e="invalid file handle";return false;}
                    auto& f=fi->second;
                    if(st.op=="write"||st.op=="write_line"){
                        if(av.size()!=1||!f->open||!(f->mode=="w"||f->mode=="a"||f->mode=="rw")){if(av.size()!=1&&e.empty())e=st.op+": expected one value";return false;}
                        json::Document v=std::move(av[0]);
                        if(v.is_array()||v.is_object()||(v.is_string()&&v.string.rfind("\x1fnift:",0)==0)){e=st.op+": value is not directly renderable";return false;}
                        std::string d=render_expression_value(v);
                        if(st.op=="write_line")d+='\n';
                        const std::size_t base=std::min(f->cursor,f->working.size());
                        const std::size_t ov=std::min(d.size(),f->working.size()-base);
                        f->working.replace(base,ov,d);
                        f->cursor=base+d.size();
                        f->dirty=f->working!=f->saved;
                        return true;
                    }
                    {json::Document lege;return c.legacy(st.text,lege,e);}
                }
                if(rb->value->is_string()&&rb->value->string.rfind("\x1fnift:collection:",0)==0){
                    auto cid=rb->value->string.substr(17);
                    auto ci=collection_instances_.find(cid);
                    if(ci==collection_instances_.end()){e="invalid collection handle";return false;}
                    auto& cc=ci->second;
                    const bool is_map=cc->kind==CollectionKind::Map||cc->kind==CollectionKind::SortedMap;
                    auto ckey=[&](const json::Document& x)->std::string{if(x.is_bool())return std::string("b")+(x.boolean?"1":"0");if(x.is_string()){if(x.string.rfind("\x1fnift:",0)==0)return "";return std::string("s")+x.string;}if(x.type==json::Type::StrNumber)return std::string("N")+x.string;double nn=x.num;if(nn==0.0)nn=0.0;uint64_t bits=0;std::memcpy(&bits,&nn,sizeof(bits));return std::string("n")+std::to_string(bits);};
                    if(!is_map&&(st.op=="add"||st.op=="contains"||st.op=="size"||st.op=="empty")){
                        if(st.op=="size"){if(!av.empty()){e="size: expected no arguments";return false;}return true;}
                        if(st.op=="empty"){if(!av.empty()){e="empty: expected no arguments";return false;}return true;}
                        if(st.op=="add"||st.op=="contains"){
                            if(av.size()!=1){e=st.op+": expected one value";return false;}
                            const json::Document& v=av[0];
                            const std::string k=ckey(v);
                            const bool indexed=!k.empty()&&!(v.is_number()&&v.type!=json::Type::StrNumber&&cc->has_huge_int);
                            if(!indexed){json::Document lege;return c.legacy(st.text,lege,e);}
                            const bool present=cc->scalar_keys.count(k)!=0;
                            if(st.op=="contains"){return true;}
                            if(!rb->mutable_binding){e="cannot mutate const collection: "+st.name;return false;}
                            if(st.op=="add"&&!present){cc->values.push_back(v);cc->scalar_keys.insert(k);}else if(st.op=="add"&&present){/* already present: no-op */}last_expression_mutation_=true;return true;
                        }
                    }
                    {json::Document lege;return c.legacy(st.text,lege,e);}
                }
                if(!rb->value->is_array()){json::Document lege;return c.legacy(st.text,lege,e);}
                auto& arr=rb->value->array;
                if(st.op=="push"){if(av.size()!=1){e="push: expected one argument";return false;}if(!rb->mutable_binding){e="cannot mutate const array: "+st.name;return false;}arr.push_back(std::move(av[0]));last_expression_mutation_=true;return true;}
                if(st.op=="size"||st.op=="length"){if(!av.empty()){e=st.op+": expected no arguments";return false;}last_expression_mutation_=false;return true;}
                if(st.op=="empty"){if(!av.empty()){e="empty: expected no arguments";return false;}last_expression_mutation_=false;return true;}
                if(st.op=="first"||st.op=="last"||st.op=="pop"){if(!av.empty()){e=st.op+": expected no arguments";return false;}if(arr.empty()){e=st.op+": array is empty";return false;}if(st.op=="pop"){if(!rb->mutable_binding){e="cannot mutate const array: "+st.name;return false;}arr.pop_back();last_expression_mutation_=true;}else last_expression_mutation_=false;return true;}
                if(st.op=="clear"){if(!av.empty()){e="clear: expected no arguments";return false;}if(!rb->mutable_binding){e="cannot mutate const array: "+st.name;return false;}arr.clear();last_expression_mutation_=true;return true;}
                {json::Document lege;return c.legacy(st.text,lege,e);}};
        execute_body=[&](const std::vector<std::unique_ptr<nift::ast::Stmt>>& body,std::string& e)->bool{for(const auto& s:body){if(pending_control_.kind!=ControlFlow::None)return true;if(!execute_prepared(*s,e))return false;}return true;};
        execute_prepared=[&](const nift::ast::Stmt& st,std::string& e)->bool{auto c=ast_context();json::Document rhs,oldv,next;if(st.kind==nift::ast::StmtKind::If){json::Document cv;if(!st.condition){e="unsupported prepared if condition";return false;}if(!nift::ast::evaluate(*st.condition,c,cv,e))return false;if(!nift::ast::truthy(cv))return true;return execute_body(st.body,e);}if(st.kind==nift::ast::StmtKind::While){json::Document cv;while(true){if(pending_control_.kind!=ControlFlow::None)return true;if(!st.condition){e="unsupported prepared while condition";return false;}if(!nift::ast::evaluate(*st.condition,c,cv,e))return false;if(!nift::ast::truthy(cv))return true;push_json_scope();++loop_depth_;const bool wok=execute_body(st.body,e);--loop_depth_;pop_json_scope();if(!wok)return false;if(pending_control_.kind==ControlFlow::Break){pending_control_={};return true;}if(pending_control_.kind==ControlFlow::Continue){pending_control_={};continue;}}return true;}if(st.kind==nift::ast::StmtKind::For){std::shared_ptr<const json::Document> col;json::Document colv;if(st.iterable&&st.iterable->kind==nift::ast::Kind::Binding){if(!c.resolve_ref(st.iterable->name,col,e))return false;}else if(st.iterable){if(!nift::ast::evaluate(*st.iterable,c,colv,e))return false;col=std::make_shared<const json::Document>(std::move(colv));}else{e="unsupported prepared for iterable";return false;}if(!col->is_array()){e="for iteration requires an array";return false;}for(std::size_t k=0;k<col->array.size();++k){if(pending_control_.kind!=ControlFlow::None)break;push_json_scope();variable_scopes_.back().emplace(st.name,VariableBinding{std::const_pointer_cast<json::Document>(std::shared_ptr<const json::Document>(col,&col->array[k])),nift_binding_type(col->array[k]),true,false});++loop_depth_;const bool bok=execute_body(st.body,e);--loop_depth_;pop_json_scope();if(!bok)return false;if(pending_control_.kind==ControlFlow::Break){pending_control_={};break;}if(pending_control_.kind==ControlFlow::Continue){pending_control_={};continue;}}return true;}if(st.kind==nift::ast::StmtKind::Break){pending_control_.kind=ControlFlow::Break;pending_control_.value.reset();return true;}if(st.kind==nift::ast::StmtKind::Continue){pending_control_.kind=ControlFlow::Continue;pending_control_.value.reset();return true;}if(st.kind==nift::ast::StmtKind::Return){json::Document rv;if(st.expr){if(!nift::ast::evaluate(*st.expr,c,rv,e))return false;pending_control_.value=std::make_shared<json::Document>(std::move(rv));}else pending_control_.value.reset();pending_control_.kind=ControlFlow::Return;return true;}if(st.kind==nift::ast::StmtKind::Expression){return execute_native_call(st,e);}if(st.kind==nift::ast::StmtKind::Declaration){if(!nift::ast::evaluate(*st.expr,c,rhs,e))return false;if(large_num(rhs)){json::Document lege;return c.legacy(st.text,lege,e);}if(variable_scopes_.empty()){e="no scope for declaration: "+st.name;return false;}auto& scope=variable_scopes_.back();if(scope.count(st.name)){e="binding already declared in this scope: "+st.name;return false;}auto sp=std::make_shared<json::Document>(std::move(rhs));const int dt=st.decl_type;const int bt=(dt==3)?3:((dt==2)?nift_binding_type(*sp):((dt>=0&&dt!=2&&dt!=3)?dt:nift_binding_type(*sp)));scope.emplace(st.name,VariableBinding{sp,bt,true,false});return true;}if(st.kind==nift::ast::StmtKind::Assignment){if(!nift::ast::evaluate(*st.expr,c,rhs,e))return false;if(large_num(rhs)){json::Document lege;return c.legacy(st.text,lege,e);}if(st.target)return assign_indexed(*st.target,std::move(rhs),e);return assign_plain(st.name,std::move(rhs),e);}VariableBinding* cb=nullptr;for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(st.name);if(it!=sc->end()){cb=&it->second;break;}}if(!cb){e="assignment to undefined binding: "+st.name;return false;}cb->sync();if(!cb->mutable_binding){e="cannot assign to const binding: "+st.name;return false;}if(st.kind==nift::ast::StmtKind::Increment){if(!cb->value->is_number()){e="increment/decrement requires a numeric lvalue";return false;}oldv=*cb->value;if(large_num(oldv)){json::Document lege;return c.legacy(st.text,lege,e);}next=json::Document(oldv.num+(st.op=="++"?1.0:-1.0));return assign_plain(st.name,std::move(next),e);}if(!nift::ast::evaluate(*st.expr,c,rhs,e))return false;if(st.op=="+="&&cb->value->is_string()&&rhs.is_string()){if(cb->value.use_count()==2){cb->value->string.append(rhs.string);last_expression_mutation_=true;return true;}oldv=*cb->value;next=json::Document(oldv.string+rhs.string);}else if(st.op=="+="&&cb->value->is_array()&&rhs.is_array()){oldv=*cb->value;next=json::Document::make_array();next.array.reserve(oldv.array.size()+rhs.array.size());next.array.insert(next.array.end(),oldv.array.begin(),oldv.array.end());next.array.insert(next.array.end(),rhs.array.begin(),rhs.array.end());}else{oldv=*cb->value;if(!oldv.is_number()||!rhs.is_number()){e="arithmetic operators require numeric operands";return false;}if(large_num(oldv)||large_num(rhs)){json::Document lege;return c.legacy(st.text,lege,e);}if(st.op=="+=")next=json::Document(oldv.num+rhs.num);else if(st.op=="-=")next=json::Document(oldv.num-rhs.num);else if(st.op=="*=")next=json::Document(oldv.num*rhs.num);else if(st.op=="/="){if(rhs.num==0){e="division by zero";return false;}next=json::Document(oldv.num/rhs.num);}else{if(rhs.num==0){e="modulo by zero";return false;}next=json::Document(std::fmod(oldv.num,rhs.num));}}return assign_plain(st.name,std::move(next),e);};
        if (source.compare(i, 7, "@while(") == 0) {
            std::size_t hc=0;if(!find_balanced(source,i+6,'(',')',hc)){fail(source_path,source,i,"@while has no matching ')' for its condition");break;}
            std::size_t bo=hc+1;while(bo<source.size()&&std::isspace((unsigned char)source[bo]))++bo;std::size_t bc=0;
            if(bo>=source.size()||source[bo]!='{'||!find_balanced(source,bo,'{','}',bc)){fail(source_path,source,i,"@while(...) must be followed by a '{...}' block");break;}
            const std::string condition=source.substr(i+7,hc-(i+7));
            const auto body=normalize_control_block_body(source.substr(bo+1,bc-bo-1));
            auto prepared_condition=nift::ast::parse_expression(condition);
            std::vector<std::unique_ptr<nift::ast::Stmt>> prepared_body; const bool prepared_body_ok=prepare_loop_body(body.text,prepared_body);
            
            while(result_.ok){bool yes=false;std::string e;if(prepared_condition.supported){auto c=ast_context();json::Document cv;if(!nift::ast::evaluate(*prepared_condition.expr,c,cv,e)){fail(source_path,source,i,e);break;}yes=nift::ast::truthy(cv);last_expression_mutation_=false;}else if(!evaluate_condition(condition,yes,e)){fail(source_path,source,i,e);break;}if(!yes)break;
                push_json_scope(); ++loop_depth_; RenderResult nested;if(prepared_body_ok){if(!execute_body(prepared_body,e)){nested.ok=false;nested.error.message=e;}}else nested=parse(body.text,source_path,depth+1); --loop_depth_; pop_json_scope();if(!nested.ok){fail(source_path,source,i,nested.error.message);break;}append_indented(output,nested.output,"",code_block_depth_);
                if (pending_control_.kind == ControlFlow::Continue) { pending_control_ = {}; continue; }
                if (pending_control_.kind == ControlFlow::Break) { pending_control_ = {}; break; }
                if(pending_control_.kind!=ControlFlow::None)break;
            }
            if(!result_.ok)break;i=bc+1;continue;
        }

        if (source.compare(i, 5, "@for(") == 0) {
            std::size_t header_close = 0;
            if (!find_balanced(source, i + 4, '(', ')', header_close)) {
                fail(source_path, source, i, "@for has no matching ')' for its header");
                break;
            }

            std::size_t block_open = header_close + 1;
            while (block_open < source.size() &&
                   (source[block_open] == ' ' || source[block_open] == '\t' ||
                    source[block_open] == '\r' || source[block_open] == '\n')) {
                ++block_open;
            }
            if (block_open >= source.size() || source[block_open] != '{') {
                fail(source_path, source, i, "@for(...) must be followed by a '{...}' block");
                break;
            }

            std::size_t block_close = 0;
            if (!find_balanced(source, block_open, '{', '}', block_close)) {
                fail(source_path, source, block_open, "@for block has no matching '}'");
                break;
            }

            const std::string header = trim_copy(
                source.substr(i + 5, header_close - (i + 5)));

            bool quoted = false;
            char quote = 0;
            std::size_t paren_depth = 0;
            std::size_t bracket_depth = 0;
            std::size_t separator_position = std::string::npos;
            for (std::size_t h = 0; h < header.size(); ++h) {
                const char c = header[h];
                if (quoted) {
                    if (c == '\\') ++h;
                    else if (c == quote) quoted = false;
                    continue;
                }
                if (c == '\'' || c == '"') { quoted = true; quote = c; continue; }
                if (c == '(') { ++paren_depth; continue; }
                if (c == ')') { if (paren_depth) --paren_depth; continue; }
                if (c == '[') { ++bracket_depth; continue; }
                if (c == ']') { if (bracket_depth) --bracket_depth; continue; }

                if (paren_depth == 0 && bracket_depth == 0 && c == ':') {
                    separator_position = h;
                    break;
                }
            }

            if (separator_position == std::string::npos) {
                fail(source_path, source, i, "@for header must contain ':'");
                break;
            }

            const std::string binding_part = trim_copy(header.substr(0, separator_position));
            const std::string collection_clause = trim_copy(header.substr(separator_position + 1));
            std::string collection_expression;
            std::string sort_expression;
            bool sort_descending = false;
            std::string sort_clause_error;
            if (!parse_for_collection_clause(collection_clause, collection_expression,
                                             sort_expression, sort_descending, sort_clause_error)) {
                fail(source_path, source, i, sort_clause_error);
                break;
            }

            std::shared_ptr<const json::Document> collection;
            if (sort_expression.empty() && valid_binding_identifier(collection_expression)) {
                for (auto scope = variable_scopes_.rbegin(); scope != variable_scopes_.rend(); ++scope) {
                    auto found = scope->find(collection_expression);
                    if (found != scope->end()) { collection = found->second.value; break; }
                }
            }
            if (!collection) {
                json::Document collection_value; std::string collection_error;
                if (!evaluate_collection_value(collection_expression, collection_value, collection_error)) { fail(source_path, source, i, "@for collection: " + collection_error); break; }
                collection = std::make_shared<const json::Document>(std::move(collection_value));
            }

            // Materialize internal v4.3 sequential/set collections for the
            // existing @for engine; maps/sorted_maps are iterated directly so
            // typed keys are preserved (no JSON-object stringification).
            if(collection->is_string()&&collection->string.rfind("\x1fnift:collection:",0)==0){
                auto ci=collection_instances_.find(collection->string.substr(17));
                if(ci==collection_instances_.end()){fail(source_path,source,i,"@for collection: invalid collection");break;}
                if(ci->second->kind==CollectionKind::Map||ci->second->kind==CollectionKind::SortedMap){
                    // keep the collection reference; the dedicated map branch below iterates entries.
                }else{
                    json::Document materialized=json::Document::make_array();materialized.array=ci->second->values;if(ci->second->kind==CollectionKind::Stack)std::reverse(materialized.array.begin(),materialized.array.end());
                    collection=std::make_shared<const json::Document>(std::move(materialized));
                }
            }

            const auto body = normalize_control_block_body(
                source.substr(block_open + 1, block_close - block_open - 1));
            std::vector<std::unique_ptr<nift::ast::Stmt>> for_prepared_body;
            const bool for_prepared_ok=prepare_loop_body(body.text,for_prepared_body);
            const std::string control_indent = insertion_indent(output);
            const int insertion_code_block_depth = code_block_depth_;

            // Direct typed-entry iteration for v4.3 maps/sorted_maps: bind the
            // original typed key and value, preserving the map's key domain and
            // ordering (insertion for map, sorted for sorted_map) without an
            // intermediate JSON-object representation.
            if (collection->is_string() && collection->string.rfind("\x1fnift:collection:",0)==0) {
                auto mci=collection_instances_.find(collection->string.substr(17));
                if(mci==collection_instances_.end()||(mci->second->kind!=CollectionKind::Map&&mci->second->kind!=CollectionKind::SortedMap)){fail(source_path,source,i,"@for collection: invalid map");break;}
                const auto& entries=mci->second->entries;
                if(binding_part.size()<5||binding_part.front()!='('||binding_part.back()!=')'){
                    fail(source_path,source,i,"map @for syntax is @for((key, val) : map){...}");break;
                }
                const std::string pair=binding_part.substr(1,binding_part.size()-2);
                const auto comma=pair.find(',');
                if(comma==std::string::npos||pair.find(',',comma+1)!=std::string::npos){fail(source_path,source,i,"map @for requires exactly two bindings: (key, val)");break;}
                const std::string key_name=trim_copy(pair.substr(0,comma));
                const std::string value_name=trim_copy(pair.substr(comma+1));
                if(!valid_binding_identifier(key_name)||!valid_binding_identifier(value_name)||key_name==value_name){fail(source_path,source,i,"map @for key and value bindings must be distinct identifiers");break;}
                if(reserved_binding_name(key_name)||reserved_binding_name(value_name)){fail(source_path,source,i,"@for bindings cannot conflict with built-in metadata");break;}
                if(host_.is_contract_name(key_name)||host_.is_contract_name(value_name)){fail(source_path,source,i,"@for bindings cannot conflict with configured contract namespaces");break;}
                const bool valid_map_sort_root=sort_expression.empty()||sort_expression==key_name||sort_expression==value_name||
                    sort_expression.rfind(key_name+".",0)==0||sort_expression.rfind(key_name+"[",0)==0||
                    sort_expression.rfind(value_name+".",0)==0||sort_expression.rfind(value_name+"[",0)==0;
                if(!valid_map_sort_root){fail(source_path,source,i,"@for map sort key must begin with key/value binding '"+key_name+"' or '"+value_name+"'");break;}

                const auto old_key_it=json_bindings_.find(key_name);
                const auto old_value_it=json_bindings_.find(value_name);
                const bool had_old_key=old_key_it!=json_bindings_.end();
                const bool had_old_value=old_value_it!=json_bindings_.end();
                std::shared_ptr<const json::Document> old_key,old_value;
                if(had_old_key)old_key=old_key_it->second;
                if(had_old_value)old_value=old_value_it->second;
                const auto previous_loop=json_bindings_.find("loop");
                const bool had_previous_loop=previous_loop!=json_bindings_.end();
                std::shared_ptr<const json::Document> previous_loop_value;
                if(had_previous_loop)previous_loop_value=previous_loop->second;

                std::vector<std::size_t> order(entries.size());
                for(std::size_t n=0;n<order.size();++n)order[n]=n;
                std::vector<json::Document> sort_keys;
                if(!sort_expression.empty()){
                    sort_keys.reserve(entries.size());
                    json::Type key_type=json::Type::Null;bool have_key_type=false;
                    for(std::size_t n=0;n<entries.size();++n){
                        json_bindings_[key_name]=std::make_shared<const json::Document>(entries[n].first);
                        json_bindings_[value_name]=std::make_shared<const json::Document>(entries[n].second);
                        std::shared_ptr<const json::Document> key;std::string key_error;
                        if(!resolve_json_value(sort_expression,key,key_error)||!key_error.empty()){fail(source_path,source,i,key_error.empty()?"@for sort key is not a bound JSON path: "+sort_expression:key_error);break;}
                        if(!sortable_scalar(*key)){fail(source_path,source,i,"@for sort keys must all be numbers or all be strings: "+sort_expression);break;}
                        if(!have_key_type){key_type=key->type;have_key_type=true;}
                        else if(key->type!=key_type){fail(source_path,source,i,"@for sort keys must have the same type: "+sort_expression);break;}
                        sort_keys.push_back(*key);
                    }
                    if(!result_.ok){if(had_old_key)json_bindings_[key_name]=old_key;else json_bindings_.erase(key_name);if(had_old_value)json_bindings_[value_name]=old_value;else json_bindings_.erase(value_name);break;}
                    std::stable_sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b){const int comparison=compare_sort_keys(sort_keys[a],sort_keys[b]);return sort_descending?comparison>0:comparison<0;});
                }

                for(std::size_t position=0;position<order.size()&&result_.ok;++position){
                    const auto& entry=entries[order[position]];
                    json_bindings_[key_name]=std::make_shared<const json::Document>(entry.first);
                    json_bindings_[value_name]=std::make_shared<const json::Document>(entry.second);
                    json_bindings_["loop"]=make_loop_metadata(position,order.size());
                    push_json_scope();
                    variable_scopes_.back().emplace(key_name,VariableBinding{std::make_shared<json::Document>(entry.first),nift_binding_type(entry.first),true,false});
                    variable_scopes_.back().emplace(value_name,VariableBinding{std::make_shared<json::Document>(entry.second),nift_binding_type(entry.second),true,false});
                    ++loop_depth_;
                    const auto nested=parse(body.text,source_path,depth+1);
                    --loop_depth_;
                    pop_json_scope();
                    if(!nested.ok)break;
                    append_indented(output,nested.output,control_indent,insertion_code_block_depth);
                    if(pending_control_.kind==ControlFlow::Continue){pending_control_={};continue;}
                    if(pending_control_.kind==ControlFlow::Break){pending_control_={};break;}
                    if(body.multiline&&position+1<order.size())output+="\n"+control_indent;
                }
                if(had_old_key)json_bindings_[key_name]=std::move(old_key);else json_bindings_.erase(key_name);
                if(had_old_value)json_bindings_[value_name]=std::move(old_value);else json_bindings_.erase(value_name);
                if(had_previous_loop)json_bindings_["loop"]=std::move(previous_loop_value);else json_bindings_.erase("loop");
                if(!result_.ok)break;
                i=block_close+1;
                continue;
            }

            if (collection->is_array()) {
                if (!valid_binding_identifier(binding_part)) {
                    fail(source_path, source, i, "array @for syntax is @for(item : array){...}");
                    break;
                }
                if (reserved_binding_name(binding_part)) {
                    fail(source_path, source, i,
                         "@for binding '" + binding_part + "' conflicts with built-in metadata");
                    break;
                }
                if (host_.is_contract_name(binding_part)) {
                    fail(source_path, source, i,
                         "@for binding '" + binding_part + "' conflicts with configured contract namespace");
                    break;
                }

                if (!sort_expression.empty() &&
                    !(sort_expression == binding_part ||
                      sort_expression.rfind(binding_part + ".", 0) == 0 ||
                      sort_expression.rfind(binding_part + "[", 0) == 0)) {
                    fail(source_path, source, i,
                         "@for array sort key must begin with loop binding '" + binding_part + "'");
                    break;
                }

                const auto previous = json_bindings_.find(binding_part);
                const bool had_previous = previous != json_bindings_.end();
                std::shared_ptr<const json::Document> previous_value;
                if (had_previous) previous_value = previous->second;
                const auto previous_loop = json_bindings_.find("loop");
                const bool had_previous_loop = previous_loop != json_bindings_.end();
                std::shared_ptr<const json::Document> previous_loop_value;
                if (had_previous_loop) previous_loop_value = previous_loop->second;

                std::vector<std::size_t> order(collection->array.size());
                for (std::size_t n = 0; n < order.size(); ++n) order[n] = n;
                std::vector<json::Document> sort_keys;
                if (!sort_expression.empty()) {
                    sort_keys.reserve(collection->array.size());
                    json::Type key_type = json::Type::Null;
                    bool have_key_type = false;
                    for (std::size_t n = 0; n < collection->array.size(); ++n) {
                        const json::Document* element = &collection->array[n];
                        json_bindings_[binding_part] =
                            std::shared_ptr<const json::Document>(collection, element);
                        std::shared_ptr<const json::Document> key;
                        std::string key_error;
                        if (!resolve_json_value(sort_expression, key, key_error) || !key_error.empty()) {
                            fail(source_path, source, i, key_error.empty()
                                ? "@for sort key is not a bound JSON path: " + sort_expression
                                : key_error);
                            break;
                        }
                        if (!sortable_scalar(*key)) {
                            fail(source_path, source, i,
                                 "@for sort keys must all be numbers or all be strings: " + sort_expression);
                            break;
                        }
                        if (!have_key_type) { key_type = key->type; have_key_type = true; }
                        else if (key->type != key_type) {
                            fail(source_path, source, i,
                                 "@for sort keys must have the same type: " + sort_expression);
                            break;
                        }
                        sort_keys.push_back(*key);
                    }
                    if (!result_.ok) {
                        if (had_previous) json_bindings_[binding_part] = previous_value;
                        else json_bindings_.erase(binding_part);
                        break;
                    }
                    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                        const int comparison = compare_sort_keys(sort_keys[a], sort_keys[b]);
                        return sort_descending ? comparison > 0 : comparison < 0;
                    });
                }

                for (std::size_t position = 0; position < order.size() && result_.ok; ++position) {
                    const std::size_t index = order[position];
                    const json::Document* element = &collection->array[index];
                    json_bindings_[binding_part] =
                        std::shared_ptr<const json::Document>(collection, element);
                    json_bindings_["loop"] = make_loop_metadata(position, order.size());

                    push_json_scope();
                    variable_scopes_.back().emplace(binding_part, VariableBinding{
                        std::const_pointer_cast<json::Document>(
                            std::shared_ptr<const json::Document>(collection, element)),
                        nift_binding_type(*element), true, false});
                    ++loop_depth_;
                    RenderResult nested;
                    if(for_prepared_ok){
                        std::string pe;
                        if(!execute_body(for_prepared_body,pe)){nested.ok=false;nested.error.message=pe;}
                    }else nested=parse(body.text, source_path, depth + 1);
                    --loop_depth_;
                    pop_json_scope();
                    if (!nested.ok) break;
                    append_indented(output, nested.output, control_indent, insertion_code_block_depth);
                    if (pending_control_.kind == ControlFlow::Continue) { pending_control_ = {}; continue; }
                    if (pending_control_.kind == ControlFlow::Break) { pending_control_ = {}; break; }
                    if (body.multiline && position + 1 < order.size())
                        output += "\n" + control_indent;
                }

                if (had_previous) json_bindings_[binding_part] = std::move(previous_value);
                else json_bindings_.erase(binding_part);
                if (had_previous_loop) json_bindings_["loop"] = std::move(previous_loop_value);
                else json_bindings_.erase("loop");

                if (!result_.ok) break;
            } else if (collection->is_object()) {
                if (binding_part.size() < 5 ||
                    binding_part.front() != '(' || binding_part.back() != ')') {
                    fail(source_path, source, i,
                         "object @for syntax is @for((key, val) : object){...}");
                    break;
                }

                const std::string pair = binding_part.substr(1, binding_part.size() - 2);
                const auto comma = pair.find(',');
                if (comma == std::string::npos || pair.find(',', comma + 1) != std::string::npos) {
                    fail(source_path, source, i,
                         "object @for requires exactly two bindings: (key, val)");
                    break;
                }

                const std::string key_name = trim_copy(pair.substr(0, comma));
                const std::string value_name = trim_copy(pair.substr(comma + 1));
                if (!valid_binding_identifier(key_name) || !valid_binding_identifier(value_name) ||
                    key_name == value_name) {
                    fail(source_path, source, i,
                         "object @for key and value bindings must be distinct identifiers");
                    break;
                }
                if (reserved_binding_name(key_name) || reserved_binding_name(value_name)) {
                    fail(source_path, source, i,
                         "@for bindings cannot conflict with built-in metadata");
                    break;
                }
                if (host_.is_contract_name(key_name) || host_.is_contract_name(value_name)) {
                    fail(source_path, source, i,
                         "@for bindings cannot conflict with configured contract namespaces");
                    break;
                }

                const bool valid_object_sort_root = sort_expression.empty() ||
                    sort_expression == key_name || sort_expression == value_name ||
                    sort_expression.rfind(key_name + ".", 0) == 0 ||
                    sort_expression.rfind(key_name + "[", 0) == 0 ||
                    sort_expression.rfind(value_name + ".", 0) == 0 ||
                    sort_expression.rfind(value_name + "[", 0) == 0;
                if (!valid_object_sort_root) {
                    fail(source_path, source, i,
                         "@for object sort key must begin with key/value binding '" +
                         key_name + "' or '" + value_name + "'");
                    break;
                }

                const auto old_key_it = json_bindings_.find(key_name);
                const auto old_value_it = json_bindings_.find(value_name);
                const bool had_old_key = old_key_it != json_bindings_.end();
                const bool had_old_value = old_value_it != json_bindings_.end();
                std::shared_ptr<const json::Document> old_key;
                std::shared_ptr<const json::Document> old_value;
                if (had_old_key) old_key = old_key_it->second;
                if (had_old_value) old_value = old_value_it->second;
                const auto previous_loop = json_bindings_.find("loop");
                const bool had_previous_loop = previous_loop != json_bindings_.end();
                std::shared_ptr<const json::Document> previous_loop_value;
                if (had_previous_loop) previous_loop_value = previous_loop->second;

                std::vector<std::size_t> order(collection->object.size());
                for (std::size_t n = 0; n < order.size(); ++n) order[n] = n;
                std::vector<json::Document> sort_keys;
                if (!sort_expression.empty()) {
                    sort_keys.reserve(collection->object.size());
                    json::Type key_type = json::Type::Null;
                    bool have_key_type = false;
                    for (std::size_t n = 0; n < collection->object.size(); ++n) {
                        const auto& entry = collection->object[n];
                        json_bindings_[key_name] = std::make_shared<const json::Document>(entry.first);
                        json_bindings_[value_name] =
                            std::shared_ptr<const json::Document>(collection, &entry.second);
                        std::shared_ptr<const json::Document> key;
                        std::string key_error;
                        if (!resolve_json_value(sort_expression, key, key_error) || !key_error.empty()) {
                            fail(source_path, source, i, key_error.empty()
                                ? "@for sort key is not a bound JSON path: " + sort_expression
                                : key_error);
                            break;
                        }
                        if (!sortable_scalar(*key)) {
                            fail(source_path, source, i,
                                 "@for sort keys must all be numbers or all be strings: " + sort_expression);
                            break;
                        }
                        if (!have_key_type) { key_type = key->type; have_key_type = true; }
                        else if (key->type != key_type) {
                            fail(source_path, source, i,
                                 "@for sort keys must have the same type: " + sort_expression);
                            break;
                        }
                        sort_keys.push_back(*key);
                    }
                    if (!result_.ok) {
                        if (had_old_key) json_bindings_[key_name] = old_key; else json_bindings_.erase(key_name);
                        if (had_old_value) json_bindings_[value_name] = old_value; else json_bindings_.erase(value_name);
                        break;
                    }
                    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                        const int comparison = compare_sort_keys(sort_keys[a], sort_keys[b]);
                        return sort_descending ? comparison > 0 : comparison < 0;
                    });
                }

                for (std::size_t position = 0; position < order.size() && result_.ok; ++position) {
                    const auto& entry = collection->object[order[position]];
                    json_bindings_[key_name] = std::make_shared<const json::Document>(entry.first);
                    json_bindings_[value_name] =
                        std::shared_ptr<const json::Document>(collection, &entry.second);
                    json_bindings_["loop"] = make_loop_metadata(position, order.size());

                    push_json_scope();
                    variable_scopes_.back().emplace(key_name, VariableBinding{
                        std::make_shared<json::Document>(entry.first),
                        nift_binding_type(json::Document(entry.first)), true, false});
                    variable_scopes_.back().emplace(value_name, VariableBinding{
                        std::const_pointer_cast<json::Document>(
                            std::shared_ptr<const json::Document>(collection, &entry.second)),
                        nift_binding_type(entry.second), true, false});
                    ++loop_depth_;
                    const auto nested = parse(body.text, source_path, depth + 1);
                    --loop_depth_;
                    pop_json_scope();
                    if (!nested.ok) break;
                    append_indented(output, nested.output, control_indent, insertion_code_block_depth);
                    if (pending_control_.kind == ControlFlow::Continue) { pending_control_ = {}; continue; }
                    if (pending_control_.kind == ControlFlow::Break) { pending_control_ = {}; break; }
                    if (body.multiline && position + 1 < order.size())
                        output += "\n" + control_indent;
                }

                if (had_old_key) json_bindings_[key_name] = std::move(old_key);
                else json_bindings_.erase(key_name);
                if (had_old_value) json_bindings_[value_name] = std::move(old_value);
                else json_bindings_.erase(value_name);
                if (had_previous_loop) json_bindings_["loop"] = std::move(previous_loop_value);
                else json_bindings_.erase("loop");

                if (!result_.ok) break;
            } else {
                fail(source_path, source, i, "@for can only iterate over JSON arrays or objects");
                break;
            }

            i = block_close + 1;
            continue;
        }

        if (source[i] == '@' && i + 1 < source.size() && source[i + 1] >= 'a' && source[i + 1] <= 'z') {
            std::size_t name_end = i + 1;
            while (name_end < source.size() && source[name_end] >= 'a' && source[name_end] <= 'z') ++name_end;
            const std::string function = source.substr(i + 1, name_end - i - 1);
            std::size_t call_start = name_end;
            std::vector<std::string> parameters;
            std::vector<bool> parameter_quoted;
            bool has_parameters = false;
            bool parameters_ok = true;
            std::size_t end = call_start;

            if (call_start < source.size() && source[call_start] == '(') {
                has_parameters = true;
                std::size_t close = 0;
                if (!find_balanced(source, call_start, '(', ')', close)) { fail(source_path, source, i, function + ": malformed parameters"); break; }
                parameters = parse_parameters(source.substr(call_start + 1, close - call_start - 1), parameters_ok,
                                              function == "json" ? &parameter_quoted : nullptr);
                if (!parameters_ok) { fail(source_path, source, i, function + ": malformed parameters"); break; }
                end = close + 1;
                if (end < source.size() && source[end] == ';') ++end;
            }

            // Inside @fn bodies, @return(expr) captures the function result and
            // stops the current parse level; enclosing constructs unwind when
            // the pending return is set. Outside function bodies it remains a
            // passthrough so existing literal "@return(...)" text is preserved.
            if (function == "return" && function_call_depth_ > 0 && !in_fragment_body_) {
                if (!has_parameters) {
                    fail(source_path, source, i, "return requires an argument expression");
                    break;
                }
                std::size_t return_close = 0;
                if (!find_balanced(source, call_start, '(', ')', return_close)) {
                    fail(source_path, source, i, "return: malformed expression");
                    break;
                }
                const std::string return_expr = trim_copy(source.substr(call_start + 1, return_close - call_start - 1));
                json::Document return_value(nullptr);
                if (!return_expr.empty()) {
                    std::string return_error;
                    if (!evaluate_expression(return_expr, return_value, return_error)) {
                        fail(source_path, source, i, return_error.rfind("callable recursion depth exceeded",0)==0 ? return_error : "return: " + return_error);
                        break;
                    }
                }
                pending_control_.value = std::make_shared<json::Document>(std::move(return_value));
                pending_control_.kind = ControlFlow::Return;
                break;
            }

            // Parameterised functions are only calls when followed by (...).
            // This keeps prose such as "Partials & @input" literal while preserving
            // strict validation for actual calls such as @input().
            if (!has_parameters && function != "content") {
                if (name_end < source.size() && source[name_end] == '[') {
                    fail(source_path, source, i, function + ": expected parentheses for parameters");
                    break;
                }
                output += source.substr(i, name_end - i);
                i = name_end;
                continue;
            }

            const auto previous_newline = output.find_last_of('\n');
            const std::string current_line = previous_newline == std::string::npos ? output : output.substr(previous_newline + 1);
            std::string indent = current_line;
            for (char c : current_line) if (c != ' ' && c != '\t') { indent.assign(current_line.size(), ' '); break; }

            if (function == "content") {
                if (has_parameters && !parameters.empty()) { fail(source_path, source, i, "content: expected 0 parameters"); break; }
                if (!page_source_.has_value()) {
                    fail(source_path, source, i, "@content requires a page source; render with a page and template, or use @input for a partial");
                    break;
                }
                if (++result_.content_count > 1) {
                    fail(source_path, source, i, "@content may be executed exactly once for a templated tracked item");
                    break;
                }

                std::string content_source;
                fs::path content_identity;
                if (!page_source_->path.empty()) {
                    const fs::path content_path = fs::absolute(page_source_->path).lexically_normal();
                    if (std::find(input_stack_.begin(), input_stack_.end(), content_path) != input_stack_.end()) {
                        fail(source_path, source, i, "@content would result in an input loop through " + content_path.generic_string());
                        break;
                    }
                    content_identity = content_path;
                    // The read is the authority: missing/unreadable content
                    // yields nullptr -> the typed content error, with no
                    // separate readability probe (performance-regression repair).
                    const auto cached = host_.read_shared_source(content_path);
                    if (cached.status == nift::HostStatus::Error) { fail(source_path, source, i, cached.error); break; }
                    if (!cached.content) { fail(source_path, source, i, "content file is not readable"); break; }
                    content_source = frontmatter::parse_inline(*cached.content).body;
                    result_.dependencies.insert(page_source_->dependency.empty() ? host_.relative(content_path) : page_source_->dependency);
                } else {
                    content_identity = fs::path(page_source_->logical_name);
                    if (!content_identity.empty() &&
                        std::find(input_stack_.begin(), input_stack_.end(), content_identity) != input_stack_.end()) {
                        fail(source_path, source, i, "@content would result in an input loop through " + content_identity.generic_string());
                        break;
                    }
                    content_source = page_source_->text;
                }
                input_stack_.push_back(content_identity);
                const int insertion_code_block_depth = code_block_depth_;
                const auto nested = parse(content_source, content_identity, depth + 1);
                input_stack_.pop_back();

                if (!nested.ok) break;

                append_indented(output, nested.output, indent, insertion_code_block_depth);
                result_.content_used = true;
                i = end;
                continue;
            }

            if (function == "pathtopage") {
                if (!has_parameters || parameters.size() != 1) { fail(source_path, source, i, "pathtopage: expected 1 page number"); break; }
                const std::string raw_page = trim_copy(parameters[0]);
                const bool relative = !raw_page.empty() && (raw_page.front() == '+' || raw_page.front() == '-');
                const int relative_sign = !raw_page.empty() && raw_page.front() == '-' ? -1 : 1;
                const std::string page_argument = relative ? raw_page.substr(1) : raw_page;
                std::string resolved, interpolation_error;
                if (!interpolate_parameter(page_argument, resolved, interpolation_error)) { fail(source_path, source, i, "pathtopage: " + interpolation_error); break; }
                const std::string trimmed = trim_copy(resolved);
                char* endp = nullptr;
                const unsigned long long number = std::strtoull(trimmed.c_str(), &endp, 10);
                if (trimmed.empty() || !endp || *endp != '\0' || number == 0) { fail(source_path, source, i, "pathtopage: page/offset must be a positive integer"); break; }
                long long page = static_cast<long long>(number);
                if (relative) page = static_cast<long long>(pagination_current_) + relative_sign * static_cast<long long>(number);
                if (page < 1 || static_cast<unsigned long long>(page) > static_cast<unsigned long long>(pagination_total_)) {
                    fail(source_path, source, i, "pathtopage: resolved page must be between 1 and " + std::to_string(pagination_total_)); break;
                }
                output += path_to_page(static_cast<std::size_t>(page));
                if (!result_.ok) break;
                i = end;
                continue;
            }

            if (function == "filter" || function == "map" || function == "sort" || function == "slice" ||
                function == "find" || function == "some" || function == "every" || function == "distinct" || function == "reverse" ||
                function == "sum" || function == "prod" || function == "min" || function == "max" || function == "reduce") {
                if (!has_parameters) { fail(source_path, source, i, function + ": expected parameters"); break; }
                json::Document collection_result; std::string collection_error;
                const std::size_t call_end = (end > call_start && source[end - 1] == ';') ? end - 1 : end;
                const std::string call = "@" + function + source.substr(call_start, call_end - call_start);
                if (!evaluate_collection_value(call, collection_result, collection_error)) { fail(source_path, source, i, collection_error); break; }
                output += render_expression_value(collection_result); i = end; continue;
            }

            if (function == "substr") {
                if (!has_parameters || parameters.size() != 3) { fail(source_path, source, i, "substr: expected value, position and length"); break; }
                std::string text, interpolation_error;
                if (!interpolate_parameter(parameters[0], text, interpolation_error)) {
                    fail(source_path, source, i, "substr: " + interpolation_error); break;
                }
                auto parse_index = [&](const std::string& raw, const char* label, std::size_t& parsed) {
                    const std::string trimmed = trim_copy(raw);
                    if (trimmed.empty() || trimmed.front() == '-') {
                        fail(source_path, source, i, std::string("substr: ") + label + " must be a non-negative integer"); return false;
                    }
                    char* endp = nullptr;
                    const unsigned long long value = std::strtoull(trimmed.c_str(), &endp, 10);
                    if (!endp || *endp != '\0') {
                        fail(source_path, source, i, std::string("substr: ") + label + " must be a non-negative integer"); return false;
                    }
                    parsed = static_cast<std::size_t>(value);
                    return true;
                };
                std::size_t position = 0, length = 0;
                if (!parse_index(parameters[1], "position", position) || !parse_index(parameters[2], "length", length)) break;

                std::vector<std::size_t> offsets;
                offsets.reserve(text.size() + 1);
                for (std::size_t byte = 0; byte < text.size();) {
                    offsets.push_back(byte);
                    const unsigned char lead = static_cast<unsigned char>(text[byte]);
                    std::size_t width = 0;
                    if (lead < 0x80) width = 1;
                    else if ((lead & 0xE0) == 0xC0) width = 2;
                    else if ((lead & 0xF0) == 0xE0) width = 3;
                    else if ((lead & 0xF8) == 0xF0) width = 4;
                    else { fail(source_path, source, i, "substr: value contains invalid UTF-8"); break; }
                    if (byte + width > text.size()) { fail(source_path, source, i, "substr: value contains truncated UTF-8"); break; }
                    for (std::size_t c = 1; c < width; ++c) {
                        if ((static_cast<unsigned char>(text[byte + c]) & 0xC0) != 0x80) {
                            fail(source_path, source, i, "substr: value contains invalid UTF-8 continuation byte"); break;
                        }
                    }
                    if (!result_.ok) break;
                    byte += width;
                }
                if (!result_.ok) break;
                offsets.push_back(text.size());
                const std::size_t count = offsets.size() - 1;
                if (position < count && length > 0) {
                    const std::size_t finish = std::min(count, position + std::min(length, count - position));
                    output.append(text, offsets[position], offsets[finish] - offsets[position]);
                }
                i = end;
                continue;
            }

            if (function == "join") {
                if (!has_parameters || parameters.size() != 2) { fail(source_path, source, i, "join: expected array and separator"); break; }
                std::string expression = trim_copy(parameters[0]);
                if (expression.size() >= 3 && expression.rfind("$[", 0) == 0 && expression.back() == ']') {
                    expression = expression.substr(2, expression.size() - 3);
                }
                json::Document array_value;
                std::string join_error;
                if (!evaluate_collection_value(expression, array_value, join_error)) {
                    fail(source_path, source, i, "join: " + join_error); break;
                }
                auto array = std::make_shared<const json::Document>(std::move(array_value));
                if (!array->is_array()) { fail(source_path, source, i, "join: first parameter must resolve to a JSON array"); break; }
                std::string separator, interpolation_error;
                if (!interpolate_parameter(parameters[1], separator, interpolation_error)) {
                    fail(source_path, source, i, "join: " + interpolation_error); break;
                }
                for (std::size_t item_index = 0; item_index < array->array.size(); ++item_index) {
                    const auto& item = array->array[item_index];
                    if (item.is_array() || item.is_object()) {
                        fail(source_path, source, i, "join: array items must be scalar JSON values");
                        break;
                    }
                    if (item_index) output += separator;
                    output += item.is_string() ? item.string : item.dump(0);
                }
                if (!result_.ok) break;
                i = end;
                continue;
            }

            if (function == "input") {
                if (!has_parameters || parameters.size() != 1) { fail(source_path, source, i, "input: expected 1 parameter"); break; }
                std::string resolved, interpolation_error;
                if (!interpolate_parameter(parameters[0], resolved, interpolation_error)) {
                    fail(source_path, source, i, "input: " + interpolation_error); break;
                }
                parameters[0] = std::move(resolved);
                fs::path input_path = parameters[0];
                if (input_path.is_relative()) {
                    // Only resolve against the current source's directory when
                    // that source actually has one (filesystem sources always
                    // do; in-memory sources only if the caller supplied a
                    // logical name with a directory). A bare in-memory source
                    // must never fall back to the process working directory.
                    const fs::path relative_to_source = source_path.parent_path() / input_path;
                    if (!source_path.parent_path().empty() && host_.source_exists(relative_to_source)) {
                        input_path = relative_to_source;
                    } else if (host_.root().empty()) {
                        fail(source_path, source, i, "@input cannot resolve relative path '" + parameters[0] + "' without a project root");
                        break;
                    } else {
                        input_path = host_.root() / input_path;
                    }
                }
                if (!host_.source_exists(input_path)) { fail(source_path, source, i, "@input path does not exist: " + parameters[0]); break; }
                input_path = fs::absolute(input_path).lexically_normal();
                if (std::find(input_stack_.begin(), input_stack_.end(), input_path) != input_stack_.end()) {
                    fail(source_path, source, i, "@input would result in an input loop through " + input_path.generic_string());
                    break;
                }
                input_stack_.push_back(input_path);
                result_.dependencies.insert(host_.relative(input_path));
                const int insertion_code_block_depth = code_block_depth_;
                // The read is the authority (no separate readability probe).
                const auto input_source = host_.read_shared_source(input_path);
                if (input_source.status == nift::HostStatus::Error) { fail(source_path, source, i, input_source.error); break; }
                if (!input_source.content) { fail(source_path, source, i, "input file is not readable"); break; }
                push_variable_scope();
                const auto nested = parse(*input_source.content, input_path, depth + 1);
                pop_variable_scope();
                input_stack_.pop_back();
                if (!nested.ok) break;
                append_indented(output, nested.output, indent, insertion_code_block_depth);
                i = end;
                continue;
            }

            if (function == "path" || function == "pathto" || function == "pathtofile") {
                if (!has_parameters || parameters.size() != 1) { fail(source_path, source, i, "@" + function + " expects exactly one path/name"); break; }
                std::string resolved, interpolation_error;
                if (!interpolate_parameter(parameters[0], resolved, interpolation_error)) {
                    fail(source_path, source, i, function + ": " + interpolation_error); break;
                }
                parameters[0] = std::move(resolved);

                if (!host_.tracked_output_path(parameters[0])) {
                    const fs::path target_path = (host_.root() / parameters[0]).lexically_normal();
                    if (!filesystem::path_within(host_.root(), target_path)) {
                        fail(source_path, source, i, "@" + function + " path must stay inside the Nift project: " + parameters[0]);
                        break;
                    }
                }

                output += path_to(parameters[0], function);
                if (!result_.ok) { if (result_.error.source_file.empty()) fail(source_path, source, i, result_.error.message); break; }
                fs::path requirement;
                if (const auto target = host_.tracked_output_path(parameters[0])) requirement = target->path;
                else requirement = host_.root() / parameters[0];
                result_.reqs.insert(host_.relative(requirement.lexically_normal()));
                i = end;
                continue;
            }

            if (function == "getenv") {
                if (!has_parameters || parameters.size() != 1) { fail(source_path, source, i, "getenv: expected 1 parameter"); break; }
                std::string resolved, interpolation_error;
                if (!interpolate_parameter(parameters[0], resolved, interpolation_error)) {
                    fail(source_path, source, i, "getenv: " + interpolation_error); break;
                }
                parameters[0] = std::move(resolved);
                nift::HostResult env = host_.environment(parameters[0]);
                if (env.status == nift::HostStatus::Found) output += env.value;
                else if (env.status == nift::HostStatus::Error) {
                    fail(source_path, source, i, "getenv: " + env.error);
                    break;
                }
                i = end;
                continue;
            }

            if (function == "ent") {
                if (!has_parameters || parameters.size() != 1) { fail(source_path, source, i, "@ent expects exactly one entity"); break; }
                std::string resolved, interpolation_error;
                if (!interpolate_parameter(parameters[0], resolved, interpolation_error)) {
                    fail(source_path, source, i, "ent: " + interpolation_error); break;
                }
                parameters[0] = std::move(resolved);
                bool known = false;
                output += entity(parameters[0], known);
                if (!known) { fail(source_path, source, i, "do not currently have an entity value for '" + parameters[0] + "'"); break; }
                i = end;
                continue;
            }

            if (function == "markup") {
                std::size_t block_open = end;
                while (block_open < source.size() &&
                       std::isspace(static_cast<unsigned char>(source[block_open]))) ++block_open;
                const bool inline_source = block_open < source.size() && source[block_open] == '{';
                if (!has_parameters || (inline_source ? parameters.size() != 1 : parameters.size() != 2)) {
                    fail(source_path, source, i,
                         inline_source ? "markup: inline syntax is @markup(format){...}"
                                       : "markup: file syntax is @markup(format, path)");
                    break;
                }

                std::string format_name, interpolation_error;
                if (!interpolate_parameter(parameters[0], format_name, interpolation_error)) {
                    fail(source_path, source, i, "markup: " + interpolation_error);
                    break;
                }
                markup::Format format;
                const std::string normalized_format = trim_copy(format_name);
                if (normalized_format == "md" || normalized_format == "markdown") format = markup::Format::Markdown;
                else if (normalized_format == "adoc" || normalized_format == "asciidoc") format = markup::Format::AsciiDoc;
                else if (normalized_format == "rst" || normalized_format == "restructuredtext") format = markup::Format::ReStructuredText;
                else {
                    fail(source_path, source, i, "markup: unknown format '" + normalized_format +
                         "' (expected md, adoc or rst)");
                    break;
                }

                std::string markup_source;
                fs::path markup_identity = source_path;
                std::size_t directive_end = end;
                if (inline_source) {
                    std::size_t block_close = 0;
                    if (!find_balanced(source, block_open, '{', '}', block_close)) {
                        fail(source_path, source, block_open, "@markup block has no matching '}'");
                        break;
                    }
                    const auto body = normalize_control_block_body(
                        source.substr(block_open + 1, block_close - block_open - 1));
                    const auto templated = parse(body.text, source_path, depth + 1);
                    if (!templated.ok) break;
                    markup_source = templated.output;
                    directive_end = block_close + 1;
                } else {
                    std::string resolved_path;
                    if (!interpolate_parameter(parameters[1], resolved_path, interpolation_error)) {
                        fail(source_path, source, i, "markup: " + interpolation_error);
                        break;
                    }
                    fs::path candidate = resolved_path;
                    if (candidate.is_relative()) candidate = host_.root() / candidate;
                    candidate = fs::absolute(candidate).lexically_normal();
                    if (!filesystem::path_within(host_.root().lexically_normal(), candidate)) {
                        fail(source_path, source, i,
                             "markup: path must stay inside the Nift project: " + resolved_path);
                        break;
                    }
                    if (!host_.source_exists(candidate)) {
                        fail(source_path, source, i, "markup: file does not exist: " + resolved_path);
                        break;
                    }
                    if (std::find(input_stack_.begin(), input_stack_.end(), candidate) != input_stack_.end()) {
                        fail(source_path, source, i,
                             "@markup would result in an input loop through " + candidate.generic_string());
                        break;
                    }
                    const auto loaded = host_.read_shared_source(candidate);
                    if (loaded.status == nift::HostStatus::Error) {
                        fail(source_path, source, i, "markup: " + loaded.error);
                        break;
                    }
                    if (!loaded.content) {
                        fail(source_path, source, i, "markup: file is not readable: " + resolved_path);
                        break;
                    }
                    result_.dependencies.insert(host_.relative(candidate));
                    input_stack_.push_back(candidate);
                    const auto templated = parse(*loaded.content, candidate, depth + 1);
                    input_stack_.pop_back();
                    if (!templated.ok) break;
                    markup_source = templated.output;
                    markup_identity = candidate;
                }

                markup::Options options;
                options.allow_raw_html = true;
                options.asciidoc_source_identity = markup_identity.generic_string();
                options.rst_source_identity = markup_identity.generic_string();
                auto resolve_resource = [&](const std::string& including,
                                            const std::string& requested,
                                            std::string& content,
                                            std::string& canonical,
                                            std::string& resolver_error) {
                    fs::path base = fs::path(including).parent_path();
                    fs::path candidate = fs::path(requested);
                    if (candidate.is_relative()) candidate = base / candidate;
                    candidate = fs::absolute(candidate).lexically_normal();
                    if (!filesystem::path_within(host_.root().lexically_normal(), candidate)) {
                        resolver_error = "markup include must stay inside the Nift project: " + requested;
                        return false;
                    }
                    if (!host_.source_exists(candidate)) {
                        resolver_error = "markup include does not exist: " + requested;
                        return false;
                    }
                    const auto loaded = host_.read_shared_source(candidate);
                    if (loaded.status == nift::HostStatus::Error) {
                        resolver_error = loaded.error;
                        return false;
                    }
                    if (!loaded.content) {
                        resolver_error = "markup include is not readable: " + requested;
                        return false;
                    }
                    if (std::find(input_stack_.begin(), input_stack_.end(), candidate) != input_stack_.end()) {
                        resolver_error = "markup include would form a cycle through " + candidate.generic_string();
                        return false;
                    }
                    result_.dependencies.insert(host_.relative(candidate));
                    input_stack_.push_back(candidate);
                    const auto templated = parse(*loaded.content, candidate, depth + 1);
                    input_stack_.pop_back();
                    if (!templated.ok) {
                        resolver_error = result_.error.message;
                        return false;
                    }
                    content = templated.output;
                    canonical = candidate.generic_string();
                    return true;
                };
                options.asciidoc_include_resolver = resolve_resource;
                options.rst_resource_resolver = resolve_resource;
                options.asciidoc_dependency = [&](const std::string& identity) {
                    result_.dependencies.insert(host_.relative(fs::path(identity)));
                };
                options.rst_dependency = options.asciidoc_dependency;
                std::vector<std::string> diagnostics;
                options.asciidoc_diagnostic = [&](const std::string& message) { diagnostics.push_back(message); };
                options.rst_diagnostic = options.asciidoc_diagnostic;

                std::string html, conversion_error;
                if (!markup::convert(format, markup_source, html, conversion_error, options)) {
                    fail(source_path, source, i, "markup: " + conversion_error);
                    break;
                }
                if (!diagnostics.empty()) {
                    fail(source_path, source, i, "markup: " + diagnostics.front());
                    break;
                }
                append_indented(output, html, indent, code_block_depth_);
                i = directive_end;
                continue;
            }

            if (function == "json") {
                std::size_t block_open = end;
                while (block_open < source.size() &&
                       std::isspace(static_cast<unsigned char>(source[block_open]))) ++block_open;
                const bool inline_json = block_open < source.size() && source[block_open] == '{';
                const bool arity_ok = inline_json
                    ? (parameters.size() == 1 || parameters.size() == 2)
                    : (parameters.size() == 2 || parameters.size() == 3);
                if (!has_parameters || !arity_ok) {
                    fail(source_path, source, i,
                         "json: expected @json(name, path), @json(name, schema, path), "
                         "@json(name){...} or @json(name, schema){...}");
                    break;
                }

                std::string binding_name = trim_copy(parameters[0]);
                if (!valid_binding_identifier(binding_name)) {
                    fail(source_path, source, i,
                         "json: name must be an identifier using letters, digits and underscores");
                    break;
                }
                if (reserved_binding_name(binding_name)) {
                    fail(source_path, source, i, "json: name '" + binding_name +
                         "' conflicts with built-in metadata/reserved bindings");
                    break;
                }
                if (host_.is_contract_name(binding_name)) {
                    fail(source_path, source, i, "json: name '" + binding_name +
                         "' conflicts with configured contract namespace");
                    break;
                }
                if (host_.binding(binding_name) || json_bindings_.count(binding_name)) {
                    fail(source_path, source, i, "json: name '" + binding_name + "' is already bound");
                    break;
                }

                std::shared_ptr<const json::Document> document;
                std::string instance_label = "inline JSON";
                std::size_t directive_end = end;
                std::string interpolation_error;
                if (inline_json) {
                    std::size_t block_close = 0;
                    if (!find_balanced(source, block_open, '{', '}', block_close)) {
                        fail(source_path, source, block_open, "@json block has no matching '}'");
                        break;
                    }
                    const auto body = normalize_control_block_body(
                        source.substr(block_open + 1, block_close - block_open - 1));
                    const auto templated = parse(body.text, source_path, depth + 1);
                    if (!templated.ok) break;
                    json::Document parsed;
                    std::string parse_error;
                    if (!nift_json::parse(templated.output, parsed, parse_error)) {
                        fail(source_path, source, i, "json: failed to parse inline JSON (" + parse_error + ")");
                        break;
                    }
                    document = std::make_shared<const json::Document>(std::move(parsed));
                    directive_end = block_close + 1;
                } else {
                    const std::size_t path_index = parameters.size() - 1;
                    std::string json_path_argument;
                    if (!interpolate_parameter(parameters[path_index], json_path_argument, interpolation_error)) {
                        fail(source_path, source, i, "json: " + interpolation_error);
                        break;
                    }
                    const fs::path host_root = host_.root().lexically_normal();
                    const fs::path json_path = (host_.root() / json_path_argument).lexically_normal();
                    if (!filesystem::path_within(host_root, json_path)) {
                        fail(source_path, source, i,
                             "json: path must stay inside the Nift project: " + json_path_argument);
                        break;
                    }
                    if (!host_.source_exists(json_path)) {
                        fail(source_path, source, i, "json: file does not exist: " + json_path_argument);
                        break;
                    }
                    std::string parse_error;
                    document = host_.read_shared_json(json_path, parse_error);
                    if (!document) {
                        fail(source_path, source, i, "json: failed to parse " + json_path_argument +
                             (parse_error.empty() ? "" : " (" + parse_error + ")"));
                        break;
                    }
                    instance_label = json_path_argument;
                    result_.dependencies.insert(host_.relative(json_path));
                }

                const bool has_schema = inline_json ? parameters.size() == 2 : parameters.size() == 3;
                if (has_schema) {
                    const std::size_t schema_index = 1;
                    const bool schema_was_quoted = parameter_quoted.size() > schema_index &&
                                                   parameter_quoted[schema_index];
                    const std::string schema_reference = trim_copy(parameters[schema_index]);
                    std::shared_ptr<const json::Document> schema;
                    std::string schema_label;
                    if (!schema_was_quoted) {
                        // A bare identifier in the schema position is a schema
                        // binding name and must already exist; it must never
                        // fall back to a same-named file.
                        if (!valid_binding_identifier(schema_reference)) {
                            fail(source_path, source, i,
                                 "json: schema name '" + schema_reference +
                                 "' must be an existing binding name or a quoted schema path");
                            break;
                        }
                        std::string binding_error;
                        if (!resolve_json_value(schema_reference, schema, binding_error)) {
                            fail(source_path, source, i,
                                 "json: schema name '" + schema_reference +
                                 "' is not bound (quote the argument to use a schema path)");
                            break;
                        }
                        if (!binding_error.empty()) {
                            fail(source_path, source, i, "json: " + binding_error);
                            break;
                        }
                        schema_label = schema_reference;
                    } else {
                        std::string schema_path_argument;
                        if (!interpolate_parameter(parameters[schema_index], schema_path_argument,
                                                   interpolation_error)) {
                            fail(source_path, source, i, "json: " + interpolation_error);
                            break;
                        }
                        const fs::path host_root = host_.root().lexically_normal();
                        const fs::path schema_path = (host_.root() / schema_path_argument).lexically_normal();
                        if (!filesystem::path_within(host_root, schema_path)) {
                            fail(source_path, source, i,
                                 "json: schema path must stay inside the Nift project: " + schema_path_argument);
                            break;
                        }
                        if (!host_.source_exists(schema_path)) {
                            fail(source_path, source, i,
                                 "json: schema file does not exist: " + schema_path_argument);
                            break;
                        }
                        std::string schema_parse_error;
                        schema = host_.read_shared_json(schema_path, schema_parse_error);
                        if (!schema) {
                            fail(source_path, source, i, "json: failed to parse schema " + schema_path_argument +
                                 (schema_parse_error.empty() ? "" : " (" + schema_parse_error + ")"));
                            break;
                        }
                        schema_label = schema_path_argument;
                        result_.dependencies.insert(host_.relative(schema_path));
                    }
                    std::string validation_error;
                    if (!jsonschema::validate(*document, *schema, validation_error)) {
                        fail(source_path, source, i, "json: " + instance_label+
                             " does not satisfy schema " + schema_label + " (" + validation_error + ")");
                        break;
                    }
                }

                json_bindings_.emplace(binding_name, std::move(document));
                if (!json_binding_scopes_.empty()) json_binding_scopes_.back().push_back(binding_name);
                i = directive_end;
                continue;
            }

            if (function == "dep") {
                if (!has_parameters || parameters.empty()) { fail(source_path, source, i, "dep: expected parameters"); break; }
                for (auto& dependency : parameters) {
                    std::string resolved, interpolation_error;
                    if (!interpolate_parameter(dependency, resolved, interpolation_error)) {
                        fail(source_path, source, i, "dep: " + interpolation_error);
                        break;
                    }
                    dependency = std::move(resolved);
                    const fs::path dependency_path = (host_.root() / dependency).lexically_normal();
                    if (!filesystem::path_within(host_.root(), dependency_path)) {
                        fail(source_path, source, i, "dep: path must stay inside the Nift project: " + dependency);
                        break;
                    }
                    if (!host_.source_exists(dependency_path)) {
                        fail(source_path, source, i, "failed as dependency does not exist: " + dependency);
                        break;
                    }
                    result_.dependencies.insert(host_.relative(dependency_path));
                }
                if (!result_.ok) break;
                i = end;
                continue;
            }

            output += source.substr(i, end - i);
            i = end;
            continue;
        }

        if (source[i] == '\\' || source[i] == '@' || source[i] == '<' || source[i] == '$') {
            output.push_back(source[i++]);
        } else {
            const std::size_t next_special = source.find_first_of("\\@<$", i);
            const std::size_t end = next_special == std::string::npos ? source.size() : next_special;
            output.append(source, i, end - i);
            i = end;
        }
    }

    if (result_.ok && code_block_depth_ > base_code_block_depth) {
        fail(source_path, source, open_code_offset, "<pre*> open tag has no following </pre> close tag");
        code_block_depth_ = base_code_block_depth;
    }

    result_.output = output;
    return result_;
}

RenderResult Parser::render_composed(const RenderSource& template_source,
                                     const std::optional<RenderSource>& page_source,
                                     bool require_exactly_one_content) {
    std::string template_content;
    fs::path template_identity;
    if (!template_source.path.empty()) {
        template_identity = fs::absolute(template_source.path).lexically_normal();
        const auto cached = host_.read_shared_source(template_identity);
        if (cached.status == nift::HostStatus::Error) {
            result_.ok = false;
            result_.error = {tracked_info_.name, template_identity, 0, cached.error};
            return result_;
        }
        if (!cached.content) {
            result_.ok = false;
            result_.error = {tracked_info_.name, template_identity, 0, "template file is not readable"};
            return result_;
        }
        template_content = *cached.content;
    } else {
        template_identity = fs::path(template_source.logical_name);
        template_content = template_source.text;
    }

    page_source_ = page_source;
    input_stack_.push_back(template_identity);
    if (!template_source.dependency.empty()) result_.dependencies.insert(template_source.dependency);
    auto result = parse(template_content, template_identity, 0);
    input_stack_.pop_back();

    if (result.ok && require_exactly_one_content && result.content_count != 1) {
        result.ok = false;
        result.error = {tracked_info_.name, template_identity, 0, "templated tracked items must execute exactly one @content; add @content through the template/input graph or omit the tracked template field"};
    }
    return result;
}

RenderResult Parser::render() {
    if (auto it=json_bindings_.find("frontmatter"); it!=json_bindings_.end() && it->second && it->second->is_object() && it->second->has("_error")) { result_.ok=false; result_.error={tracked_info_.name,host_.content_path(tracked_info_),0,(*it->second)["_error"].string}; return result_; }
    const fs::path content_path = host_.content_path(tracked_info_);
    if (tracked_info_.template_path.empty()) {
        // The read is the authority: missing/unreadable content yields nullptr
        // -> the typed content error, with no separate readability probe.
        const auto content_source = host_.read_shared_source(content_path);
        if (content_source.status == nift::HostStatus::Error) {
            result_.ok = false;
            result_.error = {tracked_info_.name, content_path, 0, content_source.error};
            return result_;
        }
        if (!content_source.content) {
            result_.ok = false;
            result_.error = {tracked_info_.name, content_path, 0, "content file is not readable"};
            return result_;
        }
        auto fm = frontmatter::parse_inline(*content_source.content);
        if (!fm.error.empty()) { result_.ok=false; result_.error={tracked_info_.name,content_path,0,fm.error}; return result_; }
        if (tracked_info_.frontmatter && fm.present) { result_.ok=false; result_.error={tracked_info_.name,content_path,0,"multiple front matter sources are not allowed"}; return result_; }
        auto result = parse(fm.body, content_path, 0);
        result.content_used = true;
        result.dependencies.insert(host_.relative(content_path));
        return result;
    }

    const fs::path template_path = host_.root() / tracked_info_.template_path;
    // One existence probe distinguishes "does not exist" from the read's
    // "not readable"; render_composed's read classifies the latter.
    if (!host_.source_exists(template_path)) {
        result_.ok = false;
        result_.error = {tracked_info_.name, template_path, 0, "template file does not exist"};
        return result_;
    }

    RenderSource template_source;
    template_source.path = template_path;
    template_source.dependency = tracked_info_.template_path;
    RenderSource page_render_source;
    page_render_source.path = content_path;
    page_render_source.dependency = host_.relative(content_path);

    pagination_collecting_ = tracked_info_.paginate.has_value();
    auto result = render_composed(template_source, page_render_source, /*require_exactly_one_content=*/true);
    if (!result.ok || !tracked_info_.paginate.has_value()) return result;
    if (result.paginate_count != 1) {
        result.ok = false;
        result.error = {tracked_info_.name, content_path, 0, "paginated tracked items must execute exactly one @paginate"};
        return result;
    }

    const auto& config = *tracked_info_.paginate;
    auto conventional = [&](const char* suffix) {
        return content_path.parent_path() / (content_path.stem().generic_string() + suffix + ".html");
    };
    fs::path pagination_template = config.template_path.has_value()
        ? (host_.root() / *config.template_path).lexically_normal()
        : conventional(".paginate");
    if (!filesystem::path_within(host_.root(), pagination_template) || !host_.source_readable(pagination_template)) {
        result.ok = false;
        result.error = {tracked_info_.name, pagination_template, 0, "pagination template is missing, unreadable, or outside the project"};
        return result;
    }
    fs::path separator;
    if (config.separator_path.has_value()) separator = (host_.root() / *config.separator_path).lexically_normal();
    else {
        const fs::path candidate = conventional(".separator");
        if (host_.source_exists(candidate)) separator = candidate;
    }
    if (!separator.empty() && (!filesystem::path_within(host_.root(), separator) || !host_.source_readable(separator))) {
        result.ok = false;
        result.error = {tracked_info_.name, separator, 0, "pagination separator is unreadable or outside the project"};
        return result;
    }

    result_.dependencies.insert(host_.relative(pagination_template));
    if (!separator.empty()) result_.dependencies.insert(host_.relative(separator));
    const std::string base_output = result.output;
    const std::string marker = "\x1dNIFT_PAGINATE\x1d";
    const std::size_t marker_position = base_output.find(marker);
    const std::size_t total = std::max<std::size_t>(1, (result_.pagination_items.size() + config.items_per_page - 1) / config.items_per_page);
    struct PageRender {
        bool ok = false;
        std::string output;
        BuildError error;
        std::set<std::string> dependencies;
        std::set<std::string> reqs;
    };
    std::vector<PageRender> pages(total);
    const auto page_source = host_.read_shared_source(pagination_template);
    if (page_source.status == nift::HostStatus::Error) {
        result_.ok = false;
        result_.error = {tracked_info_.name, pagination_template, 0, page_source.error};
        return result_;
    }
    if (!page_source.content) {
        result_.ok = false;
        result_.error = {tracked_info_.name, pagination_template, 0, "pagination template is missing, unreadable, or outside the project"};
        return result_;
    }
    // The separator is OPTIONAL only in its absence: NotFound means "no
    // separator", but a host ERROR must fail the render with its diagnostic
    // (Error != NotFound). Found with an empty value is a valid empty separator.
    const std::string* separator_source = nullptr;
    if (!separator.empty()) {
        const auto separator_read = host_.read_shared_source(separator);
        if (separator_read.status == nift::HostStatus::Error) {
            result_.ok = false;
            result_.error = {tracked_info_.name, separator, 0, separator_read.error};
            return result_;
        }
        separator_source = separator_read.content;
    }

    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    std::size_t thread_count = tracked_info_.paginate.has_value()
        ? (host_.build_threads() < 0
            ? static_cast<std::size_t>(-static_cast<long long>(host_.build_threads())) * hardware
            : (host_.build_threads() == 0 ? hardware : static_cast<std::size_t>(host_.build_threads())))
        : 1;
    thread_count = std::max<std::size_t>(1, std::min(thread_count, total));
    std::atomic<std::size_t> next_page{0};

    auto render_page = [&] {
        while (true) {
            const std::size_t page_index = next_page.fetch_add(1);
            if (page_index >= total) break;
            const std::size_t page = page_index + 1;
            Parser page_parser(host_, tracked_info_);
            page_parser.json_bindings_ = json_bindings_;
            page_parser.contract_bindings_ = contract_bindings_;
            page_parser.page_source_ = page_source_;
            page_parser.pagination_collecting_ = false;
            page_parser.pagination_context_active_ = true;
            page_parser.pagination_current_ = page;
            page_parser.pagination_total_ = total;
            page_parser.pagination_current_output_ = host_.pagination_output_path(tracked_info_, page);
            page_parser.result_.content_count = result_.content_count;

            const std::size_t begin = page_index * config.items_per_page;
            const std::size_t finish = std::min(result_.pagination_items.size(), begin + config.items_per_page);
            std::string separator_text;
            if (separator_source && finish > begin + 1) {
                const auto rendered_separator = page_parser.parse(*separator_source, separator, 0);
                if (!rendered_separator.ok) {
                    pages[page_index].error = rendered_separator.error;
                    continue;
                }
                separator_text = rendered_separator.output;
            }
            std::string items;
            for (std::size_t index = begin; index < finish; ++index) {
                if (index != begin) items += separator_text;
                items += result_.pagination_items[index];
            }
            page_parser.pagination_items_text_ = std::move(items);
            const auto rendered_page = page_parser.parse(*page_source.content, pagination_template, 0);
            if (!rendered_page.ok) {
                pages[page_index].error = rendered_page.error;
                continue;
            }
            std::string page_output = base_output;
            page_output.replace(marker_position, marker.size(), rendered_page.output);
            pages[page_index].ok = true;
            pages[page_index].output = std::move(page_output);
            pages[page_index].dependencies = rendered_page.dependencies;
            pages[page_index].reqs = rendered_page.reqs;
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (std::size_t worker = 0; worker < thread_count; ++worker) workers.emplace_back(render_page);
    for (auto& worker : workers) worker.join();

    result_.pagination_outputs.clear();
    result_.pagination_outputs.reserve(total);
    for (std::size_t page_index = 0; page_index < total; ++page_index) {
        if (!pages[page_index].ok) {
            result_.ok = false;
            result_.error = pages[page_index].error;
            return result_;
        }
        result_.dependencies.insert(pages[page_index].dependencies.begin(), pages[page_index].dependencies.end());
        result_.reqs.insert(pages[page_index].reqs.begin(), pages[page_index].reqs.end());
        result_.pagination_outputs.push_back(std::move(pages[page_index].output));
    }
    result_.output = result_.pagination_outputs.front();
    return result_;
}

bool Parser::invoke_struct_method(std::shared_ptr<StructInstance> instance,
                                  const StructMethod& method,
                                  const std::vector<json::Document>& args,
                                  const std::vector<std::string>& arg_sources,
                                  json::Document& out,
                                  std::string& error) {
    if (args.size() != method.callable.params.size()) { error = "struct method argument count mismatch"; return false; }
    if (callable_call_depth_ >= kMaxCallableDepth) { error = "callable recursion depth exceeded"; return false; }
    ++callable_call_depth_;
    const int caller_loop_depth = loop_depth_; loop_depth_ = 0;
    push_variable_scope(); const std::size_t scope_index=variable_scopes_.size()-1;
    for(std::size_t i=0;i<args.size();++i){auto sp=std::make_shared<json::Document>(args[i]);variable_scopes_[scope_index][method.callable.params[i]]=VariableBinding{sp,nift_binding_type_from_text(arg_sources[i],*sp),true,false};}
    std::string id;
    for(const auto& e:struct_instances_) if(e.second==instance){id=e.first;break;}
    auto thisv=std::make_shared<json::Document>(std::string("\x1fnift:struct:")+id); variable_scopes_[scope_index]["this"]=VariableBinding{thisv,nift_binding_type(*thisv),false,false};
    receiver_stack_.push_back(instance); ++function_call_depth_; pending_control_={};
    RenderResult rr=execute_native_program(method.callable.body,method.callable.source_path,1);
    --function_call_depth_; receiver_stack_.pop_back();
    pop_variable_scope(); loop_depth_=caller_loop_depth; --callable_call_depth_;
    if(!rr.ok){error=rr.error.message;pending_control_={};return false;}
    if(method.constructor && pending_control_.kind==ControlFlow::Return && pending_control_.value && !pending_control_.value->is_null()){error="constructor cannot return a value";pending_control_={};return false;}
    out=pending_control_.value?*pending_control_.value:json::Document(nullptr); pending_control_={}; return true;
}
