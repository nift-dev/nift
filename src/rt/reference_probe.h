#pragma once
// B2 test configuration: the reference probe (docs/runtime-2/checkpoints/b2-refrep.md).
//
// B5 (b5-refsem.md): the Phase A reference guard and the `aliasing` refusal are
// deleted, so arrays and objects are shared references in every build. What
// remains of the probe: compiling compiler.cpp and compiler_collection_sort.cpp
// with -DNIFT_RT_REFERENCE_PROBE skips the comparator sort's libstdc++ profile
// refusal (method:sort), so the comparator-sort fixtures run where the toolchain
// profile differs from the certified one (tests/rt/refrep_vm.cpp, the guard-off
// gate's build `nift-guardoff`, kept under its name). It is a
// compile-time configuration only: no runtime flag, environment variable,
// command-line argument, CompileLimits field or embedded API reaches it, and
// the production objects contain none of its code. A probe build defines the
// marker symbol below; `make test-rt-refrep-production` requires the nift
// binary and the shared library to lack it.
#if defined(NIFT_RT_REFERENCE_PROBE)
extern "C" const char nift_rt_reference_probe_build[];
#endif
