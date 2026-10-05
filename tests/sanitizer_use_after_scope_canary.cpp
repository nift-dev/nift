// Disposable sanitizer canary (build-level assertion only; never compiled into
// the Nift runtime or any behaviour test).
//
// A genuine in-function stack-use-after-scope read. With ASan's use-after-scope
// instrumentation enabled it is reported and the process exits non-zero; with
// the instrumentation disabled the read silently succeeds. `make
// test-sanitize-lifetime` compiles this with both sanitizer profiles to prove
// they are genuinely different (lifetime: instrumentation on; deep: off).
int main() {
    volatile int *p = nullptr;
    {
        volatile int local = 42;
        p = &local;
    }
    return *p == 42 ? 0 : 1;
}
