#pragma once
// Atom table (CP09): interned object keys with stable integer ids. Object
// lookup compares atoms, never strings. One table per isolate (decision 7);
// `AtomTable::current()` is the calling thread's isolate table.
//
// Budget (memory investigation C2): interning a new atom charges its bytes
// (the text plus the table's per-entry overhead, kAtomEntryOverhead) to the
// thread's live heap total (rt/heap_account.h), which every VM heap budget
// on that thread reads. Several execution contexts can share a thread and
// therefore this table: the bytes are charged once, to the context that
// interns the atom first (the one running at that moment); a later context
// that reuses the atom is not charged again, and nothing is released until
// the table is reclaimed with its context (CP23, context lifetime). Lookups
// (`find`, re-interning an existing atom) charge nothing.
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nift::rt {

struct Atom {
    std::uint32_t id = 0;  // 0 is the empty string ""
    bool operator==(Atom o) const noexcept { return id == o.id; }
    bool operator!=(Atom o) const noexcept { return id != o.id; }
};

class AtomTable {
public:
    // Per-entry bytes beyond the text's own storage: the std::string object,
    // the vector slot and the hash node (key view, id, cached hash, next
    // pointer) plus its bucket slot, rounded up.
    static constexpr std::size_t kAtomEntryOverhead = 96;
    static std::size_t entry_bytes(std::string_view text) noexcept;

    AtomTable();
    Atom intern(std::string_view text);
    // Returns false when `text` has never been interned (lookups of unknown
    // keys can then fail without growing the table).
    bool find(std::string_view text, Atom& atom) const;
    std::string_view text(Atom atom) const { return *names_[atom.id]; }
    std::size_t size() const { return names_.size(); }
    static AtomTable& current();

private:
    std::vector<std::unique_ptr<std::string>> names_;
    std::unordered_map<std::string_view, std::uint32_t> ids_;
};

inline Atom atom(std::string_view text) { return AtomTable::current().intern(text); }
inline std::string_view atom_text(Atom a) { return AtomTable::current().text(a); }

} // namespace nift::rt
