#pragma once
// Allocation-free cycle reclamation for thread-neutral shared backing graphs.
#include <cstddef>
#include <memory>
namespace nift::rt {
// Temporary collector metadata belongs to neutral backing objects, never Values.
struct SharedGcNode {
    enum class Kind { Entry, Task } kind = Kind::Entry;
    void* object = nullptr;
    SharedGcNode* all_next = nullptr;
    SharedGcNode* work_next = nullptr;
    std::size_t incoming = 0;
    bool marked = false;
    std::shared_ptr<void> pin;
};
void shared_note_edges() noexcept;
void shared_collect() noexcept;
}
