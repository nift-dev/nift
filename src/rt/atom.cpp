#include "rt/atom.h"

#include "rt/heap_account.h"

namespace nift::rt {

namespace { thread_local AtomTable* active_atoms = nullptr; }
AtomTable::AtomTable() { intern(""); }
AtomTable::~AtomTable() { heap_uncharge(charged_); }
AtomTableScope::AtomTableScope() : previous_(active_atoms) { active_atoms = &table_; }
AtomTableScope::~AtomTableScope() { active_atoms = previous_; }

std::size_t AtomTable::entry_bytes(std::string_view text) noexcept {
    // libstdc++ keeps up to 15 characters inside the std::string object.
    return kAtomEntryOverhead + (text.size() > 15 ? text.size() + 1 : 0);
}

Atom AtomTable::intern(std::string_view text) {
    const auto found = ids_.find(text);
    if (found != ids_.end()) return Atom{found->second};
    // Names are heap strings that never move, so the map's string_view keys
    // stay valid as the vector grows.
    names_.push_back(std::make_unique<std::string>(text));
    const auto id = static_cast<std::uint32_t>(names_.size() - 1);
    try {
        ids_.emplace(std::string_view(*names_.back()), id);
    } catch (...) {
        names_.pop_back();  // keep names_ and ids_ in step if the map cannot grow
        throw;
    }
    charged_ += entry_bytes(text);
    heap_charge(entry_bytes(text));  // C2: charged once, on the intern path only
    return Atom{id};
}

bool AtomTable::find(std::string_view text, Atom& atom) const {
    const auto found = ids_.find(text);
    if (found == ids_.end()) return false;
    atom = Atom{found->second};
    return true;
}

AtomTable& AtomTable::current() {
    if (active_atoms) return *active_atoms;
    thread_local AtomTable table;
    active_atoms = &table;
    return table;
}

} // namespace nift::rt
