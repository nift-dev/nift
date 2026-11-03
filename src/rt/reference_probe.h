#pragma once
// B2 test configuration: the reference probe (docs/runtime-2/checkpoints/b2-refrep.md).
//
// Compiling compiler.cpp, compiler_rules.cpp and compiler_collection_sort.cpp
// with -DNIFT_RT_REFERENCE_PROBE skips the Phase A reference guard, the legacy
// location-alias refusal (`aliasing`) and the comparator sort's libstdc++
// profile refusal, so VM-level tests can execute programs that observe the
// shared-reference representation (tests/rt/refrep_vm.cpp). It is a
// compile-time configuration only: no runtime flag, environment variable,
// command-line argument, CompileLimits field or embedded API reaches it, and
// the production objects contain none of its code. A probe build defines the
// marker symbol below; `make test-rt-refrep-production` requires the nift
// binary and the shared library to lack it.
#if defined(NIFT_RT_REFERENCE_PROBE)
extern "C" const char nift_rt_reference_probe_build[];
#endif
