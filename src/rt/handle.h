#pragma once
// Typed handles (CP09 interface; full lifetime management is CP17).
//
// A handle Value is (HandleKind, id, generation). It is never a string, so
// data can never forge one (removes legacy defect D3). A registry maps ids to
// resources; reusing a slot bumps its generation, so a stale handle fails to
// resolve instead of reaching a different resource.
#include "rt/value.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace nift::rt {

class HandleRegistry {
public:
    explicit HandleRegistry(HandleKind kind) : kind_(kind) {}
    // Stores `resource` and returns its handle.
    Value add(std::shared_ptr<void> resource);
    // Returns the resource, or null for a stale, foreign or released handle.
    std::shared_ptr<void> resolve(const Value& handle) const;
    // Releases the slot (CP17 will drive this from refcounts); later resolves
    // of the same handle fail.
    bool release(const Value& handle);
    std::size_t live() const { return live_; }

private:
    struct Slot { std::shared_ptr<void> resource; std::uint32_t generation = 0; };
    HandleKind kind_;
    std::vector<Slot> slots_;
    std::vector<std::uint64_t> free_;
    std::size_t live_ = 0;
};

} // namespace nift::rt
