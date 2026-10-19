#pragma once
// AST masking for the refusal inventory (refusal_inventory.h): the tree index,
// the masking ladder at a refused position and the masked-name bookkeeping.
// Test tooling only; the AST it edits is the inventory's private copy.
#include "rt/ast.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nift::rt::inventory {

class Masker {
public:
    Masker(Ast& ast, NodeId root) : ast_(ast), root_(root) {}
    // Whether a refusal at `offset` may have been caused by an earlier mask.
    // Call before mask_at for the same refusal.
    bool consequential(std::uint32_t offset, const std::string& reason);
    // Applies the next mask at `offset`; false when nothing is left to mask.
    bool mask_at(std::uint32_t offset, const std::string& reason);
    // Masks an Opaque node (statement: removed; expression: null).
    bool mask_opaque(NodeId id);
    // Reasons the last mask_at proved without a compilation (the method name
    // of a chained call refused as `postfix-receiver`); cleared by the call.
    std::vector<std::string> take_extra() { return std::move(extra_); }
    // The shape of the access the last mask_at unchained (`postfix-receiver`):
    // `<receiver>-><access>`, e.g. `method(trim)->method:upper`; else empty.
    const std::string& detail() const { return detail_; }

private:
    void reindex();
    NodeId innermost_statement(std::uint32_t offset) const;
    std::vector<NodeId> candidates(NodeId statement, std::uint32_t offset) const;
    bool maskable(NodeId id) const;
    bool within(NodeId id, NodeId ancestor) const;
    bool touches_mask(NodeId id) const;
    void null_node(NodeId id);
    void statement_action(NodeId statement);
    void replace_in_parent(NodeId statement, const std::vector<NodeId>& replacement);
    void hoist_functions();
    bool unchain(std::uint32_t offset);
    void spill_arguments(NodeId statement, NodeId call);
    void add_declared_names(NodeId statement);
    void add_name(std::string_view name) { names_.emplace(name); }
    std::size_t depth(NodeId id) const;

    Ast& ast_;
    NodeId root_;
    std::vector<NodeId> parent_;
    std::vector<char> reachable_;
    std::vector<char> statement_;  // element of a Program or Block list
    std::unordered_set<NodeId> masked_;
    std::unordered_set<std::string> names_;
    std::unordered_map<std::uint32_t, std::size_t> rung_;
    bool hoisted_ = false;
    std::vector<std::string> extra_;
    std::string detail_;
    bool spill_name_found_ = false;
    std::pair<std::uint32_t, std::uint32_t> spill_name_{0, 0};  // source span
};

} // namespace nift::rt::inventory
