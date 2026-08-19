#include "Parser.h"
#include "FileSystem.h"
#include "Json.h"
#include "JsonSchema.h"
#include "ProjectInfo.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <unordered_set>
#include <vector>
#include <functional>

namespace fs = std::filesystem;

namespace {
std::vector<std::string> parse_parameters(const std::string& text, bool& ok) {
    std::vector<std::string> result;
    std::string current;
    bool quoted = false;
    char quote = 0;
    std::size_t significant_end = 0;
    ok = true;

    auto append_parameter = [&] {
        current.resize(significant_end);
        result.push_back(current);
        current.clear();
        significant_end = 0;
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '\\' && i + 1 < text.size()) {
                const char escaped = text[++i];
                if (escaped == '$') current += '\\';
                current += escaped;
                significant_end = current.size();
            } else if (c == quote) {
                quoted = false;
            } else {
                current += c;
                significant_end = current.size();
            }
        } else if (c == '\'' || c == '"') {
            quoted = true;
            quote = c;
        } else if (c == ',') {
            append_parameter();
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) current += c; // retained only if later content makes it significant
        } else {
            current += c;
            significant_end = current.size();
        }
    }

    if (quoted) { ok = false; return {}; }
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
    std::istringstream stream(clause);
    std::vector<std::string> words;
    std::string word;
    while (stream >> word) words.push_back(word);
    if (words.size() == 1) {
        collection_expression = words[0];
        sort_expression.clear();
        descending = false;
        return true;
    }
    if (words.size() == 4 && words[1] == "by" &&
        (words[3] == "asc" || words[3] == "desc")) {
        collection_expression = words[0];
        sort_expression = words[2];
        descending = words[3] == "desc";
        return true;
    }
    error = "@for sorting syntax is @for(item : collection by item.field asc|desc){...}";
    return false;
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

Parser::Parser(ProjectInfo& project, TrackedInfo& tracked_info)
    : project_(project), tracked_info_(tracked_info) {}

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
    if (key == "content-path") return project_.relative(project_.content_path(tracked_info_));
    if (key == "output-path") return project_.relative(project_.output_path(tracked_info_));
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



void Parser::push_json_scope() {
    json_binding_scopes_.emplace_back();
}

void Parser::pop_json_scope() {
    if (json_binding_scopes_.empty()) return;
    for (const auto& name : json_binding_scopes_.back()) json_bindings_.erase(name);
    json_binding_scopes_.pop_back();
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

        // Comment bodies are not template structure. In particular, a brace in
        // a source/HTML comment must not terminate a surrounding @if/@for block.
        if (source.compare(i, 4, "<#--") == 0) {
            const auto comment_end = source.find("--#>", i + 4);
            if (comment_end == std::string::npos) return false;
            i = comment_end + 3;
            continue;
        }
        if (source.compare(i, 4, "<!--") == 0) {
            const auto comment_end = source.find("-->", i + 4);
            if (comment_end == std::string::npos) return false;
            i = comment_end + 2;
            continue;
        }
        if (source.compare(i, 2, "@#") == 0 || source.compare(i, 3, "@//") == 0) {
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
    const auto binding = json_bindings_.find(root_name);
    if (binding != json_bindings_.end()) {
        current = binding->second;
    } else {
        const auto contract = project_.config.contracts.find(root_name);
        if (contract == project_.config.contracts.end()) return false;
        contract_binding = true;

        const std::string& contract_path_argument = contract->second;
        const fs::path contract_path = (project_.root / contract_path_argument).lexically_normal();
        if (!filesystem::path_within(project_.root, contract_path)) {
            error = "contract '" + root_name + "': path must stay inside the Nift project: " +
                    contract_path_argument;
            return true;
        }
        if (!filesystem::path_exists(contract_path)) {
            error = "contract '" + root_name + "': file does not exist: " + contract_path_argument;
            return true;
        }

        const auto cached = contract_bindings_.find(root_name);
        if (cached != contract_bindings_.end()) {
            current = cached->second;
        } else {
            std::string contract_error;
            auto document = project_.read_shared_json(contract_path, contract_error);
            if (!document) {
                error = "contract '" + root_name + "': failed to parse " + contract_path_argument +
                        (contract_error.empty() ? "" : " (" + contract_error + ")");
                return true;
            }
            contract_bindings_.emplace(root_name, document);
            current = std::move(document);
        }

        result_.dependencies.insert(project_.relative(project_.root / ".nift/config.json"));
        result_.dependencies.insert(project_.relative(contract_path));
    }

    while (position < expression.size()) {
        if (expression[position] == '.') {
            ++position;
            std::string member;
            if (!identifier(member)) {
                error = "invalid JSON member access in '" + expression + "'";
                return true;
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
            ++position;
            const std::size_t index_start = position;
            while (position < expression.size() &&
                   std::isdigit(static_cast<unsigned char>(expression[position]))) {
                ++position;
            }
            if (index_start == position || position >= expression.size() || expression[position] != ']') {
                error = "JSON array indices must be non-negative integers in '" + expression + "'";
                return true;
            }

            std::size_t index = 0;
            try {
                index = static_cast<std::size_t>(
                    std::stoull(expression.substr(index_start, position - index_start)));
            } catch (...) {
                error = "JSON array index is out of range in '" + expression + "'";
                return true;
            }
            ++position;

            if (!current->is_array()) {
                error = "cannot index JSON value in '" + expression + "' because it is not an array";
                return true;
            }
            if (index >= current->array.size()) {
                error = "JSON array index " + std::to_string(index) +
                        " is out of range in '" + expression + "'";
                return true;
            }

            const json::Document* child = &(*current)[index];
            current = std::shared_ptr<const json::Document>(current, child);
            continue;
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
        if (built_in_metadata_name(expression)) {
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

bool Parser::scalar_literal(const std::string& text, json::Document& value, std::string& error) const {
    const std::string trimmed = trim_copy(text);
    if (trimmed.empty()) {
        error = "expected a value in @if condition";
        return false;
    }

    if ((trimmed.front() == '"' && trimmed.back() == '"') ||
        (trimmed.front() == '\'' && trimmed.back() == '\'')) {
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

    char* end = nullptr;
    const double number = std::strtod(trimmed.c_str(), &end);
    if (end && *end == '\0') {
        value = json::Document(number);
        return true;
    }

    error = "expected JSON path or scalar literal in @if condition: " + trimmed;
    return false;
}

bool Parser::evaluate_condition(const std::string& expression, bool& value, std::string& error) {
    auto resolve_operand = [&](const std::string& operand,
                               std::shared_ptr<const json::Document>& document) -> bool {
        const std::string trimmed = trim_copy(operand);
        std::string local_error;
        if (resolve_json_value(trimmed, document, local_error)) {
            if (!local_error.empty()) error = local_error;
            return local_error.empty();
        }

        const bool built_in_metadata = built_in_metadata_name(trimmed);
        if (built_in_metadata) {
            document = std::make_shared<const json::Document>(metadata(trimmed));
            return true;
        }

        json::Document literal;
        if (!scalar_literal(trimmed, literal, error)) return false;
        document = std::make_shared<const json::Document>(std::move(literal));
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

std::string Parser::path_to(const std::string& argument) {
    const fs::path output = project_.output_path(tracked_info_);
    const fs::path base = output.parent_path();
    fs::path destination;
    bool index_page = false;

    if (const TrackedInfo* target = project_.find(argument)) {
        destination = project_.output_path(*target);
        index_page = target->name == "/" || (!target->name.empty() && target->name.back() == '/');
    } else {
        destination = (project_.root / argument).lexically_normal();
        if (!filesystem::path_within(project_.root, destination)) {
            result_.ok = false;
            result_.error = {tracked_info_.name, {}, 0, "pathto: path must stay inside the Nift project: " + argument};
            return {};
        }
        if (!filesystem::path_exists(destination)) {
            result_.ok = false;
            result_.error = {tracked_info_.name, {}, 0, "'" + argument + "' is neither a tracked name nor a file that exists"};
            return {};
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
        if (i + 1 < source.size() && source[i] == '\\' && (source[i + 1] == '@' || source[i + 1] == '$' || source[i + 1] == '#')) {
            output += source[i + 1];
            i += 2;
            continue;
        }

        if (source.compare(i, 4, "<#--") == 0) {
            const auto end = source.find("--#>", i + 4);
            if (end == std::string::npos) { fail(source_path, source, i, "open comment '<#--' has no close '--#>'"); break; }
            i = end + 4;
            continue;
        }
        if (source.compare(i, 2, "@#") == 0 || source.compare(i, 3, "@//") == 0) {
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

        if (source.compare(i, 2, "$[") == 0) {
            std::size_t end = i + 2;
            std::size_t nested_brackets = 0;
            for (; end < source.size(); ++end) {
                if (source[end] == '[') {
                    ++nested_brackets;
                } else if (source[end] == ']') {
                    if (nested_brackets == 0) break;
                    --nested_brackets;
                }
            }

            if (end < source.size()) {
                const std::string key = source.substr(i + 2, end - i - 2);

                auto split_ternary = [&](const std::string& expression,
                                         std::string& condition,
                                         std::string& when_true,
                                         std::string& when_false) -> bool {
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
                    return false;
                };

                std::string ternary_condition, when_true, when_false;
                if (split_ternary(key, ternary_condition, when_true, when_false)) {
                    bool condition_value = false;
                    std::string condition_error;
                    if (!evaluate_condition(ternary_condition, condition_value, condition_error)) {
                        fail(source_path, source, i, condition_error);
                        break;
                    }
                    const std::string& selected = condition_value ? when_true : when_false;
                    const auto nested = parse(selected, source_path, depth + 1);
                    if (!nested.ok) break;
                    output += nested.output;
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
                    const auto nested = parse(body.text, source_path, depth + 1);
                    pop_json_scope();
                    if (!nested.ok) break;
                    append_indented(output, nested.output, control_indent, insertion_code_block_depth);
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
            std::string collection_error;
            if (!resolve_json_value(collection_expression, collection, collection_error)) {
                fail(source_path, source, i, "@for collection must be a bound JSON value: " + collection_expression);
                break;
            }
            if (!collection_error.empty()) {
                fail(source_path, source, i, collection_error);
                break;
            }

            const auto body = normalize_control_block_body(
                source.substr(block_open + 1, block_close - block_open - 1));
            const std::string control_indent = insertion_indent(output);
            const int insertion_code_block_depth = code_block_depth_;

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
                if (project_.config.contracts.count(binding_part)) {
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
                    const auto nested = parse(body.text, source_path, depth + 1);
                    pop_json_scope();
                    if (!nested.ok) break;
                    append_indented(output, nested.output, control_indent, insertion_code_block_depth);
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
                if (project_.config.contracts.count(key_name) || project_.config.contracts.count(value_name)) {
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
                    const auto nested = parse(body.text, source_path, depth + 1);
                    pop_json_scope();
                    if (!nested.ok) break;
                    append_indented(output, nested.output, control_indent, insertion_code_block_depth);
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
            bool has_parameters = false;
            bool parameters_ok = true;
            std::size_t end = call_start;

            if (call_start < source.size() && source[call_start] == '(') {
                has_parameters = true;
                bool quoted = false;
                char quote = 0;
                std::size_t close = call_start + 1;
                for (; close < source.size(); ++close) {
                    const char c = source[close];
                    if (quoted) {
                        if (c == '\\') ++close;
                        else if (c == quote) quoted = false;
                    } else if (c == '\'' || c == '"') { quoted = true; quote = c; }
                    else if (c == ')') break;
                }
                if (close >= source.size()) { fail(source_path, source, i, function + ": malformed parameters"); break; }
                parameters = parse_parameters(source.substr(call_start + 1, close - call_start - 1), parameters_ok);
                if (!parameters_ok) { fail(source_path, source, i, function + ": malformed parameters"); break; }
                end = close + 1;
                if (end < source.size() && source[end] == ';') ++end;
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
                if (++result_.content_count > 1) {
                    fail(source_path, source, i, "@content may be executed exactly once for a templated tracked item");
                    break;
                }

                const fs::path content_path = fs::absolute(project_.content_path(tracked_info_)).lexically_normal();
                if (std::find(input_stack_.begin(), input_stack_.end(), content_path) != input_stack_.end()) {
                    fail(source_path, source, i, "@content would result in an input loop through " + content_path.generic_string());
                    break;
                }

                if (!filesystem::file_readable(content_path)) {
                    fail(source_path, source, i, "content file is not readable");
                    break;
                }
                input_stack_.push_back(content_path);
                result_.dependencies.insert(project_.relative(content_path));
                const int insertion_code_block_depth = code_block_depth_;
                const std::string content_source = filesystem::read_file(content_path);
                const auto nested = parse(content_source, content_path, depth + 1);
                input_stack_.pop_back();

                if (!nested.ok) break;

                append_indented(output, nested.output, indent, insertion_code_block_depth);
                result_.content_used = true;
                i = end;
                continue;
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
                std::shared_ptr<const json::Document> array;
                std::string join_error;
                if (!resolve_json_value(expression, array, join_error) || !join_error.empty()) {
                    fail(source_path, source, i, join_error.empty() ? "join: expected JSON array value" : "join: " + join_error);
                    break;
                }
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
                    const fs::path relative_to_source = source_path.parent_path() / input_path;
                    input_path = filesystem::path_exists(relative_to_source) ? relative_to_source : project_.root / input_path;
                }
                if (!filesystem::path_exists(input_path)) { fail(source_path, source, i, "@input path does not exist: " + parameters[0]); break; }
                input_path = fs::absolute(input_path).lexically_normal();
                if (std::find(input_stack_.begin(), input_stack_.end(), input_path) != input_stack_.end()) {
                    fail(source_path, source, i, "@input would result in an input loop through " + input_path.generic_string());
                    break;
                }
                if (!filesystem::file_readable(input_path)) { fail(source_path, source, i, "input file is not readable"); break; }
                input_stack_.push_back(input_path);
                result_.dependencies.insert(project_.relative(input_path));
                const int insertion_code_block_depth = code_block_depth_;
                const auto input_source = project_.read_shared_source(input_path);
                const auto nested = parse(*input_source, input_path, depth + 1);
                input_stack_.pop_back();
                if (!nested.ok) break;
                append_indented(output, nested.output, indent, insertion_code_block_depth);
                i = end;
                continue;
            }

            if (function == "pathto" || function == "pathtofile") {
                if (!has_parameters || parameters.size() != 1) { fail(source_path, source, i, "@" + function + " expects exactly one path/name"); break; }
                std::string resolved, interpolation_error;
                if (!interpolate_parameter(parameters[0], resolved, interpolation_error)) {
                    fail(source_path, source, i, function + ": " + interpolation_error); break;
                }
                parameters[0] = std::move(resolved);

                if (!project_.find(parameters[0])) {
                    const fs::path target_path = (project_.root / parameters[0]).lexically_normal();
                    if (!filesystem::path_within(project_.root, target_path)) {
                        fail(source_path, source, i, "@" + function + " path must stay inside the Nift project: " + parameters[0]);
                        break;
                    }
                }

                output += path_to(parameters[0]);
                if (!result_.ok) { if (result_.error.source_file.empty()) fail(source_path, source, i, result_.error.message); break; }
                fs::path requirement;
                if (const TrackedInfo* target = project_.find(parameters[0])) requirement = project_.output_path(*target);
                else requirement = project_.root / parameters[0];
                result_.reqs.insert(project_.relative(requirement.lexically_normal()));
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
                if (const char* value = std::getenv(parameters[0].c_str())) output += value;
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

            if (function == "json") {
                if (!has_parameters || (parameters.size() != 2 && parameters.size() != 3)) {
                    fail(source_path, source, i, "json: expected 2 or 3 parameters (path, name[, schema])");
                    break;
                }

                std::string resolved_path, interpolation_error;
                if (!interpolate_parameter(parameters[0], resolved_path, interpolation_error)) {
                    fail(source_path, source, i, "json: " + interpolation_error); break;
                }
                parameters[0] = std::move(resolved_path);
                if (parameters.size() == 3) {
                    std::string resolved_schema;
                    if (!interpolate_parameter(parameters[2], resolved_schema, interpolation_error)) {
                        fail(source_path, source, i, "json: " + interpolation_error); break;
                    }
                    parameters[2] = std::move(resolved_schema);
                }

                const std::string& json_path_argument = parameters[0];
                const std::string& binding_name = parameters[1];
                const bool valid_name =
                    !binding_name.empty() &&
                    (std::isalpha(static_cast<unsigned char>(binding_name[0])) || binding_name[0] == '_') &&
                    std::all_of(binding_name.begin() + 1, binding_name.end(), [](char c) {
                        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
                    });
                if (!valid_name) {
                    fail(source_path, source, i, "json: name must be an identifier using letters, digits and underscores");
                    break;
                }
                if (reserved_binding_name(binding_name)) {
                    fail(source_path, source, i, "json: name '" + binding_name + "' conflicts with built-in metadata/reserved bindings");
                    break;
                }
                if (project_.config.contracts.count(binding_name)) {
                    fail(source_path, source, i, "json: name '" + binding_name + "' conflicts with configured contract namespace");
                    break;
                }
                if (json_bindings_.count(binding_name)) {
                    fail(source_path, source, i, "json: name '" + binding_name + "' is already bound");
                    break;
                }

                const fs::path json_path = (project_.root / json_path_argument).lexically_normal();
                const fs::path project_root = project_.root.lexically_normal();
                if (!filesystem::path_within(project_root, json_path)) {
                    fail(source_path, source, i, "json: path must stay inside the Nift project: " + json_path_argument);
                    break;
                }
                if (!filesystem::path_exists(json_path)) {
                    fail(source_path, source, i, "json: file does not exist: " + json_path_argument);
                    break;
                }

                std::string json_error;
                auto document = project_.read_shared_json(json_path, json_error);
                if (!document) {
                    fail(source_path, source, i, "json: failed to parse " + json_path_argument +
                         (json_error.empty() ? "" : " (" + json_error + ")"));
                    break;
                }

                if (parameters.size() == 3) {
                    const std::string& schema_path_argument = parameters[2];
                    const fs::path schema_path = (project_.root / schema_path_argument).lexically_normal();
                    if (!filesystem::path_within(project_root, schema_path)) {
                        fail(source_path, source, i,
                             "json: schema path must stay inside the Nift project: " + schema_path_argument);
                        break;
                    }
                    if (!filesystem::path_exists(schema_path)) {
                        fail(source_path, source, i, "json: schema file does not exist: " + schema_path_argument);
                        break;
                    }
                    std::string schema_parse_error;
                    auto schema = project_.read_shared_json(schema_path, schema_parse_error);
                    if (!schema) {
                        fail(source_path, source, i, "json: failed to parse schema " + schema_path_argument +
                             (schema_parse_error.empty() ? "" : " (" + schema_parse_error + ")"));
                        break;
                    }
                    std::string schema_validation_error;
                    if (!jsonschema::validate(*document, *schema, schema_validation_error)) {
                        fail(source_path, source, i,
                             "json: " + json_path_argument + " does not satisfy schema " +
                             schema_path_argument + " (" + schema_validation_error + ")");
                        break;
                    }
                    result_.dependencies.insert(project_.relative(schema_path));
                }

                json_bindings_.emplace(binding_name, std::move(document));
                if (!json_binding_scopes_.empty()) json_binding_scopes_.back().push_back(binding_name);
                result_.dependencies.insert(project_.relative(json_path));
                i = end;
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
                    const fs::path dependency_path = (project_.root / dependency).lexically_normal();
                    if (!filesystem::path_within(project_.root, dependency_path)) {
                        fail(source_path, source, i, "dep: path must stay inside the Nift project: " + dependency);
                        break;
                    }
                    if (!filesystem::path_exists(dependency_path)) {
                        fail(source_path, source, i, "failed as dependency does not exist: " + dependency);
                        break;
                    }
                    result_.dependencies.insert(project_.relative(dependency_path));
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

RenderResult Parser::render() {
    const fs::path content_path = project_.content_path(tracked_info_);
    if (tracked_info_.template_path.empty()) {
        if (!filesystem::file_readable(content_path)) {
            result_.ok = false;
            result_.error = {tracked_info_.name, content_path, 0, "content file is not readable"};
            return result_;
        }
        auto result = parse(filesystem::read_file(content_path), content_path, 0);
        result.content_used = true;
        result.dependencies.insert(project_.relative(content_path));
        return result;
    }

    const fs::path template_path = project_.root / tracked_info_.template_path;
    if (!filesystem::path_exists(template_path)) {
        result_.ok = false;
        result_.error = {tracked_info_.name, template_path, 0, "template file does not exist"};
        return result_;
    }

    if (!filesystem::file_readable(template_path)) {
        result_.ok = false;
        result_.error = {tracked_info_.name, template_path, 0, "template file is not readable"};
        return result_;
    }
    input_stack_.push_back(fs::absolute(template_path).lexically_normal());
    result_.dependencies.insert(tracked_info_.template_path);
    const auto template_source = project_.read_shared_source(template_path);
    auto result = parse(*template_source, template_path, 0);
    if (result.ok && result.content_count != 1) {
        result.ok = false;
        result.error = {tracked_info_.name, template_path, 0, "templated tracked items must execute exactly one @content; add @content through the template/input graph or omit the tracked template field"};
    }
    return result;
}
