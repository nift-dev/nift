/* Test-only: report CP22_FAKE_NPROCS online CPUs so legacy's async pool
   (max(2, std::thread::hardware_concurrency())) can be sized deterministically. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>
static int fake(void) { const char* v = getenv("CP22_FAKE_NPROCS"); return v ? atoi(v) : -1; }
int get_nprocs(void) { int n = fake(); if (n > 0) return n; int (*real)(void) = (int (*)(void))dlsym(RTLD_NEXT, "get_nprocs"); return real(); }
int get_nprocs_conf(void) { int n = fake(); if (n > 0) return n; int (*real)(void) = (int (*)(void))dlsym(RTLD_NEXT, "get_nprocs_conf"); return real(); }
long sysconf(int name) {
    long (*real)(int) = (long (*)(int))dlsym(RTLD_NEXT, "sysconf");
    int n = fake();
    if (n > 0 && (name == _SC_NPROCESSORS_ONLN || name == _SC_NPROCESSORS_CONF)) return n;
    return real(name);
}
