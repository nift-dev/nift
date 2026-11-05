#pragma once
// B4b (docs/runtime-2/checkpoints/b4b-element-types.md): what the kind
// inference cannot follow: functions called where it does not see the call
// (C4, review H1) and containers reachable by code compiled in an earlier
// session statement (C5, review M1). Inference members (rt/infer_unit.h).
#include "rt/infer_sites.h"
