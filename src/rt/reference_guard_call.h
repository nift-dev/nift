#pragma once
// The Phase A forward-compatibility guard's call handling (internal; the
// rule is in reference_guard.h): Guard::call, the lendings to calls, natives
// and their callbacks (Guard::lend, Guard::native_operands,
// Guard::tie_callbacks), user-function resolution and the `x = f(x)` idiom,
// defined in reference_guard_call.cpp on the Guard of reference_guard_walk.h.
#include "rt/reference_guard_walk.h"
