#pragma once
// Non-executing expression refusal inventory and masking driver.
#include "rt/refusal_inventory.h"

namespace nift::rt {
RefusalInventory inventory_eval_refusals(std::string_view source, std::size_t max_rounds = 2048);
} // namespace nift::rt
