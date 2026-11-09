// B09 F-2 parity probe: the observable no-runtime behaviour of the functions that
// tests/rt/parallel_isolation_stubs.cpp replaces. tests/rt/parallel_isolation_stubs_check.py
// links this once with the real production objects and once with the stubs; the outputs must
// be identical.
#include "rt/parallel_task.h"

#include <cstdio>
#include <thread>

using namespace nift::rt;

static void probe(const char* where) {
    for (ParallelWaitKind kind : {ParallelWaitKind::Blocking, ParallelWaitKind::Timer, ParallelWaitKind::Cleanup}) {
        ParallelBlock block(kind);
        std::printf("%s block kind=%d admitted=%d\n", where, static_cast<int>(kind), block.admitted() ? 1 : 0);
        {
            ParallelBlock nested(kind);  // a nested block on the same thread
            std::printf("%s nested kind=%d admitted=%d\n", where, static_cast<int>(kind), nested.admitted() ? 1 : 0);
        }
    }
    {
        ParallelBlock defaulted;
        std::printf("%s block default admitted=%d\n", where, defaulted.admitted() ? 1 : 0);
    }
    std::shared_ptr<ParallelTaskRecord> none;
    std::printf("%s identity(null)=%llu\n", where, static_cast<unsigned long long>(parallel_task_identity(none)));
    notify_parallel_wait_states(none);
    std::printf("%s notify(null) returned\n", where);
}

int main() {
    probe("main");
    std::thread worker([] { probe("thread"); });
    worker.join();
    return 0;
}
