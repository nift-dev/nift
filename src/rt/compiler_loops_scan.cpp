#include "rt/compiler_loops_scan.h"

#include "rt/native.h"

// Read/write/capture scans over a default-`for` body (CP12C slice 2). The
// loop variable is a value binding, but a vanished element/key is reported at
// the first statement that reads it (legacy's lazy location reference), so
// the body is scanned for reads, writes and closure captures.

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


// Could a body change the source array's length (or an object's keys)? A plain
// non-captured local cannot be mutated by a call (arguments are copies), so
// only direct writes and methods on it matter; cells/globals/upvalues are
// conservatively treated as mutable.
struct ShrinkScan {
    const Ast& ast;
    std::string_view name;
    bool element_writes = false;
    bool found = false;

    void call(NodeId id) {
        const Node& n = ast.at(id);
        const Node& callee = ast.at(n.a);
        if (callee.kind == NodeKind::Member) {
            const Node& receiver = ast.at(callee.a);
            if (receiver.kind == NodeKind::Ident && ast.text(receiver) == name) {
                found = true;  // any method on the source may change its length
                return;
            }
        }
        const NodeId callee_id = n.a;
        const Node& callee_node = ast.at(callee_id);
        if (callee_node.kind == NodeKind::Ident) {
            NativeInfo info;
            if (!lookup_native(ast.text(callee_node), info)) found = true;  // user call may mutate a main binding
        } else if (callee_node.kind != NodeKind::Lambda) {
            found = true;  // indirect/callable-value calls are conservative too
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
            const Node& holder = ast.at(n.a);
            if (element_writes && holder.kind == NodeKind::Ident && ast.text(holder) == name) found = true;
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


bool UnitCompiler::for_source_may_shrink(const Node& body, const Binding* place, std::string_view name) const {
    if (place == nullptr) return false;  // a held expression cannot be changed by the body
    if ((place->cell && !place->selector_root_only) || place->upvalue || place->global) return true;  // conservative
    ShrinkScan scan{ast_, name};
    scan.node(node_id(body));
    return scan.found;
}

bool UnitCompiler::for_source_may_change(const Node& body, const Binding* place, std::string_view name) const {
    if (place == nullptr) return false;
    if ((place->cell && !place->selector_root_only) || place->upvalue || place->global) return true;
    ShrinkScan scan{ast_, name, true};
    scan.node(node_id(body));
    return scan.found;
}

bool UnitCompiler::for_body_captures(const Node& body, std::string_view name) const {
    CaptureScan scan{ast_, name};
    scan.node(node_id(body), false);
    return scan.found;
}

} // namespace nift::rt::detail
