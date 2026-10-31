#pragma once
// The entry prefix of a program error (D-NEXT-packages, competing failures):
// the entry's statements before the one the graph's program error happens
// under, linked, so an earlier program error in execution order (an
// invocation-init read, a compile-time error) is reported instead.
#include "rt/module_link.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace nift::rt {

void link_prefix(LinkedUnit& out, const Ast& entry, NodeId root, std::string_view source,
                 const std::filesystem::path& path, const std::vector<std::string_view>& names);

} // namespace nift::rt
