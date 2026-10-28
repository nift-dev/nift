#pragma once
// Names the legacy engine resolves as built-ins, script bindings or callables
// with special dispatch (generated legacy table plus the CP11 hand list).
// A compiled unit may not rebind them (D-009#3); a for-loop or catch binding
// with such a name may not be called (binding_rules.h).
#include <string_view>

namespace nift::rt {

bool reserved_name(std::string_view name);

} // namespace nift::rt
