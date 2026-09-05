#include <markup/Markup.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "../vendor/cmark/cmark.h"
#include "AsciiDoc.h"
#include "ReStructuredText.h"

namespace markup {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

std::string escape_html(const std::string& value, bool attribute = false) {
    std::string out;
    out.reserve(value.size());
    for (const unsigned char c : value) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += attribute ? "&quot;" : "\""; break;
        default: out.push_back(static_cast<char>(c)); break;
        }
    }
    return out;
}

bool is_entity(const std::string& text, std::size_t at, std::size_t& end) {
    if (at >= text.size() || text[at] != '&') return false;
    std::size_t i = at + 1;
    if (i < text.size() && text[i] == '#') {
        ++i;
        if (i < text.size() && (text[i] == 'x' || text[i] == 'X')) ++i;
        const std::size_t digits = i;
        while (i < text.size() && std::isalnum(static_cast<unsigned char>(text[i]))) ++i;
        if (i == digits) return false;
    } else {
        const std::size_t name = i;
        while (i < text.size() && std::isalnum(static_cast<unsigned char>(text[i]))) ++i;
        if (i == name) return false;
    }
    if (i < text.size() && text[i] == ';') {
        end = i + 1;
        return true;
    }
    return false;
}

bool safe_url(const std::string& url) {
    std::string compact;
    for (unsigned char c : url) {
        if (!std::isspace(c) && c != 0) compact.push_back(static_cast<char>(std::tolower(c)));
    }
    return compact.rfind("javascript:", 0) != 0 && compact.rfind("vbscript:", 0) != 0 &&
           compact.rfind("data:", 0) != 0;
}

std::string inline_html(const std::string& text, const Options& options);

bool parse_link_target(const std::string& text, std::size_t open, std::size_t& close,
                       std::string& url, std::string& title) {
    if (open >= text.size() || text[open] != '(') return false;
    std::size_t i = open + 1;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    if (i >= text.size()) return false;
    if (text[i] == '<') {
        const auto end = text.find('>', i + 1);
        if (end == std::string::npos) return false;
        url = text.substr(i + 1, end - i - 1);
        i = end + 1;
    } else {
        const std::size_t start = i;
        int depth = 0;
        while (i < text.size()) {
            if (text[i] == '\\' && i + 1 < text.size()) { i += 2; continue; }
            if (text[i] == '(') ++depth;
            if (text[i] == ')') {
                if (depth == 0) break;
                --depth;
            }
            if (depth == 0 && std::isspace(static_cast<unsigned char>(text[i]))) break;
            ++i;
        }
        url = text.substr(start, i - start);
    }
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    if (i < text.size() && (text[i] == '"' || text[i] == '\'')) {
        const char quote = text[i++];
        const auto end = text.find(quote, i);
        if (end == std::string::npos) return false;
        title = text.substr(i, end - i);
        i = end + 1;
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    }
    if (i >= text.size() || text[i] != ')') return false;
    close = i + 1;
    return !url.empty();
}

std::string inline_html(const std::string& text, const Options& options) {
    const bool extensions = options.markdown_profile == Options::MarkdownProfile::Extended;
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\\' && i + 1 < text.size() &&
            std::string("\\`*{}_[]()#+-.!|>").find(text[i + 1]) != std::string::npos) {
            out += escape_html(text.substr(i + 1, 1));
            i += 2;
            continue;
        }

        if (text[i] == '`') {
            std::size_t run = 1;
            while (i + run < text.size() && text[i + run] == '`') ++run;
            const std::string delimiter(run, '`');
            const auto end = text.find(delimiter, i + run);
            if (end != std::string::npos) {
                std::string code = text.substr(i + run, end - i - run);
                std::replace(code.begin(), code.end(), '\n', ' ');
                if (code.size() >= 2 && code.front() == ' ' && code.back() == ' ' &&
                    code.find_first_not_of(' ') != std::string::npos) {
                    code = code.substr(1, code.size() - 2);
                }
                out += "<code>" + escape_html(code) + "</code>";
                i = end + run;
                continue;
            }
        }

        const bool image = text[i] == '!' && i + 1 < text.size() && text[i + 1] == '[';
        if (image || text[i] == '[') {
            const std::size_t label_open = i + (image ? 2 : 1);
            const auto label_close = text.find(']', label_open);
            if (label_close != std::string::npos && label_close + 1 < text.size()) {
                std::size_t target_close = 0;
                std::string url, title;
                if (parse_link_target(text, label_close + 1, target_close, url, title)) {
                    if (!options.allow_raw_html && !safe_url(url)) url.clear();
                    const std::string title_attr = title.empty() ? "" :
                        " title=\"" + escape_html(title, true) + "\"";
                    if (image) {
                        out += "<img src=\"" + escape_html(url, true) + "\" alt=\"" +
                               escape_html(text.substr(label_open, label_close - label_open), true) +
                               "\"" + title_attr + ">";
                    } else {
                        out += "<a href=\"" + escape_html(url, true) + "\"" + title_attr + ">" +
                               inline_html(text.substr(label_open, label_close - label_open), options) +
                               "</a>";
                    }
                    i = target_close;
                    continue;
                }
            }
        }

        struct Delimiter { const char* marker; const char* open; const char* close; };
        static const Delimiter delimiters[] = {
            {"**", "<strong>", "</strong>"}, {"__", "<strong>", "</strong>"},
            {"*", "<em>", "</em>"}, {"_", "<em>", "</em>"},
        };
        bool matched = false;
        for (const auto& delimiter : delimiters) {
            const std::string marker = delimiter.marker;
            if (text.compare(i, marker.size(), marker) != 0) continue;
            auto end = text.find(marker, i + marker.size());
            // In ***nested*** forms the first character of the closing run
            // closes inner emphasis and the last two close strong emphasis.
            if (marker.size() == 2 && end != std::string::npos &&
                end + marker.size() < text.size() && text[end + marker.size()] == marker[0] &&
                text.substr(i + marker.size(), end - i - marker.size()).find(marker[0]) != std::string::npos) {
                ++end;
            }
            if (end == std::string::npos || end == i + marker.size()) continue;
            out += delimiter.open;
            out += inline_html(text.substr(i + marker.size(), end - i - marker.size()), options);
            out += delimiter.close;
            i = end + marker.size();
            matched = true;
            break;
        }
        if (matched) continue;

        if (extensions && text.compare(i, 2, "~~") == 0) {
            const auto end = text.find("~~", i + 2);
            if (end != std::string::npos && end != i + 2) {
                out += "<del>" + inline_html(text.substr(i + 2, end - i - 2), options) + "</del>";
                i = end + 2;
                continue;
            }
        }

        if (text[i] == '<') {
            const auto end = text.find('>', i + 1);
            if (end != std::string::npos) {
                const std::string inside = text.substr(i + 1, end - i - 1);
                if (inside.rfind("http://", 0) == 0 || inside.rfind("https://", 0) == 0) {
                    out += "<a href=\"" + escape_html(inside, true) + "\">" + escape_html(inside) + "</a>";
                    i = end + 1;
                    continue;
                }
                if (inside.find('@') != std::string::npos && inside.find(' ') == std::string::npos) {
                    out += "<a href=\"mailto:" + escape_html(inside, true) + "\">" + escape_html(inside) + "</a>";
                    i = end + 1;
                    continue;
                }
                if (options.allow_raw_html && !inside.empty() &&
                    (std::isalpha(static_cast<unsigned char>(inside[0])) || inside[0] == '/' || inside[0] == '!')) {
                    out += text.substr(i, end - i + 1);
                    i = end + 1;
                    continue;
                }
            }
        }

        if (text[i] == '&') {
            std::size_t end = 0;
            if (is_entity(text, i, end)) {
                out += text.substr(i, end - i);
                i = end;
                continue;
            }
        }

        if (text[i] == '\n') {
            std::size_t spaces = 0;
            for (std::size_t j = i; j > 0 && text[j - 1] == ' '; --j) ++spaces;
            if (spaces >= 2) {
                if (out.size() >= spaces) out.resize(out.size() - spaces);
                out += "<br>\n";
            } else {
                out += '\n';
            }
            ++i;
            continue;
        }
        out += escape_html(text.substr(i, 1));
        ++i;
    }
    return out;
}

std::vector<std::string> lines(const std::string& input) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= input.size()) {
        const auto end = input.find('\n', start);
        std::string line = input.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        result.push_back(std::move(line));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

bool thematic_break(const std::string& line) {
    const std::string value = trim(line);
    char marker = 0;
    int count = 0;
    for (char c : value) {
        if (c == ' ' || c == '\t') continue;
        if (marker == 0 && (c == '*' || c == '-' || c == '_')) marker = c;
        if (c != marker) return false;
        ++count;
    }
    return count >= 3;
}

bool setext(const std::string& line, int& level) {
    const std::string value = trim(line);
    if (value.empty()) return false;
    const char c = value.front();
    if (c != '=' && c != '-') return false;
    if (!std::all_of(value.begin(), value.end(), [c](char x) { return x == c || x == ' ' || x == '\t'; })) return false;
    level = c == '=' ? 1 : 2;
    return true;
}

bool fence(const std::string& line, char& marker, std::size_t& count, std::string& info) {
    const std::string value = trim(line);
    if (value.size() < 3 || (value[0] != '`' && value[0] != '~')) return false;
    marker = value[0];
    count = 0;
    while (count < value.size() && value[count] == marker) ++count;
    if (count < 3) return false;
    info = trim(value.substr(count));
    return marker != '`' || info.find('`') == std::string::npos;
}

struct ListMarker {
    bool ordered = false;
    unsigned start = 1;
    std::size_t content = 0;
};

bool list_marker(const std::string& line, ListMarker& marker) {
    std::size_t i = 0;
    while (i < line.size() && i < 4 && line[i] == ' ') ++i;
    if (i >= line.size()) return false;
    if ((line[i] == '-' || line[i] == '*' || line[i] == '+') && i + 1 < line.size() &&
        std::isspace(static_cast<unsigned char>(line[i + 1]))) {
        marker.content = i + 2;
        return true;
    }
    const std::size_t digits = i;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])) && i - digits < 9) ++i;
    if (i == digits || i + 1 >= line.size() || (line[i] != '.' && line[i] != ')') ||
        !std::isspace(static_cast<unsigned char>(line[i + 1]))) return false;
    marker.ordered = true;
    marker.start = static_cast<unsigned>(std::stoul(line.substr(digits, i - digits)));
    marker.content = i + 2;
    return true;
}

std::vector<std::string> table_cells(const std::string& line) {
    std::string value = trim(line);
    if (!value.empty() && value.front() == '|') value.erase(value.begin());
    if (!value.empty() && value.back() == '|') value.pop_back();
    std::vector<std::string> cells;
    std::string cell;
    bool escaped = false;
    for (char c : value) {
        if (c == '|' && !escaped) {
            cells.push_back(trim(cell));
            cell.clear();
        } else {
            cell.push_back(c);
        }
        escaped = c == '\\' && !escaped;
        if (c != '\\') escaped = false;
    }
    cells.push_back(trim(cell));
    return cells;
}

bool table_delimiter(const std::string& line, std::vector<std::string>& alignments) {
    const auto cells = table_cells(line);
    if (cells.empty()) return false;
    alignments.clear();
    for (std::string cell : cells) {
        cell = trim(cell);
        const bool left = !cell.empty() && cell.front() == ':';
        const bool right = !cell.empty() && cell.back() == ':';
        if (left) cell.erase(cell.begin());
        if (right && !cell.empty()) cell.pop_back();
        if (cell.size() < 3 || !std::all_of(cell.begin(), cell.end(), [](char c) { return c == '-'; })) return false;
        alignments.push_back(left && right ? "center" : right ? "right" : left ? "left" : "");
    }
    return true;
}

std::string markdown_fragment(const std::string& input, const Options& options) {
    const bool extensions = options.markdown_profile == Options::MarkdownProfile::Extended;
    if (!extensions) {
        const int cmark_options = CMARK_OPT_VALIDATE_UTF8 |
            (options.allow_raw_html ? CMARK_OPT_UNSAFE : CMARK_OPT_DEFAULT);
        char* rendered = cmark_markdown_to_html(input.data(), input.size(), cmark_options);
        if (!rendered) return {};
        std::string result(rendered);
        cmark_get_default_mem_allocator()->free(rendered);
        return result;
    }
    const auto source = lines(input);
    std::string out;
    std::size_t i = 0;
    while (i < source.size()) {
        if (trim(source[i]).empty()) { ++i; continue; }

        char fence_marker = 0;
        std::size_t fence_count = 0;
        std::string info;
        if (fence(source[i], fence_marker, fence_count, info)) {
            ++i;
            std::string code;
            bool closed = false;
            while (i < source.size()) {
                char closing_marker = 0;
                std::size_t closing_count = 0;
                std::string closing_info;
                if (fence(source[i], closing_marker, closing_count, closing_info) &&
                    closing_marker == fence_marker && closing_count >= fence_count && closing_info.empty()) {
                    closed = true;
                    ++i;
                    break;
                }
                if (!code.empty()) code += '\n';
                code += source[i++];
            }
            (void)closed; // An unclosed fence intentionally consumes to EOF.
            const auto language_end = info.find_first_of(" \t{");
            const std::string language = info.substr(0, language_end);
            out += "<pre><code";
            if (!language.empty()) out += " class=\"language-" + escape_html(language, true) + "\"";
            out += ">" + escape_html(code) + "</code></pre>\n";
            continue;
        }

        const std::string current = trim(source[i]);
        std::size_t hashes = 0;
        while (hashes < current.size() && current[hashes] == '#') ++hashes;
        if (hashes >= 1 && hashes <= 6 && hashes < current.size() && current[hashes] == ' ') {
            std::string body = trim(current.substr(hashes + 1));
            while (!body.empty() && body.back() == '#') body.pop_back();
            body = trim(body);
            out += "<h" + std::to_string(hashes) + ">" + inline_html(body, options) +
                   "</h" + std::to_string(hashes) + ">\n";
            ++i;
            continue;
        }

        int setext_level = 0;
        if (i + 1 < source.size() && setext(source[i + 1], setext_level)) {
            out += "<h" + std::to_string(setext_level) + ">" + inline_html(current, options) +
                   "</h" + std::to_string(setext_level) + ">\n";
            i += 2;
            continue;
        }

        if (thematic_break(source[i])) {
            out += "<hr>\n";
            ++i;
            continue;
        }

        if (source[i].size() >= 4 && source[i].substr(0, 4) == "    ") {
            std::string code;
            while (i < source.size() && (source[i].size() >= 4 || trim(source[i]).empty())) {
                if (!code.empty()) code += '\n';
                code += source[i].size() >= 4 ? source[i].substr(4) : "";
                ++i;
            }
            while (!code.empty() && code.back() == '\n') code.pop_back();
            out += "<pre><code>" + escape_html(code) + "</code></pre>\n";
            continue;
        }

        if (!current.empty() && current[0] == '>') {
            std::string quote;
            while (i < source.size()) {
                const std::string line = trim(source[i]);
                if (line.empty() || line[0] != '>') break;
                std::string part = line.substr(1);
                if (!part.empty() && part[0] == ' ') part.erase(part.begin());
                if (!quote.empty()) quote += '\n';
                quote += part;
                ++i;
            }
            out += "<blockquote>\n" + markdown_fragment(quote, options) + "</blockquote>\n";
            continue;
        }

        ListMarker first_marker;
        if (list_marker(source[i], first_marker)) {
            const bool ordered = first_marker.ordered;
            out += ordered ? "<ol" : "<ul";
            if (ordered && first_marker.start != 1) out += " start=\"" + std::to_string(first_marker.start) + "\"";
            out += ">\n";
            while (i < source.size()) {
                ListMarker item_marker;
                if (!list_marker(source[i], item_marker) || item_marker.ordered != ordered) break;
                std::string item = source[i].substr(item_marker.content);
                ++i;
                while (i < source.size() && !trim(source[i]).empty()) {
                    ListMarker next;
                    if (list_marker(source[i], next)) break;
                    if (source[i].size() < 2 || (source[i][0] != ' ' && source[i][0] != '\t')) break;
                    item += '\n' + trim(source[i++]);
                }
                bool task = extensions && item.size() >= 3 && item[0] == '[' && item[2] == ']' &&
                            (item[1] == ' ' || item[1] == 'x' || item[1] == 'X');
                out += "<li>";
                if (task) {
                    out += "<input type=\"checkbox\" disabled";
                    if (item[1] != ' ') out += " checked";
                    out += "> ";
                    item = trim(item.substr(3));
                }
                out += inline_html(item, options) + "</li>\n";
                while (i < source.size() && trim(source[i]).empty()) ++i;
            }
            out += ordered ? "</ol>\n" : "</ul>\n";
            continue;
        }

        if (extensions && i + 1 < source.size() && source[i].find('|') != std::string::npos) {
            std::vector<std::string> alignments;
            if (table_delimiter(source[i + 1], alignments)) {
                const auto header = table_cells(source[i]);
                if (header.size() == alignments.size()) {
                    out += "<table>\n<thead>\n<tr>";
                    for (std::size_t c = 0; c < header.size(); ++c) {
                        out += "<th";
                        if (!alignments[c].empty()) out += " style=\"text-align: " + alignments[c] + "\"";
                        out += ">" + inline_html(header[c], options) + "</th>";
                    }
                    out += "</tr>\n</thead>\n<tbody>\n";
                    i += 2;
                    while (i < source.size() && !trim(source[i]).empty() && source[i].find('|') != std::string::npos) {
                        auto row = table_cells(source[i++]);
                        row.resize(header.size());
                        out += "<tr>";
                        for (std::size_t c = 0; c < header.size(); ++c) {
                            out += "<td";
                            if (!alignments[c].empty()) out += " style=\"text-align: " + alignments[c] + "\"";
                            out += ">" + inline_html(row[c], options) + "</td>";
                        }
                        out += "</tr>\n";
                    }
                    out += "</tbody>\n</table>\n";
                    continue;
                }
            }
        }

        if (current.front() == '<' && current.back() == '>' && options.allow_raw_html &&
            current.find("://") == std::string::npos && current.find('@') == std::string::npos) {
            out += source[i++] + "\n";
            continue;
        }

        std::string paragraph = source[i++];
        while (i < source.size() && !trim(source[i]).empty()) {
            char next_fence_marker = 0;
            std::size_t next_fence_count = 0;
            std::string next_info;
            ListMarker next_marker;
            int next_setext = 0;
            const std::string next = trim(source[i]);
            std::size_t next_hashes = 0;
            while (next_hashes < next.size() && next[next_hashes] == '#') ++next_hashes;
            if (fence(source[i], next_fence_marker, next_fence_count, next_info) ||
                list_marker(source[i], next_marker) || (!next.empty() && next[0] == '>') ||
                thematic_break(source[i]) || (next_hashes > 0 && next_hashes <= 6 &&
                next_hashes < next.size() && next[next_hashes] == ' ') ||
                (i + 1 < source.size() && setext(source[i + 1], next_setext))) break;
            paragraph += '\n' + source[i++];
        }
        out += "<p>" + inline_html(paragraph, options) + "</p>\n";
    }
    return out;
}

} // namespace

bool format_for_extension(const std::string& extension, Format& format) {
    std::string ext = lower(extension);
    if (!ext.empty() && ext[0] != '.') ext.insert(ext.begin(), '.');
    if (ext == ".md" || ext == ".markdown" || ext == ".mdown" || ext == ".mkd") {
        format = Format::Markdown;
        return true;
    }
    if (ext == ".adoc" || ext == ".asciidoc") {
        format = Format::AsciiDoc;
        return true;
    }
    if (ext == ".rst") {
        format = Format::ReStructuredText;
        return true;
    }
    return false;
}

const char* format_name(Format format) {
    switch (format) {
    case Format::Markdown: return "Markdown";
    case Format::AsciiDoc: return "AsciiDoc";
    case Format::ReStructuredText: return "reStructuredText";
    }
    return "unknown";
}

bool is_supported(Format format) {
    return format == Format::Markdown || format == Format::AsciiDoc ||
           format == Format::ReStructuredText;
}

bool convert(Format format, const std::string& input, std::string& output,
             std::string& error, const Options& options) {
    output.clear();
    error.clear();
    if (!is_supported(format)) {
        error = std::string(format_name(format)) + " conversion is not implemented yet";
        return false;
    }

    std::string fragment;
    if (format == Format::Markdown) {
        fragment = markdown_fragment(input, options);
    } else if (format == Format::AsciiDoc) {
        asciidoc::Document document;
        if (!asciidoc::parse(input, document, error, options)) return false;
        fragment = asciidoc::render_html(document, options);
    } else {
        std::string expanded;
        if (!rst::expand(input, expanded, error, options)) return false;
        rst::Document document;
        if (!rst::parse(expanded, document, error, options)) return false;
        fragment = rst::render_html(document, options);
    }
    if (!options.standalone) {
        output = std::move(fragment);
        return true;
    }

    const std::string title = options.title.empty() ? "Document" : options.title;
    output = "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n";
    output += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    output += "<title>" + escape_html(title) + "</title>\n</head>\n<body>\n";
    output += fragment;
    output += "</body>\n</html>\n";
    return true;
}

} // namespace markup
