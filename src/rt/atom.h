#pragma once
// Atom table (CP09): interned object keys with stable integer ids. Object
// lookup compares atoms, never strings. One table per isolate (decision 7);
// `AtomTable::current()` is the calling thread's isolate table.
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
