#include "rt/compiler_element_path.h"

// Checkpoint 1 (combined-b3-b5.md section 94; owner 2026-11-10 16:35, option C).
//
// Admitted: a receiver that is a plain name; one to three levels; each key a plain name, an unsigned
// integer literal or a string literal without `.`, brackets, quotes, backslashes, braces, `$` or
// control characters (legacy reads any other key text as a member path, or fails); each level's
// receiver proved an array with a number key or an object with a string key.
//
// A store (`g[i][j] = v`, `o["k"] = v`) follows H-A and legacy: the receiver is read first and
// retained, then the value, then each key with the intermediate container it selects (GetIndex with
// Note::assign_path: legacy's assignment wording), then the final key and the checked commit
// (SetIndex; an object receiver through the protocol's object_put). Nothing is written before the
// commit, so a failed step leaves every container as it was.
//
// A compound update (`a[i] += v`) follows D2: the receiver, every key (retained when the value can
// rebind its name), the intermediate containers and the element are read before the value; then the
// operation, and the commit into the container read first, at the key read first. Legacy reads the
// element at the old key and writes at the new one: an approved difference (owner 16:35).

namespace nift::rt::detail {

namespace {
constexpr std::size_t kMaxPathDepth = 3;

bool key_char_ok(char ch) {
    const unsigned char u = static_cast<unsigned char>(ch);
    if (u < 0x20 || u == 0x7f) return false;
    switch (ch) {
        case '.': case '[': case ']': case '\\': case '"': case '\'': case '{': case '}': case '$': return false;
        default: return true;
    }
}
} // namespace

bool ElementPath::form(const UnitCompiler& c, const Node& target) {
    if (c.ast_.at(target.a).kind != NodeKind::Ident) return true;
    const KindSet receiver = c.kinds(target.a);
    return receiver != 0 && (receiver & ~kKindObject) == 0;
}

bool ElementPath::key_ok(const UnitCompiler& c, const Node& level) {
    const Node& key = c.ast_.at(level.b);
    const std::string_view spelled = c.text(key);
    bool plain = key.kind == NodeKind::Ident;
    if (key.kind == NodeKind::Number)
        plain = !spelled.empty() && spelled.find_first_not_of("0123456789") == std::string_view::npos;
    if (key.kind == NodeKind::String && key.count == 0 && spelled.size() >= 2 &&
        (spelled.front() == '"' || spelled.front() == '\'') && spelled.back() == spelled.front()) {
        plain = true;
        for (char ch : spelled.substr(1, spelled.size() - 2)) plain = plain && key_char_ok(ch);
    }
    // The bracket holds exactly that token (no parentheses around it), as element_target_ok.
    std::string_view inside =
        c.ast_.source().substr(c.ast_.at(level.a).end, level.end - c.ast_.at(level.a).end);
    while (!inside.empty() && (inside.front() == '[' || inside.front() == ' ' || inside.front() == '\t'))
        inside.remove_prefix(1);
    while (!inside.empty() && (inside.back() == ']' || inside.back() == ' ' || inside.back() == '\t'))
        inside.remove_suffix(1);
    return plain && inside == spelled;
}

bool ElementPath::levels(UnitCompiler& c, const Node& target, std::vector<const Node*>& out) {
    for (const Node* n = &target; n->kind == NodeKind::Index; n = &c.ast_.at(n->a)) out.insert(out.begin(), n);
    if (c.ast_.at(out.front()->a).kind != NodeKind::Ident || out.size() > kMaxPathDepth)
        return c.fail(target, "index-assign-target");
    if (c.text(target).find('.') != std::string_view::npos) return c.fail(target, "index-target");
    for (const Node* level : out)
        if (!key_ok(c, *level)) return c.fail(target, "index-target");
    return true;
}

bool ElementPath::kinds_ok(UnitCompiler& c, const Node& level, const Node& target) {
    const KindSet receiver = c.kinds(level.a), key = c.kinds(level.b);
    const bool array = (receiver & ~kKindArray) == 0 && (key & ~kKindNumber) == 0;
    const bool object = (receiver & ~kKindObject) == 0 && (key & ~kKindString) == 0;
    if (array || object || c.unknown_defers(receiver | key)) return true;  // Unknown is neither (review D1)
    return c.fail(target, "type:element-assignment");
}

// The receiver as UnitCompiler::assign reads it for `name[key] = v` (B5, owner Q2: `const` is
// binding-only, so a const array's elements are written; a const scalar is refused).
bool ElementPath::root(UnitCompiler& c, const Node& object, const Node& target, NodeId rhs, Root& out) {
    const Binding* holder = c.find_binding(c.text(object));
    if (holder && holder->const_scalar)
        return c.fail(target, write_refusal(holder, c.text(object), "declare-const"));
    out.global = holder != nullptr && holder->global;
    out.indirect = holder != nullptr && (holder->cell || holder->upvalue);
    if (out.global || out.indirect) out.held = *holder;
    if (!out.global &&
        !(holder && holder->cell ? c.indexed_binding(*holder, out.reg) : c.local_register(object, out.reg)))
        return c.fail(target, "index-assign-target");
    if (out.global) {
        c.fb_->set_offset(target.begin);
        if (!c.global_check_defined(out.held.reg, c.text(object))) return false;
        if (!(out.held.cell ? c.read_binding(out.held, out.reg)
                            : c.global_read_value(out.held.reg, out.reg, c.text(object))))
            return false;
    }
    return c.retain_receiver(out.reg, !out.global && !out.indirect, rhs);
}

// A captured loop binding or location source commits its cached cell, as the plain store does (no
// write-back when the value can rebind the binding: it would undo the rebinding).
bool ElementPath::write_back(UnitCompiler& c, const Root& root, NodeId rhs) {
    if (!root.indirect || root.global || !(root.held.loop_binding || root.held.location_source) || c.may_rebind(rhs))
        return true;
    std::uint16_t untyped = 0;
    if (!c.fb_->constant(Value::integer(-1), untyped)) return c.builder_ok();
    return c.store_indexed_binding(root.held, root.reg);
}

bool ElementPath::store(UnitCompiler& c, const Node& node, const std::uint16_t* result) {
    const Node& target = c.ast_.at(node.a);
    std::vector<const Node*> path;
    if (!levels(c, target, path)) return false;
    for (const Node* level : path)
        if (!kinds_ok(c, *level, target)) return false;
    const std::uint16_t mark = c.fb_->top();
    Root r;
    std::uint16_t value = 0;
    if (!root(c, c.ast_.at(path.front()->a), target, node.b, r) || !c.expr_any(node.b, value)) return false;
    std::uint16_t container = r.reg;
    for (std::size_t l = 0; l + 1 < path.size(); ++l) {
        std::uint16_t key = 0, next = 0;
        if (!c.expr_any(path[l]->b, key) || !c.temp(next)) return false;
        c.fb_->set_offset(node.begin);
        const std::size_t at = c.fb_->emit(OpCode::GetIndex, next, container, key);
        c.fb_->mark_assign_path(c.fb_->note(at, std::string(c.text(c.ast_.at(path[l]->b)))));
        container = next;
    }
    std::uint16_t key = 0;
    if (!c.expr_any(path.back()->b, key)) return false;
    c.fb_->set_offset(node.begin);
    c.fb_->note(c.fb_->emit(OpCode::SetIndex, container, key, value), std::string(c.text(c.ast_.at(path.back()->b))));
    if (!write_back(c, r, node.b)) return false;
    if (result) c.assignment_value(*result, value);  // P1: the value may be a constant operand
    c.fb_->release_to(mark);
    return c.builder_ok();
}

bool ElementPath::compound(UnitCompiler& c, const Node& node) {
    const Node& target = c.ast_.at(node.a);
    std::vector<const Node*> path;
    if (!levels(c, target, path)) return false;
    for (const Node* level : path)
        if (!kinds_ok(c, *level, target)) return false;
    // `+=` on two numbers or two strings, the others on numbers (as compound_assign; legacy's array
    // concatenation and its errors are refused).
    const KindSet l = c.kinds(node.a), rk = c.kinds(node.b);
    const bool numbers = (l & ~kKindNumber) == 0 && (rk & ~kKindNumber) == 0;
    const bool strings = node.op == Op::AddAssign && l == kKindString && rk == kKindString;
    if (!numbers && !strings && !c.unknown_defers(l | rk)) return c.fail(node, "type:compound-assignment");
    if (node.op != Op::AddAssign && node.op != Op::SubAssign && node.op != Op::MulAssign &&
        node.op != Op::DivAssign && node.op != Op::ModAssign)
        return c.fail(node, "compound-operator");
    const std::uint16_t mark = c.fb_->top();
    Root r;
    if (!root(c, c.ast_.at(path.front()->a), target, node.b, r)) return false;
    const std::string spelled(c.text(target));
    std::uint16_t container = r.reg, key = 0, element = 0, value = 0;
    for (std::size_t k = 0; k < path.size(); ++k) {
        if (!c.expr_any(path[k]->b, key) || !c.retain_receiver(key, key < mark, node.b)) return false;
        std::uint16_t next = 0;
        if (!c.temp(next)) return false;
        c.fb_->set_offset(node.begin);
        c.fb_->note(c.fb_->emit(OpCode::GetIndex, next, container, key), spelled);  // legacy's read wording
        if (k + 1 < path.size()) container = next;
        else element = next;
    }
    if (!c.expr_any(node.b, value)) return false;
    c.fb_->set_offset(node.begin);
    switch (node.op) {
        case Op::AddAssign: c.fb_->emit(OpCode::AddAssign, element, value); break;
        case Op::SubAssign: c.fb_->emit(OpCode::Sub, element, element, value); break;
        case Op::MulAssign: c.fb_->emit(OpCode::Mul, element, element, value); break;
        case Op::DivAssign: c.fb_->emit(OpCode::Div, element, element, value); break;
        default: c.fb_->emit(OpCode::Mod, element, element, value); break;
    }
    c.fb_->note(c.fb_->emit(OpCode::SetIndex, container, key, element), std::string(c.text(c.ast_.at(path.back()->b))));
    if (!write_back(c, r, node.b)) return false;
    c.fb_->release_to(mark);
    return c.builder_ok();
}

} // namespace nift::rt::detail
