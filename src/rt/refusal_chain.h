#pragma once
// Chained-call masking of the refusal inventory (refusal_inventory.h): the
// Masker members that unchain `a.b().c()` shapes and spill a masked call's
// arguments, defined in refusal_chain.cpp. Test tooling only.
#include "rt/refusal_mask.h"
