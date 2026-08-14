#include "Parser.h"
#include "FileSystem.h"
#include "ProjectInfo.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::vector<std::string> parse_parameters(const std::string& text, bool& ok) {
    std::vector<std::string> result;
    std::string current;
    bool quoted = false;
    char quote = 0;
    ok = true;

    auto append_trimmed = [&] {
        const auto first = current.find_first_not_of(" \t\r\n");
        const auto last = current.find_last_not_of(" \t\r\n");
        result.push_back(first == std::string::npos ? "" : current.substr(first, last - first + 1));
        current.clear();
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '\\' && i + 1 < text.size()) current += text[++i];
            else if (c == quote) quoted = false;
            else current += c;
        } else if (c == '\'' || c == '"') {
            quoted = true;
            quote = c;
        } else if (c == ',') {
            append_trimmed();
        } else {
            current += c;
        }
    }

    if (quoted) { ok = false; return {}; }
    if (!text.empty() || !current.empty()) append_trimmed();
    return result;
}

void append_indented(std::string& output, const std::string& text, const std::string& indent,
                     bool trim_trailing_newline = true) {
    std::size_t size = text.size();
    if (trim_trailing_newline && size && text[size - 1] == '\n') {
        --size;
        if (size && text[size - 1] == '\r') --size;
    }
    if (size == 0) return;

    std::size_t start = 0;
    while (start < size) {
        const std::size_t newline = text.find('\n', start);
        if (newline == std::string::npos || newline >= size) {
            output.append(text, start, size - start);
            break;
        }
        output.append(text, start, newline - start + 1);
        if (newline + 1 < size) output += indent;
        start = newline + 1;
    }
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

std::string Parser::path_to(const std::string& argument) {
    const fs::path output = project_.output_path(tracked_info_);
    const fs::path base = output.parent_path();
    fs::path destination;
    bool index_page = false;

    if (const TrackedInfo* target = project_.find(argument)) {
        destination = project_.output_path(*target);
        index_page = target->name == "/" || (!target->name.empty() && target->name.back() == '/');
    } else {
        destination = project_.root / argument;
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
        fail(source_path, source, 0, "maximum @input depth exceeded (possible input loop)");
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

        if (source.compare(i, 4, "@#--") == 0) {
            const auto end = source.find("--#", i + 4);
            if (end == std::string::npos) { fail(source_path, source, i, "open comment #-- has no close --#"); break; }
            i = end + 3;
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
            const auto end = source.find(']', i + 2);
            if (end != std::string::npos) {
                const std::string key = source.substr(i + 2, end - i - 2);
                const std::string value = metadata(key);
                if (!value.empty() || key == "title" || key == "name") {
                    output += value;
                    i = end + 1;
                    continue;
                }
            }
        }

        if (source[i] == '@' && i + 1 < source.size() && source[i + 1] >= 'a' && source[i + 1] <= 'z') {
            std::size_t name_end = i + 1;
            while (name_end < source.size() && source[name_end] >= 'a' && source[name_end] <= 'z') ++name_end;
            const std::string function = source.substr(i + 1, name_end - i - 1);
            std::vector<std::string> parameters;
            bool has_parameters = false;
            bool parameters_ok = true;
            std::size_t end = name_end;

            if (name_end < source.size() && source[name_end] == '(') {
                has_parameters = true;
                bool quoted = false;
                char quote = 0;
                std::size_t close = name_end + 1;
                for (; close < source.size(); ++close) {
                    const char c = source[close];
                    if (quoted) {
                        if (c == '\\') ++close;
                        else if (c == quote) quoted = false;
                    } else if (c == '\'' || c == '"') { quoted = true; quote = c; }
                    else if (c == ')') break;
                }
                if (close >= source.size()) { fail(source_path, source, i, function + ": malformed parameters"); break; }
                parameters = parse_parameters(source.substr(name_end + 1, close - name_end - 1), parameters_ok);
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

                const fs::path content_path = fs::absolute(project_.content_path(tracked_info_)).lexically_normal();
                if (std::find(input_stack_.begin(), input_stack_.end(), content_path) != input_stack_.end()) {
                    fail(source_path, source, i, "@content would result in an input loop through " + content_path.generic_string());
                    break;
                }

                input_stack_.push_back(content_path);
                result_.dependencies.insert(project_.relative(content_path));
                const std::string content_source = filesystem::read_file(content_path);
                const auto nested = parse(content_source, content_path, depth + 1);
                input_stack_.pop_back();

                if (!nested.ok) break;

                append_indented(output, nested.output, indent);
                result_.content_used = true;
                i = end;
                continue;
            }

            if (function == "input") {
                if (!has_parameters || parameters.size() != 1) { fail(source_path, source, i, "input: expected 1 parameter"); break; }
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
                input_stack_.push_back(input_path);
                result_.dependencies.insert(project_.relative(input_path));
                const auto input_source = project_.read_shared_source(input_path);
                const auto nested = parse(*input_source, input_path, depth + 1);
                input_stack_.pop_back();
                if (!nested.ok) break;
                append_indented(output, nested.output, indent);
                i = end;
                continue;
            }

            if (function == "pathto" || function == "pathtofile") {
                if (!has_parameters || parameters.size() != 1) { fail(source_path, source, i, "@" + function + " expects exactly one path/name"); break; }
                output += path_to(parameters[0]);
                if (!result_.ok) { if (result_.error.source_file.empty()) fail(source_path, source, i, result_.error.message); break; }
                i = end;
                continue;
            }

            if (function == "getenv") {
                if (!has_parameters || parameters.size() != 1) { fail(source_path, source, i, "getenv: expected 1 parameter"); break; }
                if (const char* value = std::getenv(parameters[0].c_str())) output += value;
                i = end;
                continue;
            }

            if (function == "ent") {
                if (!has_parameters || parameters.size() != 1) { fail(source_path, source, i, "@ent expects exactly one entity"); break; }
                bool known = false;
                output += entity(parameters[0], known);
                if (!known) { fail(source_path, source, i, "do not currently have an entity value for '" + parameters[0] + "'"); break; }
                i = end;
                continue;
            }

            if (function == "dep") {
                if (!has_parameters || parameters.empty()) { fail(source_path, source, i, "dep: expected parameters"); break; }
                for (const auto& dependency : parameters) {
                    if (!filesystem::path_exists(project_.root / dependency)) {
                        fail(source_path, source, i, "failed as dependency does not exist: " + dependency);
                        break;
                    }
                    result_.dependencies.insert(dependency);
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

    input_stack_.push_back(fs::absolute(template_path).lexically_normal());
    result_.dependencies.insert(tracked_info_.template_path);
    const auto template_source = project_.read_shared_source(template_path);
    auto result = parse(*template_source, template_path, 0);
    std::error_code content_size_error;
    const auto content_size = fs::file_size(content_path, content_size_error);
    if (result.ok && !result.content_used && !content_size_error && content_size > 0) {
        result.ok = false;
        result.error = {tracked_info_.name, template_path, 0, "content has not been used as a dependency; add @content to the template or use an empty template path"};
    }
    return result;
}
