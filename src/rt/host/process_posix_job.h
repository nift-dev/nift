#pragma once
// CP21 job control for the POSIX Host process layer: stop/continue waits and
// explicit terminal ownership. The ProcessGroup operations are declared in
// process.h; this header is the concern boundary for their implementation.
#ifndef _WIN32
#include "rt/host/process.h"
#endif  // !_WIN32
