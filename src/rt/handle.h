#pragma once
// Typed handles (CP09 interface; full lifetime management is CP17).
//
// A handle Value is (HandleKind, id, generation). It is never a string, so
// data can never forge one (removes legacy defect D3). A registry maps ids to
// resources; releasing a slot bumps its generation, so a stale handle fails to
// resolve instead of reaching a different resource. A slot whose generation
// has reached UINT32_MAX is retired on release (never reused), so generations
// never wrap and a stale handle can never match a later resource.
#include "rt/value.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace nift::rt {

class HandleRegistry {
public:
    explicit HandleRegistry(HandleKind kind) : kind_(kind) {}
    static constexpr std::uint32_t kMaxGeneration = 0xFFFFFFFFu;
    // Stores `resource` and returns its handle. A null resource is rejected:
    // the result is Null and no slot is used.
    Value add(std::shared_ptr<void> resource);
    // Returns the resource, or null for a stale, foreign or released handle.
    std::shared_ptr<void> resolve(const Value& handle) const;
    // Releases the slot (CP17 will drive this from refcounts); later resolves
    // of the same handle fail.
    bool release(const Value& handle);
    std::size_t live() const { return live_; }
    std::size_t retired() const { return retired_; }
    // Test hook: sets a free slot's generation so the retirement path can be
    // tested without 2^32 add/release cycles. Returns false for a live or
    // unknown slot.
    bool set_free_slot_generation_for_test(std::uint64_t id, std::uint32_t generation);

private:
    struct Slot { std::shared_ptr<void> resource; std::uint32_t generation = 0; };
    HandleKind kind_;
    std::vector<Slot> slots_;
    std::vector<std::uint64_t> free_;
    std::size_t live_ = 0;
    std::size_t retired_ = 0;
};

} // namespace nift::rt
