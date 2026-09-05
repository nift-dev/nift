#include "AsciiDoc.h"

#include <markup/Markup.h>

#include <cstddef>
#include <cctype>
#include <algorithm>
#include <set>
#include <utility>

namespace markup::asciidoc {
namespace {

constexpr std::size_t max_input_bytes = 64U * 1024U * 1024U;
constexpr std::size_t max_table_cells = 100000U;
constexpr std::size_t max_include_depth = 32U;

std::string normalize(const std::string& input);
std::vector<std::string> split_lines(const std::string& input);
bool starts_with(const std::string& value, const std::string& prefix);

std::string select_include_content(const std::string& content, const std::string& options) {
    auto lines = split_lines(normalize(content));
    std::vector<std::string> selected = lines;
    const auto lines_at = options.find("lines=");
    if (lines_at != std::string::npos) {
        const std::size_t start = lines_at + 6;
        const auto end = options.find(',', start);
        const std::string range = options.substr(start, end - start);
        const auto dots = range.find("..");
        if (dots != std::string::npos) {
            const std::size_t first = static_cast<std::size_t>(std::stoul(range.substr(0, dots)));
            const std::size_t last = static_cast<std::size_t>(std::stoul(range.substr(dots + 2)));
            selected.clear();
            if (first && last >= first)
                for (std::size_t n = first; n <= last && n <= lines.size(); ++n) selected.push_back(lines[n - 1]);
        }
    }
    const auto tag_at = options.find("tag=");
    if (tag_at != std::string::npos) {
        const std::size_t start = tag_at + 4;
        const auto end = options.find(',', start);
        const std::string tag = options.substr(start, end - start);
        const std::string open = "tag::" + tag + "[]";
        const std::string close = "end::" + tag + "[]";
        selected.clear();
        bool active = false;
        for (const auto& value : lines) {
            if (value.find(open) != std::string::npos) { active = true; continue; }
            if (value.find(close) != std::string::npos) { active = false; continue; }
            if (active) selected.push_back(value);
        }
    }
    unsigned indent = 0;
    const auto indent_at = options.find("indent=");
    if (indent_at != std::string::npos)
        indent = static_cast<unsigned>(std::stoul(options.substr(indent_at + 7)));
    std::string result;
    for (std::size_t i = 0; i < selected.size(); ++i) {
        if (indent && !selected[i].empty()) result.append(indent, ' ');
        result += selected[i];
        if (i + 1 < selected.size()) result += '\n';
    }
    return result;
}

bool expand_includes(const std::string& input, const std::string& identity,
                     const Options& options, std::vector<std::string>& stack,
                     std::size_t& bytes, std::string& output, std::string& error) {
    const auto lines = split_lines(normalize(input));
    for (std::size_t line = 0; line < lines.size(); ++line) {
        const std::string& value = lines[line];
        if (starts_with(value, "include::") && value.size() > 11 && value.back() == ']') {
            const auto bracket = value.find('[', 9);
            if (bracket == std::string::npos) {
                error = identity + ":" + std::to_string(line + 1) + ": malformed include directive";
                return false;
            }
            const std::string target = value.substr(9, bracket - 9);
            const std::string include_options = value.substr(bracket + 1, value.size() - bracket - 2);
            if (!options.asciidoc_include_resolver) {
                error = identity + ":" + std::to_string(line + 1) +
                        ": include requires a host resolver: " + target;
                return false;
            }
            if (stack.size() >= max_include_depth) {
                error = identity + ":" + std::to_string(line + 1) + ": include depth exceeds 32";
                return false;
            }
            std::string content, canonical, resolver_error;
            if (!options.asciidoc_include_resolver(identity, target, content, canonical, resolver_error)) {
                error = identity + ":" + std::to_string(line + 1) + ": " +
                        (resolver_error.empty() ? "include could not be resolved: " + target : resolver_error);
                return false;
            }
            if (canonical.empty()) {
                error = identity + ":" + std::to_string(line + 1) + ": resolver returned an empty identity";
                return false;
            }
            if (std::find(stack.begin(), stack.end(), canonical) != stack.end()) {
                error = identity + ":" + std::to_string(line + 1) + ": include cycle at " + canonical;
                return false;
            }
            if (content.size() > max_input_bytes - bytes) {
                error = identity + ":" + std::to_string(line + 1) + ": expanded input exceeds 64 MiB";
                return false;
            }
            try {
                content = select_include_content(content, include_options);
            } catch (const std::exception&) {
                error = identity + ":" + std::to_string(line + 1) + ": invalid include options";
                return false;
            }
            bytes += content.size();
            if (options.asciidoc_dependency) options.asciidoc_dependency(canonical);
            stack.push_back(canonical);
            if (!expand_includes(content, canonical, options, stack, bytes, output, error)) return false;
            stack.pop_back();
            if (line + 1 < lines.size() && (output.empty() || output.back() != '\n')) output += '\n';
        } else {
            output += value;
            if (line + 1 < lines.size()) output += '\n';
        }
    }
    return true;
}

std::string normalize(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        const unsigned char byte = static_cast<unsigned char>(input[i]);
        if (byte == 0) {
            output += "\xef\xbf\xbd";
        } else if (byte == '\r') {
            output.push_back('\n');
            if (i + 1 < input.size() && input[i + 1] == '\n') ++i;
        } else {
            output.push_back(static_cast<char>(byte));
        }
    }
    return output;
}

std::vector<std::string> split_lines(const std::string& input) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start < input.size()) {
        const auto end = input.find('\n', start);
        result.push_back(input.substr(start, end == std::string::npos
            ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

std::string substitute_attributes(const std::string& value,
                                  const std::map<std::string, std::string>& attributes) {
    std::string result;
    for (std::size_t i = 0; i < value.size();) {
        if (value[i] == '\\' && i + 1 < value.size() && value[i + 1] == '{') {
            result.push_back('{');
            i += 2;
            continue;
        }
        if (value[i] == '{') {
            const auto end = value.find('}', i + 1);
            if (end != std::string::npos) {
                const auto found = attributes.find(value.substr(i + 1, end - i - 1));
                if (found != attributes.end()) {
                    result += found->second;
                    i = end + 1;
                    continue;
                }
            }
        }
        result.push_back(value[i++]);
    }
    return result;
}

bool boundary_character(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isspace(value) || std::ispunct(value);
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.compare(0, prefix.size(), prefix) == 0;
}

std::vector<Inline> parse_inlines(const std::string& text, const Range& source) {
    std::vector<Inline> output;
    std::string plain;
    auto flush = [&]() {
        if (plain.empty()) return;
        Inline node;
        node.text = std::move(plain);
        node.source = source;
        output.push_back(std::move(node));
        plain.clear();
    };
    struct Delimiter { const char* marker; InlineKind kind; bool constrained; };
    static const Delimiter delimiters[] = {
        {"**", InlineKind::Strong, false}, {"__", InlineKind::Emphasis, false},
        {"``", InlineKind::Monospace, false}, {"##", InlineKind::Mark, false},
        {"*", InlineKind::Strong, true}, {"_", InlineKind::Emphasis, true},
        {"`", InlineKind::Monospace, true}, {"#", InlineKind::Mark, true},
        {"^", InlineKind::Superscript, true}, {"~", InlineKind::Subscript, true},
    };

    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            plain.push_back(text[i + 1]);
            i += 2;
            continue;
        }
        const auto macro = [&](const std::string& prefix, InlineKind kind,
                               std::size_t at, Inline& node, std::size_t& next) {
            if (!starts_with(text.substr(at), prefix)) return false;
            const std::size_t target_start = at + prefix.size();
            const auto bracket = text.find('[', target_start);
            if (bracket == std::string::npos || bracket == target_start) return false;
            const auto close = text.find(']', bracket + 1);
            if (close == std::string::npos) return false;
            node.kind = kind;
            node.target = text.substr(target_start, bracket - target_start);
            node.text = text.substr(bracket + 1, close - bracket - 1);
            const auto comma = node.text.find(',');
            if (comma != std::string::npos) {
                node.title = trim(node.text.substr(comma + 1));
                node.text = trim(node.text.substr(0, comma));
            }
            node.source = source;
            if (kind == InlineKind::Link || kind == InlineKind::CrossReference)
                node.children = parse_inlines(node.text.empty() ? node.target : node.text, source);
            next = close + 1;
            return true;
        };
        Inline macro_node;
        std::size_t macro_next = i;
        if (macro("link:", InlineKind::Link, i, macro_node, macro_next) ||
            macro("mailto:", InlineKind::Link, i, macro_node, macro_next) ||
            macro("image:", InlineKind::Image, i, macro_node, macro_next) ||
            macro("icon:", InlineKind::Image, i, macro_node, macro_next) ||
            macro("xref:", InlineKind::CrossReference, i, macro_node, macro_next)) {
            flush();
            if (starts_with(text.substr(i), "mailto:")) macro_node.target = "mailto:" + macro_node.target;
            if (starts_with(text.substr(i), "icon:")) macro_node.title = "icon";
            output.push_back(std::move(macro_node));
            i = macro_next;
            continue;
        }
        const auto simple_macro = [&](const std::string& prefix, InlineKind kind) {
            if (!starts_with(text.substr(i), prefix)) return false;
            const auto close = text.find(']', i + prefix.size());
            if (close == std::string::npos) return false;
            flush();
            Inline node;
            node.kind = kind;
            node.text = text.substr(i + prefix.size(), close - i - prefix.size());
            node.source = source;
            output.push_back(std::move(node));
            i = close + 1;
            return true;
        };
        if (simple_macro("kbd:[", InlineKind::Keyboard) ||
            simple_macro("btn:[", InlineKind::Button) ||
            simple_macro("menu:[", InlineKind::Menu) ||
            simple_macro("footnote:[", InlineKind::Footnote) ||
            simple_macro("pass:[", InlineKind::Passthrough)) continue;
        if (text.compare(i, 2, "<<") == 0) {
            const auto close = text.find(">>", i + 2);
            if (close != std::string::npos) {
                flush();
                Inline xref;
                xref.kind = InlineKind::CrossReference;
                std::string inside = text.substr(i + 2, close - i - 2);
                const auto comma = inside.find(',');
                xref.target = trim(inside.substr(0, comma));
                xref.text = comma == std::string::npos ? xref.target : trim(inside.substr(comma + 1));
                xref.children = parse_inlines(xref.text, source);
                xref.source = source;
                output.push_back(std::move(xref));
                i = close + 2;
                continue;
            }
        }
        if (starts_with(text.substr(i), "http://") || starts_with(text.substr(i), "https://")) {
            std::size_t end = i;
            while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) ++end;
            flush();
            Inline link;
            link.kind = InlineKind::Link;
            link.target = text.substr(i, end - i);
            link.text = link.target;
            Inline label;
            label.text = link.text;
            label.source = source;
            link.children.push_back(std::move(label));
            link.source = source;
            output.push_back(std::move(link));
            i = end;
            continue;
        }
        if (text.compare(i, 3, "(C)") == 0) { plain += "\xc2\xa9"; i += 3; continue; }
        if (text.compare(i, 3, "(R)") == 0) { plain += "\xc2\xae"; i += 3; continue; }
        if (text.compare(i, 4, "(TM)") == 0) { plain += "\xe2\x84\xa2"; i += 4; continue; }
        if (text.compare(i, 4, " -> ") == 0) { plain += " \xe2\x86\x92 "; i += 4; continue; }
        if (text.compare(i, 4, " <- ") == 0) { plain += " \xe2\x86\x90 "; i += 4; continue; }
        if (text[i] == '\n' && !plain.empty() && plain.size() >= 2 &&
            plain[plain.size() - 1] == '+' && plain[plain.size() - 2] == ' ') {
            plain.resize(plain.size() - 2);
            flush();
            Inline line_break;
            line_break.kind = InlineKind::LineBreak;
            line_break.source = source;
            output.push_back(std::move(line_break));
            ++i;
            continue;
        }

        bool matched = false;
        for (const auto& delimiter : delimiters) {
            const std::string marker = delimiter.marker;
            if (text.compare(i, marker.size(), marker) != 0) continue;
            if (delimiter.constrained && i > 0 && !boundary_character(text[i - 1])) continue;
            if (i + marker.size() >= text.size() ||
                std::isspace(static_cast<unsigned char>(text[i + marker.size()]))) continue;
            std::size_t end = text.find(marker, i + marker.size());
            while (end != std::string::npos && delimiter.constrained &&
                   end + marker.size() < text.size() &&
                   !boundary_character(text[end + marker.size()])) {
                end = text.find(marker, end + marker.size());
            }
            if (end == std::string::npos || end == i + marker.size()) continue;
            flush();
            Inline span;
            span.kind = delimiter.kind;
            span.text = text.substr(i + marker.size(), end - i - marker.size());
            span.source = source;
            if (span.kind != InlineKind::Monospace) span.children = parse_inlines(span.text, source);
            output.push_back(std::move(span));
            i = end + marker.size();
            matched = true;
            break;
        }
        if (matched) continue;
        plain.push_back(text[i++]);
    }
    flush();
    return output;
}

bool attribute_entry(const std::string& line, std::string& name,
                     std::string& value, bool& unset) {
    if (line.size() < 3 || line.front() != ':') return false;
    const auto separator = line.find(':', 1);
    if (separator == std::string::npos) return false;
    name = line.substr(1, separator - 1);
    unset = false;
    if (!name.empty() && name.front() == '!') {
        name.erase(name.begin());
        unset = true;
    } else if (!name.empty() && name.back() == '!') {
        name.pop_back();
        unset = true;
    }
    if (name.empty()) return false;
    value = trim(line.substr(separator + 1));
    return true;
}

unsigned section_level(const std::string& line, std::string& title) {
    std::size_t equals = 0;
    while (equals < line.size() && line[equals] == '=') ++equals;
    if (equals < 2 || equals > 6 || equals >= line.size() || line[equals] != ' ') return 0;
    title = trim(line.substr(equals + 1));
    return static_cast<unsigned>(equals - 1);
}

void add_text_inline(Block& block, const std::string& text,
                     std::size_t first_line, std::size_t last_line,
                     const std::map<std::string, std::string>& attributes) {
    block.text = substitute_attributes(text, attributes);
    block.source.begin = {first_line + 1, 1};
    block.source.end = {last_line + 1, text.size()};
    block.inlines = parse_inlines(block.text, block.source);
}

bool delimited_block(const std::string& line, BlockKind& kind) {
    if (line == "----") kind = BlockKind::Listing;
    else if (line == "....") kind = BlockKind::Literal;
    else if (line == "--") kind = BlockKind::Open;
    else if (line == "====") kind = BlockKind::Example;
    else if (line == "****") kind = BlockKind::Sidebar;
    else if (line == "____") kind = BlockKind::Quote;
    else if (line == "////") kind = BlockKind::Comment;
    else if (line == "++++") kind = BlockKind::Passthrough;
    else return false;
    return true;
}

bool verbatim_kind(BlockKind kind) {
    return kind == BlockKind::Listing || kind == BlockKind::Literal ||
           kind == BlockKind::Source || kind == BlockKind::Verse ||
           kind == BlockKind::Comment || kind == BlockKind::Passthrough;
}

Block parse_table_cell(const std::string& raw, std::size_t line,
                       const std::map<std::string, std::string>& attributes) {
    Block cell;
    cell.kind = BlockKind::Paragraph;
    std::string value = raw;
    const auto plus = value.find('+');
    if (plus != std::string::npos && plus > 0 && plus < 4) {
        bool digits = true;
        for (std::size_t i = 0; i < plus; ++i)
            digits = digits && std::isdigit(static_cast<unsigned char>(value[i]));
        if (digits) {
            const unsigned parsed = static_cast<unsigned>(std::stoul(value.substr(0, plus)));
            if (parsed > 0 && parsed <= 1000) cell.span = parsed;
            value.erase(0, plus + 1);
        }
    }
    if (value.size() > 2 && value[1] == ':' &&
        (value[0] == 'a' || value[0] == 'm' || value[0] == 's' || value[0] == 'e')) {
        cell.style = value.substr(0, 1);
        value.erase(0, 2);
    }
    add_text_inline(cell, trim(value), line, line, attributes);
    return cell;
}

void parse_table(const std::vector<std::string>& lines, std::size_t& line, Block& table,
                 const std::map<std::string, std::string>& attributes) {
    const std::size_t first = line++;
    table.kind = BlockKind::Table;
    table.source.begin = {first + 1, 1};
    std::size_t cells = 0;
    while (line < lines.size() && lines[line] != "|===") {
        if (lines[line].empty()) { ++line; continue; }
        Block row;
        row.kind = BlockKind::Open;
        row.source.begin = {line + 1, 1};
        const std::string& value = lines[line];
        std::size_t at = value.empty() || value.front() != '|' ? 0 : 1;
        while (at <= value.size() && cells < max_table_cells) {
            const auto separator = value.find('|', at);
            std::string raw = value.substr(at,
                separator == std::string::npos ? std::string::npos : separator - at);
            if (separator != std::string::npos && raw.size() > 1 && raw.back() == '+') {
                bool digits = true;
                for (std::size_t digit = 0; digit + 1 < raw.size(); ++digit)
                    digits = digits && std::isdigit(static_cast<unsigned char>(raw[digit]));
                if (digits) {
                    const auto following = value.find('|', separator + 1);
                    raw += value.substr(separator + 1,
                        following == std::string::npos ? std::string::npos : following - separator - 1);
                    row.items.push_back(parse_table_cell(raw, line, attributes));
                    ++cells;
                    if (following == std::string::npos) break;
                    at = following + 1;
                    continue;
                }
            }
            row.items.push_back(parse_table_cell(raw, line, attributes));
            ++cells;
            if (separator == std::string::npos) break;
            at = separator + 1;
        }
        row.source.end = {line + 1, value.size()};
        table.items.push_back(std::move(row));
        ++line;
    }
    if (line < lines.size()) ++line;
    const std::size_t last = line ? line - 1 : first;
    table.source.end = {last + 1, lines[last].size()};
}

void parse_block_attributes(const std::string& line, std::string& style) {
    if (line.size() < 2 || line.front() != '[' || line.back() != ']') return;
    const std::string inside = line.substr(1, line.size() - 2);
    const auto comma = inside.find(',');
    style = trim(inside.substr(0, comma));
}

std::string anchor_id(const std::string& line) {
    if (line.size() > 4 && starts_with(line, "[[") && line.substr(line.size() - 2) == "]] ") return {};
    if (line.size() > 4 && starts_with(line, "[[") && line.substr(line.size() - 2) == "]]" )
        return trim(line.substr(2, line.size() - 4));
    if (line.size() > 3 && starts_with(line, "[#") && line.back() == ']')
        return trim(line.substr(2, line.size() - 3));
    return {};
}

struct ListInfo {
    BlockKind kind = BlockKind::Paragraph;
    std::size_t depth = 0;
    unsigned start = 1;
    std::string marker;
    std::string principal;
};

bool list_info(const std::string& value, ListInfo& info) {
    std::size_t count = 0;
    while (count < value.size() && value[count] == '*') ++count;
    if (count && count <= 64 && count < value.size() && value[count] == ' ') {
        info.kind = BlockKind::UnorderedList;
        info.depth = count;
        info.marker = value.substr(0, count);
        info.principal = value.substr(count + 1);
        return true;
    }
    count = 0;
    while (count < value.size() && value[count] == '.') ++count;
    if (count && count <= 64 && count < value.size() && value[count] == ' ') {
        info.kind = BlockKind::OrderedList;
        info.depth = count;
        info.marker = value.substr(0, count);
        info.principal = value.substr(count + 1);
        return true;
    }
    std::size_t digits = 0;
    while (digits < value.size() && std::isdigit(static_cast<unsigned char>(value[digits]))) ++digits;
    if (digits && digits <= 9 && digits + 1 < value.size() && value[digits] == '.' && value[digits + 1] == ' ') {
        info.kind = BlockKind::OrderedList;
        info.depth = 1;
        info.start = static_cast<unsigned>(std::stoul(value.substr(0, digits)));
        info.marker = value.substr(0, digits + 1);
        info.principal = value.substr(digits + 2);
        return true;
    }
    const auto description = value.find("::");
    if (description != std::string::npos && description > 0 &&
        (description + 2 == value.size() || value[description + 2] == ' ')) {
        info.kind = BlockKind::DescriptionList;
        info.depth = 1;
        info.marker = "::";
        info.principal = value.substr(0, description);
        if (description + 2 < value.size()) {
            info.principal += '\n';
            info.principal += value.substr(description + 3);
        }
        return true;
    }
    return false;
}

void parse_list(const std::vector<std::string>& lines, std::size_t& line,
                std::size_t depth, BlockKind kind, Block& list,
                const std::map<std::string, std::string>& attributes) {
    list.kind = kind;
    list.source.begin = {line + 1, 1};
    while (line < lines.size()) {
        ListInfo info;
        if (!list_info(lines[line], info) || info.kind != kind || info.depth != depth) break;
        if (list.items.empty()) {
            list.marker = info.marker;
            list.start = info.start;
        }
        const std::size_t first = line++;
        Block item;
        item.kind = BlockKind::Paragraph;
        item.marker = info.marker;
        item.start = info.start;
        std::string principal = info.principal;
        if (kind == BlockKind::DescriptionList) {
            const auto newline = principal.find('\n');
            item.title = principal.substr(0, newline);
            principal = newline == std::string::npos ? "" : principal.substr(newline + 1);
        }
        if (principal.size() >= 3 && principal.front() == '[' && principal[2] == ']' &&
            (principal[1] == ' ' || principal[1] == 'x' || principal[1] == 'X')) {
            item.checklist = true;
            item.checked = principal[1] != ' ';
            list.checklist = true;
            principal = trim(principal.substr(3));
        }
        add_text_inline(item, principal, first, first, attributes);

        while (line < lines.size()) {
            ListInfo nested;
            if (!list_info(lines[line], nested) || nested.depth <= depth) break;
            Block child;
            parse_list(lines, line, nested.depth, nested.kind, child, attributes);
            item.blocks.push_back(std::move(child));
        }
        if (line < lines.size() && lines[line] == "+") {
            ++line;
            while (line < lines.size() && lines[line].empty()) ++line;
            if (line < lines.size()) {
                const std::size_t continuation_line = line++;
                Block continuation;
                add_text_inline(continuation, lines[continuation_line], continuation_line,
                                continuation_line, attributes);
                item.blocks.push_back(std::move(continuation));
            }
        }
        item.source.begin = {first + 1, 1};
        const std::size_t last = line ? line - 1 : first;
        item.source.end = {last + 1, lines[last].size()};
        list.items.push_back(std::move(item));
    }
    const std::size_t last = line ? line - 1 : 0;
    list.source.end = {last + 1, lines[last].size()};
}

void parse_blocks(const std::vector<std::string>& lines, std::size_t& line,
                  unsigned parent_level, std::vector<Block>& blocks,
                  const std::map<std::string, std::string>& attributes,
                  const std::string& end_delimiter = {}, std::size_t nesting = 0) {
    std::string pending_title;
    std::string pending_style;
    std::string pending_id;
    while (line < lines.size()) {
        while (line < lines.size() && lines[line].empty()) ++line;
        if (line == lines.size()) return;
        if (!end_delimiter.empty() && lines[line] == end_delimiter) {
            ++line;
            return;
        }
        const std::string id = anchor_id(lines[line]);
        if (!id.empty()) { pending_id = id; ++line; continue; }

        if (lines[line].size() > 1 && lines[line].front() == '.' && lines[line][1] != '.') {
            pending_title = substitute_attributes(lines[line++].substr(1), attributes);
            continue;
        }
        if (lines[line].size() > 1 && lines[line].front() == '[' && lines[line].back() == ']') {
            parse_block_attributes(lines[line++], pending_style);
            continue;
        }

        std::string title;
        const unsigned level = section_level(lines[line], title);
        if (level) {
            if (parent_level && level <= parent_level) return;
            const std::size_t first = line++;
            Block section;
            section.kind = BlockKind::Section;
            section.level = level;
            section.id = pending_id;
            section.title = substitute_attributes(title, attributes);
            section.source.begin = {first + 1, 1};
            parse_blocks(lines, line, level, section.blocks, attributes, end_delimiter, nesting);
            const std::size_t last = line ? line - 1 : first;
            section.source.end = {last + 1, lines[last].size()};
            section.title = substitute_attributes(section.title, attributes);
            blocks.push_back(std::move(section));
            pending_id.clear();
            continue;
        }

        if (lines[line] == "'''" || lines[line] == "<<<") {
            Block separator;
            separator.kind = lines[line] == "'''" ? BlockKind::ThematicBreak : BlockKind::PageBreak;
            separator.source.begin = separator.source.end = {line + 1, lines[line].size()};
            ++line;
            blocks.push_back(std::move(separator));
            pending_title.clear(); pending_style.clear();
            continue;
        }

        if (lines[line] == "|===") {
            Block table;
            table.title = pending_title;
            table.style = pending_style;
            table.id = pending_id;
            parse_table(lines, line, table, attributes);
            blocks.push_back(std::move(table));
            pending_title.clear(); pending_style.clear(); pending_id.clear();
            continue;
        }

        BlockKind delimiter_kind;
        if (delimited_block(lines[line], delimiter_kind)) {
            const std::string delimiter = lines[line];
            const std::size_t first = line++;
            Block block;
            block.kind = delimiter_kind;
            block.title = pending_title;
            block.style = pending_style;
            block.id = pending_id;
            if (pending_style == "source" && delimiter_kind == BlockKind::Listing) block.kind = BlockKind::Source;
            if (pending_style == "verse" && delimiter_kind == BlockKind::Quote) block.kind = BlockKind::Verse;
            block.source.begin = {first + 1, 1};
            if (verbatim_kind(block.kind)) {
                std::string text;
                const std::size_t content_first = line;
                while (line < lines.size() && lines[line] != delimiter) {
                    if (!text.empty()) text += '\n';
                    text += lines[line++];
                }
                block.text = substitute_attributes(text, attributes);
                if (!block.text.empty()) {
                    Inline literal;
                    literal.text = block.text;
                    literal.source.begin = {content_first + 1, 1};
                    literal.source.end = {line, lines[line - 1].size()};
                    block.inlines.push_back(std::move(literal));
                }
                if (line < lines.size()) ++line;
            } else if (nesting < 64) {
                parse_blocks(lines, line, parent_level, block.blocks, attributes, delimiter, nesting + 1);
            } else {
                while (line < lines.size() && lines[line] != delimiter) {
                    if (!block.text.empty()) block.text += '\n';
                    block.text += lines[line++];
                }
                if (line < lines.size()) ++line;
            }
            const std::size_t last = line ? line - 1 : first;
            block.source.end = {last + 1, lines[last].size()};
            blocks.push_back(std::move(block));
            pending_title.clear(); pending_style.clear();
            pending_id.clear();
            continue;
        }

        ListInfo first_item;
        if (list_info(lines[line], first_item)) {
            Block list;
            list.title = pending_title;
            list.style = pending_style;
            list.id = pending_id;
            parse_list(lines, line, first_item.depth, first_item.kind, list, attributes);
            blocks.push_back(std::move(list));
            pending_title.clear(); pending_style.clear();
            pending_id.clear();
            continue;
        }

        if (!lines[line].empty() && (lines[line][0] == ' ' || lines[line][0] == '\t')) {
            const std::size_t first = line;
            Block literal;
            literal.kind = BlockKind::Literal;
            literal.title = pending_title;
            literal.id = pending_id;
            while (line < lines.size() && (lines[line].empty() || lines[line][0] == ' ' || lines[line][0] == '\t')) {
                if (!literal.text.empty()) literal.text += '\n';
                literal.text += lines[line].empty() ? "" :
                    (lines[line][0] == '\t' ? lines[line].substr(1) : lines[line].substr(1));
                ++line;
            }
            literal.source.begin = {first + 1, 1};
            literal.source.end = {line, lines[line - 1].size()};
            blocks.push_back(std::move(literal));
            pending_title.clear(); pending_style.clear();
            pending_id.clear();
            continue;
        }

        const std::size_t first = line;
        std::string text = lines[line++];
        while (line < lines.size() && !lines[line].empty()) {
            std::string next_title;
            if (section_level(lines[line], next_title)) break;
            BlockKind next_kind;
            if (delimited_block(lines[line], next_kind) || lines[line] == "'''" ||
                lines[line] == "<<<" || (!end_delimiter.empty() && lines[line] == end_delimiter) ||
                (lines[line].size() > 1 && lines[line].front() == '[' && lines[line].back() == ']') ||
                (lines[line].size() > 1 && lines[line].front() == '.' && lines[line][1] != '.')) break;
            ListInfo next_list;
            if (list_info(lines[line], next_list)) break;
            text += '\n';
            text += lines[line++];
        }
        Block paragraph;
        add_text_inline(paragraph, text, first, line - 1, attributes);
        paragraph.title = pending_title;
        paragraph.style = pending_style;
        paragraph.id = pending_id;
        blocks.push_back(std::move(paragraph));
        pending_title.clear(); pending_style.clear();
        pending_id.clear();
    }
}

std::string escape_html(const std::string& value) {
    std::string output;
    output.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '&': output += "&amp;"; break;
        case '<': output += "&lt;"; break;
        case '>': output += "&gt;"; break;
        default: output.push_back(character); break;
        }
    }
    return output;
}

bool safe_uri(const std::string& uri) {
    std::string scheme;
    for (unsigned char c : uri) if (!std::isspace(c)) scheme.push_back(static_cast<char>(std::tolower(c)));
    return !starts_with(scheme, "javascript:") && !starts_with(scheme, "vbscript:") &&
           !starts_with(scheme, "data:");
}

std::string escape_attribute(const std::string& value) {
    std::string output = escape_html(value);
    std::string result;
    for (char c : output) result += c == '"' ? "&quot;" : std::string(1, c);
    return result;
}

std::string render_inlines(const std::vector<Inline>& inlines, const Options& options) {
    std::string output;
    for (const auto& node : inlines) {
        switch (node.kind) {
        case InlineKind::Text: output += escape_html(node.text); break;
        case InlineKind::Strong: output += "<strong>" + render_inlines(node.children, options) + "</strong>"; break;
        case InlineKind::Emphasis: output += "<em>" + render_inlines(node.children, options) + "</em>"; break;
        case InlineKind::Monospace: output += "<code>" + escape_html(node.text) + "</code>"; break;
        case InlineKind::Mark: output += "<mark>" + render_inlines(node.children, options) + "</mark>"; break;
        case InlineKind::Superscript: output += "<sup>" + render_inlines(node.children, options) + "</sup>"; break;
        case InlineKind::Subscript: output += "<sub>" + render_inlines(node.children, options) + "</sub>"; break;
        case InlineKind::Link: {
            const std::string target = !options.allow_raw_html && !safe_uri(node.target) ? "" : node.target;
            output += "<a href=\"" + escape_attribute(target) + "\">" +
                      render_inlines(node.children, options) + "</a>";
            break;
        }
        case InlineKind::CrossReference:
            output += "<a href=\"#" + escape_attribute(node.target) + "\">" +
                      render_inlines(node.children, options) + "</a>"; break;
        case InlineKind::Image: {
            const std::string target = !options.allow_raw_html && !safe_uri(node.target) ? "" : node.target;
            if (node.title == "icon") output += "<span class=\"icon\">" + escape_html(node.text) + "</span>";
            else output += "<img src=\"" + escape_attribute(target) + "\" alt=\"" +
                           escape_attribute(node.text) + "\">";
            break;
        }
        case InlineKind::Keyboard: output += "<kbd>" + escape_html(node.text) + "</kbd>"; break;
        case InlineKind::Button: output += "<b class=\"button\">" + escape_html(node.text) + "</b>"; break;
        case InlineKind::Menu: output += "<span class=\"menuseq\">" + escape_html(node.text) + "</span>"; break;
        case InlineKind::Footnote: output += "<span class=\"footnote\">" + escape_html(node.text) + "</span>"; break;
        case InlineKind::Passthrough:
            output += options.allow_raw_html ? node.text : escape_html(node.text); break;
        case InlineKind::LineBreak: output += "<br>\n"; break;
        default: output += escape_html(node.text); break;
        }
    }
    return output;
}

std::string render_inline_text(const std::string& value, const Options& options) {
    return render_inlines(parse_inlines(value, {}), options);
}

std::string id_attribute(const Block& block) {
    return block.id.empty() ? "" : " id=\"" + escape_attribute(block.id) + "\"";
}

std::string render_blocks(const std::vector<Block>& blocks, const Options& options) {
    std::string output;
    for (const auto& block : blocks) {
        if (block.kind == BlockKind::Paragraph) {
            if (!block.title.empty()) output += "<div class=\"title\">" + escape_html(block.title) + "</div>\n";
            output += "<div class=\"paragraph\"" + id_attribute(block) + ">\n<p>" + render_inlines(block.inlines, options) +
                      "</p>\n</div>\n";
        } else if (block.kind == BlockKind::Section) {
            output += "<div class=\"sect" + std::to_string(block.level) + "\"" + id_attribute(block) + ">\n<h" +
                      std::to_string(block.level + 1) + ">" + render_inline_text(block.title, options) + "</h" +
                      std::to_string(block.level + 1) + ">\n";
            output += render_blocks(block.blocks, options);
            output += "</div>\n";
        } else if (block.kind == BlockKind::Listing || block.kind == BlockKind::Literal ||
                   block.kind == BlockKind::Source) {
            const std::string role = block.kind == BlockKind::Source ? "source" :
                                     block.kind == BlockKind::Listing ? "listingblock" : "literalblock";
            output += "<div class=\"" + role + "\"" + id_attribute(block) + ">\n";
            if (!block.title.empty()) output += "<div class=\"title\">" + escape_html(block.title) + "</div>\n";
            output += "<pre>" + escape_html(block.text) + "</pre>\n</div>\n";
        } else if (block.kind == BlockKind::Passthrough) {
            output += options.allow_raw_html ? block.text + "\n" : escape_html(block.text) + "\n";
        } else if (block.kind == BlockKind::Sidebar || block.kind == BlockKind::Example ||
                   block.kind == BlockKind::Open) {
            const std::string role = block.kind == BlockKind::Sidebar ? "sidebarblock" :
                                     block.kind == BlockKind::Example ? "exampleblock" : "openblock";
            output += "<div class=\"" + role + "\">\n";
            if (!block.title.empty()) output += "<div class=\"title\">" + escape_html(block.title) + "</div>\n";
            output += render_blocks(block.blocks, options) + "</div>\n";
        } else if (block.kind == BlockKind::Quote || block.kind == BlockKind::Verse) {
            output += "<blockquote";
            if (block.kind == BlockKind::Verse) output += " class=\"verse\"";
            output += ">\n" + render_blocks(block.blocks, options) + "</blockquote>\n";
        } else if (block.kind == BlockKind::ThematicBreak) {
            output += "<hr>\n";
        } else if (block.kind == BlockKind::PageBreak) {
            output += "<div class=\"pagebreak\"></div>\n";
        } else if (block.kind == BlockKind::UnorderedList || block.kind == BlockKind::OrderedList) {
            const bool ordered = block.kind == BlockKind::OrderedList;
            output += ordered ? "<ol" : "<ul";
            if (ordered && block.start != 1) output += " start=\"" + std::to_string(block.start) + "\"";
            if (block.checklist) output += " class=\"checklist\"";
            output += ">\n";
            for (const auto& item : block.items) {
                output += "<li>";
                if (item.checklist) {
                    output += "<input type=\"checkbox\" disabled";
                    if (item.checked) output += " checked";
                    output += "> ";
                }
                output += render_inlines(item.inlines, options);
                if (!item.blocks.empty()) output += '\n' + render_blocks(item.blocks, options);
                output += "</li>\n";
            }
            output += ordered ? "</ol>\n" : "</ul>\n";
        } else if (block.kind == BlockKind::DescriptionList) {
            output += "<dl>\n";
            for (const auto& item : block.items) {
                output += "<dt>" + escape_html(item.title) + "</dt>\n<dd>" +
                          render_inlines(item.inlines, options);
                if (!item.blocks.empty()) output += '\n' + render_blocks(item.blocks, options);
                output += "</dd>\n";
            }
            output += "</dl>\n";
        } else if (block.kind == BlockKind::Table) {
            output += "<table" + id_attribute(block) + ">\n";
            if (!block.title.empty()) output += "<caption>" + escape_html(block.title) + "</caption>\n";
            const bool header = block.style == "options" || block.style == "header";
            for (std::size_t row_index = 0; row_index < block.items.size(); ++row_index) {
                output += "<tr>\n";
                for (const auto& cell : block.items[row_index].items) {
                    const bool heading = header && row_index == 0;
                    const std::string tag = heading ? "th" : "td";
                    output += "<" + tag;
                    if (cell.span != 1) output += " colspan=\"" + std::to_string(cell.span) + "\"";
                    output += ">" + render_inlines(cell.inlines, options) + "</" + tag + ">\n";
                }
                output += "</tr>\n";
            }
            output += "</table>\n";
        }
    }
    return output;
}

void validate_references(const std::vector<Block>& blocks, std::set<std::string>& ids,
                         std::vector<std::pair<std::string, Range>>& references,
                         std::vector<std::string>& diagnostics) {
    for (const auto& block : blocks) {
        if (!block.id.empty() && !ids.insert(block.id).second) {
            diagnostics.push_back("<input>:" + std::to_string(block.source.begin.line) +
                                  ": duplicate anchor: " + block.id);
        }
        const auto inspect = [&](const std::vector<Inline>& inlines, const auto& self) -> void {
            for (const auto& node : inlines) {
                if (node.kind == InlineKind::CrossReference)
                    references.emplace_back(node.target, node.source);
                self(node.children, self);
            }
        };
        inspect(block.inlines, inspect);
        for (const auto& item : block.items) {
            inspect(item.inlines, inspect);
            validate_references(item.blocks, ids, references, diagnostics);
        }
        validate_references(block.blocks, ids, references, diagnostics);
    }
}

} // namespace

bool parse(const std::string& input, Document& document, std::string& error,
           const Options& options) {
    document = {};
    error.clear();
    if (input.size() > max_input_bytes) {
        error = "AsciiDoc input exceeds the 64 MiB library limit";
        return false;
    }

    if (options.asciidoc_diagnostic) {
        const auto raw_lines = split_lines(normalize(input));
        static const char* unsupported[] = {"diagram::", "kroki::", "video::", "audio::"};
        for (std::size_t index = 0; index < raw_lines.size(); ++index)
            for (const char* prefix : unsupported)
                if (starts_with(raw_lines[index], prefix))
                    options.asciidoc_diagnostic(options.asciidoc_source_identity + ":" +
                        std::to_string(index + 1) + ": unsupported Asciidoctor macro: " + prefix);
    }

    std::string expanded;
    std::vector<std::string> include_stack{options.asciidoc_source_identity};
    std::size_t expanded_bytes = input.size();
    if (!expand_includes(input, options.asciidoc_source_identity, options,
                         include_stack, expanded_bytes, expanded, error)) return false;
    auto input_lines = split_lines(normalize(expanded));
    std::size_t line = 0;
    if (!input_lines.empty() && input_lines[0].rfind("= ", 0) == 0) {
        document.title = trim(input_lines[0].substr(2));
        document.attributes["doctitle"] = document.title;
        line = 1;
        if (line < input_lines.size() && !input_lines[line].empty() && input_lines[line][0] != ':') {
            document.author = input_lines[line++];
            if (line < input_lines.size() && !input_lines[line].empty() && input_lines[line][0] != ':') {
                document.revision = input_lines[line++];
            }
        }
        while (line < input_lines.size()) {
            std::string name, value;
            bool unset = false;
            if (!attribute_entry(input_lines[line], name, value, unset)) break;
            if (unset) document.attributes.erase(name);
            else document.attributes[name] = substitute_attributes(value, document.attributes);
            ++line;
        }
        document.title = substitute_attributes(document.title, document.attributes);
        document.attributes["doctitle"] = document.title;
    }
    std::vector<std::string> filtered(input_lines.begin(), input_lines.begin() + static_cast<std::ptrdiff_t>(line));
    std::vector<bool> conditions;
    bool active = true;
    for (std::size_t index = line; index < input_lines.size(); ++index) {
        const std::string& value = input_lines[index];
        const bool ifdef = starts_with(value, "ifdef::");
        const bool ifndef = starts_with(value, "ifndef::");
        if (ifdef || ifndef) {
            const auto bracket = value.find('[');
            const auto close = value.rfind(']');
            if (bracket != std::string::npos && close == value.size() - 1) {
                const std::string name = value.substr(ifdef ? 7 : 8, bracket - (ifdef ? 7 : 8));
                conditions.push_back(active);
                const bool defined = document.attributes.find(name) != document.attributes.end();
                active = active && (ifdef ? defined : !defined);
                continue;
            }
        }
        if (starts_with(value, "endif::") && !conditions.empty()) {
            active = conditions.back();
            conditions.pop_back();
            continue;
        }
        if (active) filtered.push_back(value);
    }
    input_lines = std::move(filtered);
    parse_blocks(input_lines, line, 0, document.blocks, document.attributes);
    std::set<std::string> ids;
    std::vector<std::pair<std::string, Range>> references;
    std::vector<std::string> diagnostics;
    validate_references(document.blocks, ids, references, diagnostics);
    for (const auto& reference : references) {
        if (reference.first.find('/') == std::string::npos &&
            reference.first.find('.') == std::string::npos &&
            ids.find(reference.first) == ids.end()) {
            diagnostics.push_back(options.asciidoc_source_identity + ":" +
                                  std::to_string(reference.second.begin.line) +
                                  ": unresolved cross-reference: " + reference.first);
        }
    }
    if (options.asciidoc_diagnostic)
        for (const auto& diagnostic : diagnostics) options.asciidoc_diagnostic(diagnostic);
    if (!input_lines.empty()) {
        document.source.begin = {1, 1};
        document.source.end = {input_lines.size(), input_lines.back().size()};
    }
    return true;
}

std::string render_html(const Document& document, const Options& options) {
    std::string output;
    if (!document.title.empty()) {
        output += "<div id=\"header\">\n<h1>" + render_inline_text(document.title, options) + "</h1>\n";
        if (!document.author.empty()) output += "<div class=\"details\">" + escape_html(document.author) + "</div>\n";
        output += "</div>\n";
    }
    output += render_blocks(document.blocks, options);
    return output;
}

} // namespace markup::asciidoc
