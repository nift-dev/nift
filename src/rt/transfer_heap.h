#pragma once
// Worker-isolate lifetime of cyclic decoded heap graphs.
#include "rt/value.h"
#include <vector>
namespace nift::rt {
// A worker isolate may receive cyclic wire graphs from an embedder. Retain
// those nodes until their external roots disappear, and cut remaining cycles
// only after all Values in the isolate have been destroyed. Never crosses threads.
void collect_transfer_heap() noexcept;
// Declared before a VM's owning members: runs after their Values die.
struct TransferHeapBoundary { ~TransferHeapBoundary() { collect_transfer_heap(); } };

class TransferHeapScope {
public:
    TransferHeapScope();
    ~TransferHeapScope();
    TransferHeapScope(const TransferHeapScope&) = delete;
    static void retain_cycle(const std::vector<Value>& nodes);
    void collect() noexcept;
private:
    struct Node { Value value; std::size_t incoming = 0; bool marked = false; };
    std::vector<Node> nodes_;
    TransferHeapScope* previous_;
};
}
