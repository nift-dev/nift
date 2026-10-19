// CP17 typed handles: cost of create / resolve / release, for the callgrind
// guard (tests/runtime2/cp17/handle_guards.py) and quick wall-clock checks.
//   handle-bench <mode> <iterations>
// modes: cycle   = create + resolve + release (uncounted)
//        resolve = resolve of one of 64 live handles
//        counted = create(counted) + retain + drop + drop (abandoned)
//        context = open_context + create 4 + close_context
#include "rt/handles/table.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace nift::rt;
using namespace nift::rt::handles;

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: handle-bench cycle|resolve|counted|context N\n"); return 2; }
    const char* mode = argv[1];
    const long n = std::atol(argv[2]);
    HandleTable t;
    KindSpec spec;
    spec.name = "bench";
    spec.cleanup = [](const CleanupEvent&) { return CleanupOutcome::success(); };
    t.register_typed_kind<int>(HandleKind::File, spec);
    ContextId c;
    t.open_context(ContextKind::Script, kDetached, c);
    auto resource = std::make_shared<int>(7);
    long sink = 0;
    const auto start = std::chrono::steady_clock::now();
    if (std::strcmp(mode, "cycle") == 0) {
        for (long i = 0; i < n; ++i) {
            Value h;
            t.create(c, HandleKind::File, resource, h);
            std::shared_ptr<void> out;
            t.resolve(h, HandleKind::File, c, out);
            sink += out != nullptr;
            t.release(h, HandleKind::File, c);
        }
    } else if (std::strcmp(mode, "resolve") == 0) {
        std::vector<Value> hs(64);
        for (Value& h : hs) t.create(c, HandleKind::File, resource, h);
        for (long i = 0; i < n; ++i) {
            std::shared_ptr<int> out;
            t.resolve_as<int>(hs[static_cast<std::size_t>(i) & 63], HandleKind::File, c, out);
            sink += *out;
        }
    } else if (std::strcmp(mode, "counted") == 0) {
        for (long i = 0; i < n; ++i) {
            Value h;
            t.create(c, HandleKind::File, resource, h, CreateOptions{true});
            t.retain(h);
            t.drop(h);
            sink += t.drop(h) == HandleStatus::Ok;
        }
    } else if (std::strcmp(mode, "context") == 0) {
        for (long i = 0; i < n; ++i) {
            ContextId k;
            t.open_context(ContextKind::PageRender, c, k);
            for (int j = 0; j < 4; ++j) { Value h; t.create(k, HandleKind::File, resource, h); }
            sink += static_cast<long>(t.close_context(k).cleaned);
        }
    } else {
        std::fprintf(stderr, "unknown mode %s\n", mode);
        return 2;
    }
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
    std::printf("%s: %ld iterations, %.1f ns/iteration (sink %ld)\n", mode, n, n ? ns / n : 0.0, sink);
    return 0;
}
