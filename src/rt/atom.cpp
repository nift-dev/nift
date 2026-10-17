#include "rt/atom.h"

namespace nift::rt {

AtomTable::AtomTable() { intern(""); }

Atom AtomTable::intern(std::string_view text) {
    const auto found = ids_.find(text);
    if (found != ids_.end()) return Atom{found->second};
    // Names are heap strings that never move, so the map's string_view keys
    // stay valid as the vector grows.
    names_.push_back(std::make_unique<std::string>(text));
    const auto id = static_cast<std::uint32_t>(names_.size() - 1);
    ids_.emplace(std::string_view(*names_.back()), id);
    return Atom{id};
}

bool AtomTable::find(std::string_view text, Atom& atom) const {
    const auto found = ids_.find(text);
    if (found == ids_.end()) return false;
    atom = Atom{found->second};
    return true;
}

AtomTable& AtomTable::current() {
    thread_local AtomTable table;
    return table;
}

} // namespace nift::rt
