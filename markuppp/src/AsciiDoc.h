#pragma once

#include <map>
#include <string>
#include <vector>

namespace markup {
struct Options;

namespace asciidoc {

struct Position { std::size_t line = 1; std::size_t column = 1; };
struct Range { Position begin; Position end; };

enum class InlineKind {
    Text, Emphasis, Strong, Monospace, Mark, Superscript, Subscript,
    Link, Image, CrossReference, Keyboard, Button, Menu, Footnote, Passthrough, LineBreak,
};

struct Inline {
    InlineKind kind = InlineKind::Text;
    std::string text;
    std::string target;
    std::string title;
    std::vector<Inline> children;
    Range source;
};

enum class BlockKind {
    Paragraph, Section, Listing, Literal, Source, Open, Example, Sidebar,
    Quote, Verse, Comment, ThematicBreak, PageBreak, UnorderedList,
    OrderedList, DescriptionList, Table, Passthrough,
};

struct Block {
    BlockKind kind = BlockKind::Paragraph;
    std::string title;
    std::string text;
    std::string style;
    std::string marker;
    std::string id;
    unsigned level = 0;
    unsigned start = 1;
    unsigned span = 1;
    bool checklist = false;
    bool checked = false;
    std::vector<Inline> inlines;
    std::vector<Block> blocks;
    std::vector<Block> items;
    Range source;
};

struct Document {
    std::string title;
    std::string author;
    std::string revision;
    std::map<std::string, std::string> attributes;
    std::vector<Block> blocks;
    Range source;
};

bool parse(const std::string& input, Document& document, std::string& error,
           const Options& options);
std::string render_html(const Document& document, const Options& options);

} // namespace asciidoc
} // namespace markup
