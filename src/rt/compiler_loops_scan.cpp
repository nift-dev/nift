#include "rt/compiler_loops_scan.h"

#include "rt/native.h"

// Read/write/capture scans over a default-`for` body (CP12C slice 2). The
// loop variable is a value binding. The body is scanned for reads, writes and
// closure captures, and for statements that may shrink a named source; B5:
// the loop iterates its entry snapshot, so an element cannot vanish any more,
// and the shrink scan only keeps the CP12C `for-capture-vanishing` and
// `for-missing-write` refusals (b5-refsem.md section 2, item 6).

namespace nift::rt::detail {

namespace {

struct ReadScan {
    const Ast& ast;
    std::string_view name;
    bool found = false;
    std::uint32_t first = 0;

    void seen(const Node& n) {
        if (!found || n.begin < first) first = n.begin;
        found = true;
    }

    void node(NodeId id, bool target) {
        if (id == kNoNode) return;
        const Node& n = ast.at(id);
        switch (n.kind) {
            case NodeKind::Ident:
                if (!target && ast.text(n) == name) seen(n);
                return;
            case NodeKind::Declare:
            case NodeKind::Assign:
                node(n.a, true);
                node(n.b, false);
                return;
            case NodeKind::CompoundAssign:
            case NodeKind::IncDec:
                node(n.a, false);  // a compound target is also read
                node(n.b, false);
                return;
            case NodeKind::Member:
                node(n.a, false);
                return;  // n.b is the member name
            case NodeKind::Property:
                node(n.b, false);
                return;  // n.a is the literal key
            case NodeKind::StructField:
                node(n.b, false);
                return;  // n.a is the field name
            case NodeKind::Param:
            case NodeKind::FnDecl:
            case NodeKind::StructDecl:
            case NodeKind::EnumDecl:
            case NodeKind::ForBinding:
                return;
            case NodeKind::Lambda:
                node(n.b, false);
                return;
            case NodeKind::For: {
                const Node& h = ast.at(n.a);
                node(h.a, false);
                node(n.b, false);
                return;
            }
            default: break;
        }
        node(n.a, false);
        node(n.b, false);
        node(n.c, false);
        const NodeId* list = ast.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) node(list[k], false);
    }
};

struct WriteScan {
    const Ast& ast;
    std::string_view name;
    bool found = false;

    void target(NodeId id) {
        if (id == kNoNode) return;
        const Node& n = ast.at(id);
        if (n.kind == NodeKind::Ident && ast.text(n) == name) {
            found = true;
            return;
        }
        if (n.kind == NodeKind::Index) {  // x[i] = ... writes an element, not x
            node(n.a);
            node(n.b);
            return;
        }
        if (n.kind == NodeKind::Member) {
            node(n.a);
            return;
        }
        node(id);
    }

    void node(NodeId id) {
        if (id == kNoNode || found) return;
        const Node& n = ast.at(id);
        switch (n.kind) {
            case NodeKind::Declare:
            case NodeKind::Assign:
            case NodeKind::CompoundAssign:
            case NodeKind::IncDec:
                target(n.a);
                node(n.b);
                return;
            case NodeKind::Param: case NodeKind::FnDecl: case NodeKind::StructDecl:
            case NodeKind::EnumDecl: case NodeKind::ForBinding: return;
            case NodeKind::Lambda: node(n.b); return;
            default: break;
        }
        for (NodeId child : {n.a, n.b, n.c}) node(child);
        const NodeId* list = ast.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) node(list[k]);
    }
};

struct CaptureScan {
    const Ast& ast;
    std::string_view name;
    bool found = false;

    void node(NodeId id, bool in_lambda) {
        if (id == kNoNode) return;
        const Node& n = ast.at(id);
        if (n.kind == NodeKind::Ident) {
            if (in_lambda && ast.text(n) == name) found = true;
            return;
        }
        if (n.kind == NodeKind::Lambda) {
            node(n.b, true);
            return;
        }
        if (n.kind == NodeKind::Param || n.kind == NodeKind::FnDecl || n.kind == NodeKind::StructDecl ||
            n.kind == NodeKind::EnumDecl || n.kind == NodeKind::ForBinding)
            return;
        for (NodeId child : {n.a, n.b, n.c}) node(child, in_lambda);
        const NodeId* list = ast.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) node(list[k], in_lambda);
    }
};

} // namespace


// Could a body remove elements or replace the source? Unknown calls may
// mutate an alias. Cells, globals and upvalues retain the conservative result.
struct ShrinkScan {
    const Ast& ast;
    const UnitKinds& kinds;
    std::string_view name;
    std::string_view captured_element;
    bool found = false;

    bool read_only_expression(NodeId id) const {
        const Node& n = ast.at(id);
        switch (n.kind) {
            case NodeKind::Ident: case NodeKind::Number: case NodeKind::True:
            case NodeKind::False: case NodeKind::Null: return true;
            case NodeKind::String: return n.count == 0;
            case NodeKind::Member: return read_only_expression(n.a);
            case NodeKind::Index: case NodeKind::Binary:
                return read_only_expression(n.a) && read_only_expression(n.b);
            default: return false;
        }
    }

    bool read_only_callable(NodeId id) const {
        const auto unknown = kinds.callable_unknown.find(id);
        if (kinds.of(id) != kKindFunction || unknown == kinds.callable_unknown.end() || unknown->second)
            return false;
        const auto targets = kinds.callable_targets.find(id);
        if (targets == kinds.callable_targets.end() || targets->second.empty()) return false;
        for (NodeId target : targets->second) {
            const Node& lambda = ast.at(target);
            if (lambda.kind != NodeKind::Lambda || (lambda.flags & (kFlagBlockBody | kFlagAsync)) ||
                !read_only_expression(lambda.b)) return false;
        }
        return true;
    }

    void call(NodeId id) {
        const Node& n = ast.at(id);
        const Node& callee = ast.at(n.a);
        if (callee.kind == NodeKind::Member) {
            // These array methods cannot remove elements. Map and sort
            // require a known read-only callback. All operands are scanned
            // for effects before the method can run.
            const auto method = ast.text(ast.at(callee.b));
            const bool push = method == "push";  // rt-lint: allow-string-compare compile time
            if (push && n.count == 1 && !captured_element.empty()) {
                const NodeId value = ast.list(n)[0];
                // A stored closure may escape this iteration. Preserve M-3
                // for this transfer even if the receiver only grows.
                if (ast.at(value).kind == NodeKind::Lambda) {
                    CaptureScan capture{ast, captured_element};
                    capture.node(value, false);
                    if (capture.found) { found = true; return; }
                }
            }
            const bool nonshrinking = kinds.of(callee.a) == kKindArray &&
                (push ||
                 (method == "stringify" && n.count == 0) ||  // rt-lint: allow-string-compare compile time
                 ((method == "sort_by" || method == "map") &&  // rt-lint: allow-string-compare compile time
                  n.count == 1 && read_only_callable(ast.list(n)[0])));
            if (!nonshrinking) { found = true; return; }
        } else if (callee.kind == NodeKind::Ident) {
            NativeInfo info;
            if (!lookup_native(ast.text(callee), info) && !read_only_callable(n.a)) { found = true; return; }
        } else if (callee.kind != NodeKind::Lambda) {
            found = true;
            return;
        }
        node(n.a);
        const NodeId* args = ast.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) node(args[k]);
    }

    void target(NodeId id) {
        if (id == kNoNode) return;
        const Node& n = ast.at(id);
        if (n.kind == NodeKind::Ident && ast.text(n) == name) { found = true; return; }
        if (n.kind == NodeKind::Member) { node(n.a); return; }
        if (n.kind == NodeKind::Index) {
            node(n.a);
            node(n.b);
            return;
        }
        node(id);
    }

    void node(NodeId id) {
        if (id == kNoNode || found) return;
        const Node& n = ast.at(id);
        switch (n.kind) {
            case NodeKind::Call: call(id); return;
            case NodeKind::Declare:
            case NodeKind::Assign:
            case NodeKind::CompoundAssign:
            case NodeKind::IncDec:
                target(n.a); node(n.b); return;
            case NodeKind::Param: case NodeKind::FnDecl: case NodeKind::StructDecl:
            case NodeKind::EnumDecl: case NodeKind::ForBinding: return;
            default: break;
        }
        for (NodeId child : {n.a, n.b, n.c}) node(child);
        const NodeId* list = ast.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) node(list[k]);
    }
};

bool UnitCompiler::for_body_reads(const Node& body, std::string_view name, std::uint32_t& first_offset) const {
    ReadScan scan{ast_, name};
    scan.node(node_id(body), false);
    first_offset = scan.first;
    return scan.found;
}

bool UnitCompiler::for_body_writes(const Node& body, std::string_view name) const {
    WriteScan scan{ast_, name};
    scan.node(node_id(body));
    return scan.found;
}


bool UnitCompiler::for_source_may_shrink(const Node& body, const Binding* place, std::string_view name,
                                         std::string_view captured_element) const {
    if (place == nullptr) return false;  // a held expression cannot be changed by the body
    if ((place->cell && !place->selector_root_only) || place->upvalue || place->global) return true;  // conservative
    ShrinkScan scan{ast_, kinds_, name, captured_element};
    scan.node(node_id(body));
    return scan.found;
}

bool UnitCompiler::for_body_captures(const Node& body, std::string_view name) const {
    CaptureScan scan{ast_, name};
    scan.node(node_id(body), false);
    return scan.found;
}

} // namespace nift::rt::detail
