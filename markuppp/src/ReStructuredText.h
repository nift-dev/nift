#pragma once
#include <map>
#include <string>
#include <vector>
namespace markup { struct Options; namespace rst {
struct Position { std::size_t line=1, column=1; }; struct Range { Position begin, end; };
enum class InlineKind { Text, Emphasis, Strong, Literal, Link, Reference, Substitution, Role, Footnote };
struct Inline { InlineKind kind=InlineKind::Text; std::string text, target, role; std::vector<Inline> children; Range source; };
enum class BlockKind { Paragraph, Section, Transition, BlockQuote, Literal, LineBlock, Doctest,
 BulletList, EnumeratedList, DefinitionList, FieldList, OptionList, Footnote, Citation,
 Comment, Table, Directive, Raw, SystemMessage };
struct Block { BlockKind kind=BlockKind::Paragraph; std::string text, title, name, argument, style;
 unsigned level=0, start=1, span=1; std::vector<Inline> inlines; std::vector<Block> blocks, items; Range source; };
struct Document { std::vector<Block> blocks; std::map<std::string,std::string> targets, substitutions; Range source; };
bool parse(const std::string&, Document&, std::string&, const Options&);
bool expand(const std::string&, std::string&, std::string&, const Options&);
std::string render_html(const Document&, const Options&);
} }
