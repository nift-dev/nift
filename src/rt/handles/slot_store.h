#pragma once
// Slot storage of the CP17 handle table: generation-checked identities, slot
// reuse and retirement, and the intrusive per-owner lists. Not thread-safe:
// HandleTable calls it with its lock held.
//
// Identity: a handle's 64-bit id is (table tag << 32) | slot index, its
// generation the slot's generation when issued. Generations start at 1 (0 is
// never issued) and are bumped on every release, so every handle ever issued
// for a slot has a generation below the slot's current one once released. A
// slot whose generation is UINT32_MAX is retired on release, never reused.
#include "rt/handle.h"
#include "rt/handles/types.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace nift::rt::handles {

inline constexpr std::uint32_t kNil = 0xFFFFFFFFu;

// One owner's handles in creation order (head = oldest, tail = newest).
struct OwnerList {
    std::uint32_t head = kNil, tail = kNil;
    std::size_t count = 0;
};

enum class SlotState : std::uint8_t { Free, Live, Retired };

struct Slot {
    std::shared_ptr<void> resource;
    ContextId owner = kDetached;
    std::uint64_t record = 0;        // the resource's lifecycle record
    std::uint32_t generation = 1;
    std::uint32_t refs = 0;          // counted handles only; saturates (immortal)
    std::uint32_t prev = kNil, next = kNil;
    HandleKind kind = HandleKind::None;
    SlotState state = SlotState::Free;
    bool counted = false;
};

// A resource taken out of a slot: the slot is already free (or retired) and
// every copy of the handle already fails to resolve.
struct Taken {
    Value handle;
    HandleKind kind = HandleKind::None;
    ContextId owner = kDetached;
    std::uint64_t record = 0;
    std::shared_ptr<void> resource;
};

class SlotStore {
public:
    static constexpr std::uint32_t kMaxGeneration = 0xFFFFFFFFu;
    static constexpr std::uint32_t kImmortalRefs = 0xFFFFFFFFu;

    SlotStore(std::uint32_t tag, std::uint32_t capacity) : tag_(tag), capacity_(capacity) {}

    // Classifies `handle` against `expected`; on Ok `index` is its live slot.
    HandleStatus classify(const Value& handle, HandleKind expected, std::uint32_t& index) const;
    // Allocates a live slot linked at the tail of `list`. LimitExceeded when
    // every slot up to the capacity is in use or retired (`resource` is then
    // left with the caller, so it is not destroyed under the table lock).
    HandleStatus allocate(HandleKind kind, std::shared_ptr<void>& resource, ContextId owner, bool counted,
                          OwnerList& list, Value& out);
    // Unlinks the live slot from `list`, frees or retires it and returns its
    // resource. The slot must be live.
    Taken take(std::uint32_t index, OwnerList& list);
    // Moves a live slot from one owner list to the tail of another.
    void move(std::uint32_t index, OwnerList& from, OwnerList& to, ContextId new_owner);

    Slot& slot(std::uint32_t index) { return slots_[index]; }
    const Slot& slot(std::uint32_t index) const { return slots_[index]; }
    Value handle_of(std::uint32_t index) const;
    std::uint32_t tag() const { return tag_; }
    std::size_t live() const { return live_; }
    std::size_t size() const { return slots_.size(); }
    std::size_t retired() const { return retired_; }
    std::uint32_t capacity() const { return capacity_; }

    // Test hook: sets a free slot's next generation (generation-wrap tests).
    bool set_free_generation_for_test(std::uint32_t index, std::uint32_t generation);

private:
    void link_tail(std::uint32_t index, OwnerList& list);
    void unlink(std::uint32_t index, OwnerList& list);

    std::uint32_t tag_;
    std::uint32_t capacity_;
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
    std::size_t live_ = 0;
    std::size_t retired_ = 0;
};

} // namespace nift::rt::handles
