#include "rt/compiler_paths.h"

#include "rt/module_link.h"
#include "rt/source_scan.h"
#include "rt/vm_mutants.h"

#include <algorithm>
#include <string>

namespace nift::rt::detail {

bool UnitCompiler::argument_path(const Node& node, std::string& note) {
    if (call_args_ == 0) return true;
    // A member/index read nested inside a larger argument expression is an
    // ordinary value read only in imported package code; entry scripts keep
    // the conservative location refusal.
    if (path_arg_ == kNoNode) {
        if (path_allow_nested_) return true;
        return fail(node, "path-in-call-argument");
    }
    const bool top = path_arg_ != kNoNode && &node == &ast_.at(path_arg_);
    if (!top && (path_chain_ == kNoNode || &node != &ast_.at(path_chain_)))
        return fail(node, "path-in-call-argument");
    const KindSet k = kinds(path_arg_);
    bool scalar = k != 0 && (k & ~kKindScalar) == 0;
    MUTATE_PATH_SCALAR(scalar);
    if (top && !scalar) return fail(node, "path-in-call-argument");
    if (module_names_ != nullptr && !module_names_->empty()) {
        NodeId root = path_arg_;  // records20k: a path rooted at a module's binding is its location
        while (ast_.at(root).kind == NodeKind::Member || ast_.at(root).kind == NodeKind::Index) root = ast_.at(root).a;
        const Node& base = ast_.at(root);
        if (base.kind == NodeKind::Ident &&
            std::find(module_names_->begin(), module_names_->end(), text(base)) != module_names_->end())
            return fail(node, "path-in-call-argument:imported");
    }
    note = std::string(text(ast_.at(path_arg_)));
    path_chain_ = node.a;
    return true;
}
// The outermost member/index chain `node` belongs to (D1, path wording):
// legacy resolves a postfix path operand as one text and quotes all of it in
// an index message (`a[5].y`: 'a[5].y'); a method call or parentheses end the
// chain. Sets the chain for `node`'s receiver, compiled next.
const Node& UnitCompiler::chain_enter(const Node& node) {
    const Node& top = chain_next_ == &node ? *chain_top_ : node;
    const std::string_view src = ast_.source();
    std::uint32_t at = ast_.at(node.a).end;
    while (at < src.size() && (src[at] == ' ' || src[at] == '\t' || src[at] == '\n' || src[at] == '\r')) ++at;
    const char joint = node.kind == NodeKind::Member ? '.' : '[';
    chain_top_ = &top;
    chain_next_ = at < src.size() && src[at] == joint ? &ast_.at(node.a) : nullptr;
    return top;
}

// D3 (path wording): legacy's textual evaluator resolves a path whose
// receiver has an index other than plain digits (`a[j].k`, `w["y"].k`,
// `a[ 1 ].k`) as one text, and a missing member is reported as `JSON value
// '<receiver>' has no member 'k'`; with digit indexes only, or on its
// prepared evaluator (loop code, functions called from it), as `value has no
// member: k`, except for a path that is a print argument by itself, which it
// resolves as text there too.
bool UnitCompiler::receiver_text_wording(const Node& node, const Node& chain) const {
    if (loop_reachable() && print_arg_ != &chain) return false;
    const std::string_view src = ast_.source();
    for (const Node* n = &ast_.at(node.a); n->kind == NodeKind::Member || n->kind == NodeKind::Index;
         n = &ast_.at(n->a)) {
        if (n->kind != NodeKind::Index) continue;
        const std::uint32_t open = detail::find_in(src, "[", ast_.at(n->a).end, n->end);
        if (open + 1 >= n->end) return true;
        const std::string_view key = src.substr(open + 1, n->end - 1 - (open + 1));
        if (key.empty() || key.find_first_not_of("0123456789") != std::string_view::npos) return true;
    }
    return false;
}

} // namespace nift::rt::detail
