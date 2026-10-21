#pragma once
// Refusal inventory (CP12 phase B, test tooling only): every refusal reason a
// compilation unit would hit, not only the first one the compiler reports.
//
// The compiler stops at its first refusal (D-001 whole-unit fallback). This
// driver repeats parse + compile on a private copy of the AST, and after each
// refusal masks the construct that caused it, then compiles again, until the
// unit compiles or the inventory has to stop. Nothing is ever executed and the
// compiler is unchanged: the masks only exist in the inventory's own AST.
//
// Masking, finest first: the smallest expression that begins at the refused
// position is replaced by `null`; a refusal again at the same position masks
// the next larger expression there, then the whole innermost statement
// (removed from its block; `for` becomes `while (true)` over its body and
// `try` becomes `if (true) {try body} else {catch body}`, so the statements
// inside are still inventoried; a function declared after a statement is
// hoisted instead). Names declared by a masked statement, and targets whose
// value was masked, become "masked names".
//
// A reason is `consequential` when masking may have caused it: it recurs at an
// already-masked position, names a masked name (free names, calls), or is a
// kind/context reason at an expression containing a masked node or a masked
// name. Consequential reasons may be spurious; reports keep them apart.
// Reasons inside a masked expression's other operands, and in a `for`
// header's iterable, are not seen (the inventory is a lower bound on them).
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nift::rt {

struct InventoryItem {
    std::string reason;  // as NIFT_VM_STATS reports it (`opaque:*`, compiler reasons)
    std::uint32_t offset = 0;  // in `path`'s source (or the entry source when path is empty)
    bool consequential = false;
    std::string detail;  // postfix-receiver: the refused access's shape (refusal_mask.h)
    // CP12c slice 0: source provenance for a linked unit. Empty path means
    // the entry unit; otherwise the imported module that owns `offset`.
    std::string path;
    std::uint32_t line = 0, column = 0;
    std::string text;  // the containing source line (at most 160 bytes)
};

struct RefusalInventory {
    std::vector<InventoryItem> items;  // discovery order; items[0] is the first refusal (classify's reason)
    bool complete = true;              // false: stopped before the masked unit compiled (`stop` says why)
    std::string stop;
    std::size_t rounds = 0;            // compilations run
};

RefusalInventory inventory_refusals(std::string_view source,
                                   const std::filesystem::path& source_path = {},
                                   std::size_t max_rounds = 2048);

} // namespace nift::rt
