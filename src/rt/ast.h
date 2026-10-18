#pragma once
// Runtime 2.0 CP10: one AST for Nift scripts (templates and shell command
// lines will reuse it in later checkpoints).
//
// Nodes live in one arena (a vector) and refer to each other by 32-bit index.
// Every node carries the byte span [begin, end) of the source it came from, so
// diagnostics need no copy of the text. Child lists are stored contiguously in
// a second vector. Identifier and literal text is read back from the source
// through the span; nothing is decoded or interned at parse time.
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nift::rt {

using NodeId = std::uint32_t;
inline constexpr NodeId kNoNode = 0xffffffffu;

enum class NodeKind : std::uint8_t {
    // Statements
    Program, Block, ExprStmt, Declare, Assign, CompoundAssign, IncDec, If, While, For, ForBinding,
    FnDecl, Param, StructDecl, StructField, EnumDecl, Import, Export, Return, Throw, Break, Continue,
    Try, Command, TemplateStmt, Comment, Pattern,
    // Expressions
    Number, String, StringPart, True, False, Null, Ident, Unary, Binary, Logical, Coalesce, Ternary,
    Call, Member, SafeMember, Index, SafeIndex, Array, Object, Property, Lambda, Spread, Await,
    // Text the legacy engine evaluates with semantics this parser does not
    // claim to reproduce (run-time-detected errors, irregular legacy parses).
    // A compilation unit that contains one falls back to the legacy engine
    // as a whole (D-001).
    Opaque
};

enum class Op : std::uint8_t {
    None,
    Neg, Pos, Not,                                   // unary
    Add, Sub, Mul, Div, Mod, Eq, Ne, Lt, Le, Gt, Ge,  // binary
    And, Or,                                         // logical
    AddAssign, SubAssign, MulAssign, DivAssign, ModAssign, AndAssign, OrAssign, XorAssign,
    PreInc, PreDec, PostInc, PostDec
};

// Node flags.
enum : std::uint16_t {
    kFlagConst = 1u << 0,      // Declare: const
    kFlagImmut = 1u << 1,      // Declare: immut
    kFlagAsync = 1u << 2,      // FnDecl / Lambda
    kFlagVariadic = 1u << 3,   // Param: ...rest
    kFlagBlockBody = 1u << 4,  // Lambda: { statements }
    kFlagPrivate = 1u << 5,    // StructField
    kFlagSingleQuote = 1u << 6,// String
    kFlagElseIf = 1u << 7,     // If chained as `else if`
    kFlagPairBinding = 1u << 8,// For: (k, v) : expr
    kFlagBare = 1u << 9,       // Return without a value
    kFlagMethod = 1u << 10,    // FnDecl inside a struct
    kFlagObjectPattern = 1u << 11, // Pattern: {a, b} (otherwise [a, b])
    kFlagDeferredBody = 1u << 12,  // FnDecl written as template `@fn`: body translated at call time
    kFlagTemplateImport = 1u << 13, // Import written as `@import(...)` (its run-time messages say "@import")
};

// Why an Opaque node was produced (recorded per class in cp10-parser.md).
enum class OpaqueReason : std::uint8_t {
    None,
    LegacyRuntimeError,   // legacy rejects only when the code runs
    IrregularLegacyParse, // legacy accepts but parses differently from the grammar
    TemplateText,         // @directive / $[...] statement: template front end (later checkpoint)
    StructBody,           // struct body that is not plain fields and methods
    DeferredTranslation,  // lambda body whose translation legacy rejects at call time
    NestingLimit,         // expression nesting beyond the parser's recursion bound
};

struct Node {
    NodeKind kind = NodeKind::Opaque;
    Op op = Op::None;
    std::uint16_t flags = 0;
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    NodeId a = kNoNode;     // first child (meaning depends on kind)
    NodeId b = kNoNode;     // second child
    NodeId c = kNoNode;     // third child
    std::uint32_t list = 0; // index of first entry in Ast::lists
    std::uint32_t count = 0;
    OpaqueReason reason = OpaqueReason::None;
};

class Ast {
public:
    explicit Ast(std::string_view source) : source_(source) {}
    NodeId add(NodeKind kind, std::uint32_t begin, std::uint32_t end);
    Node& at(NodeId id) { return nodes_[id]; }
    const Node& at(NodeId id) const { return nodes_[id]; }
    void set_list(NodeId id, const std::vector<NodeId>& children);
    void reserve(std::size_t nodes) {
        nodes_.reserve(nodes);
        lists_.reserve(nodes);
    }
    const NodeId* list(const Node& node) const { return lists_.data() + node.list; }
    std::string_view text(const Node& node) const { return source_.substr(node.begin, node.end - node.begin); }
    std::string_view source() const { return source_; }
    std::size_t size() const { return nodes_.size(); }
    // Number of Opaque nodes in the subtree rooted at `id`.
    std::size_t opaque_count(NodeId id) const;

    // Syntax prevalidation (CP13, D-022 #3; rt/prevalidate.h). A deferred
    // region (function, method or lambda block body) whose statement structure
    // the translator rejects becomes Opaque(DeferredTranslation) for the VM;
    // the translator's diagnostic, which legacy reports only when the region
    // runs, is kept here. Recording it never changes the tree.
    struct DeferredError {
        std::uint32_t offset = 0;
        std::string message;
    };
    void note_deferred_error(std::uint32_t offset, std::string message) {
        deferred_errors_.push_back({offset, std::move(message)});
    }
    const std::vector<DeferredError>& deferred_errors() const { return deferred_errors_; }

private:
    std::string_view source_;
    std::vector<Node> nodes_;
    std::vector<NodeId> lists_;
    std::vector<DeferredError> deferred_errors_;
};

const char* node_kind_name(NodeKind kind);
const char* op_name(Op op);
const char* opaque_reason_name(OpaqueReason reason);

// S-expression dump for tests and inspection, one node per line.
std::string dump(const Ast& ast, NodeId root);

} // namespace nift::rt
