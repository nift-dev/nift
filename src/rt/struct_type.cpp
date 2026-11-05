#include "rt/struct_type.h"

#include "rt/atom.h"

#include <atomic>

// rt-lint: cold-file struct descriptors are created at compile time and interned on transfer decode
namespace nift::rt {

namespace {
std::atomic<std::uint64_t> next_version{1};
}

std::ptrdiff_t StructType::position(std::string_view field) const noexcept {
    for (std::size_t i = 0; i < fields.size(); ++i)
        if (fields[i] == field) return static_cast<std::ptrdiff_t>(i);
    return -1;
}

bool StructType::same_content(const StructType& other) const noexcept {
    return version == other.version && name == other.name && fields == other.fields &&
           private_fields == other.private_fields;
}

std::shared_ptr<const StructType> StructType::make(std::string name, std::vector<std::string> fields,
                                                   std::vector<bool> private_fields) {
    auto type = std::make_shared<StructType>();
    type->name = std::move(name);
    private_fields.resize(fields.size(), false);
    type->fields = std::move(fields);
    type->private_fields = std::move(private_fields);
    type->version = next_version.fetch_add(1, std::memory_order_relaxed);
    return type;
}

std::shared_ptr<const StructType> StructTypeTable::intern(const StructType& content) {
    auto type = std::make_shared<StructType>(content);
    type->private_fields.resize(type->fields.size(), false);
    for (const auto& entry : entries_)
        if (entry->same_content(*type)) return entry;
    entries_.push_back(type);
    return entries_.back();
}

StructTypeTable& StructTypeTable::current() { return AtomTable::current().struct_types(); }

} // namespace nift::rt
