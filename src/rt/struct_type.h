#pragma once
// B3 (docs/runtime-2/checkpoints/b3-structs.md): struct type descriptors.
//
// A StructType is the immutable description of one struct declaration (one
// schema version in a session): its name, its field names in declaration
// order, which of them are private, and a version serial. It is created once
// per declaration per compiled program or session schema version and owned
// by std::shared_ptr; every StructObject references the descriptor it was
// constructed from, so privacy is never copied into instances and an
// instance keeps its own schema when a session replaces the declaration.
//
// Field names are kept as TEXT, not atoms (a documented deviation from plan
// 5.5, approved): atoms are per isolate, while a descriptor must describe the
// same type in every isolate it reaches. Instances keep their own per-field
// atoms; field i of an instance pairs with field i of its descriptor (the
// construction order), asserted in checked builds (aggregate.cpp).
//
// Isolates never share a descriptor: a transfer writes it by value and the
// receiving isolate interns it in its StructTypeTable (keyed by content,
// version included), which the isolate's AtomTable owns.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nift::rt {

struct StructType {
    std::string name;
    std::vector<std::string> fields;    // declaration order
    std::vector<bool> private_fields;   // parallel to `fields`
    std::uint64_t version = 0;          // process-unique creation serial (content key for interning)

    std::size_t size() const noexcept { return fields.size(); }
    bool is_private(std::size_t i) const noexcept { return i < private_fields.size() && private_fields[i]; }
    // The position of `field` in declaration order, or -1.
    std::ptrdiff_t position(std::string_view field) const noexcept;
    bool same_content(const StructType& other) const noexcept;

    // A new descriptor with a fresh version. `private_fields` is padded with
    // false (or truncated) to the field count.
    static std::shared_ptr<const StructType> make(std::string name, std::vector<std::string> fields,
                                                  std::vector<bool> private_fields);
};

// Per isolate (owned by its AtomTable): descriptors received from other
// isolates, one per distinct content. Never shared between isolates.
class StructTypeTable {
public:
    std::shared_ptr<const StructType> intern(const StructType& content);
    std::size_t size() const noexcept { return entries_.size(); }
    static StructTypeTable& current();  // the calling thread's isolate table

private:
    std::vector<std::shared_ptr<const StructType>> entries_;
};

} // namespace nift::rt
