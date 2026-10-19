#pragma once
// Counted handle reference (RAII) over HandleTable::retain/drop: a reference
// identifies a resource and keeps it from being abandoned; it never owns it.
// For handles created with CreateOptions::counted. The VM boxes one of these
// per handle value it keeps (see the CP17 record, "VM integration").
#include "rt/handles/table.h"

#include <utility>

namespace nift::rt::handles {

class HandleRef {
public:
    HandleRef() = default;
    // Takes over the reference the caller already holds (the creator's
    // reference from a counted create(), or one from retain()).
    static HandleRef adopt(HandleTable& table, Value handle) {
        HandleRef ref;
        ref.table_ = &table;
        ref.handle_ = std::move(handle);
        return ref;
    }
    // Adds a reference; an empty HandleRef when the handle is not live and counted.
    static HandleRef share(HandleTable& table, const Value& handle) {
        return table.retain(handle) == HandleStatus::Ok ? adopt(table, handle) : HandleRef();
    }
    HandleRef(const HandleRef& other) : table_(other.table_), handle_(other.handle_) {
        if (table_ && table_->retain(handle_) != HandleStatus::Ok) { table_ = nullptr; handle_ = Value(); }
    }
    HandleRef(HandleRef&& other) noexcept : table_(other.table_), handle_(std::move(other.handle_)) {
        other.table_ = nullptr;
    }
    HandleRef& operator=(HandleRef other) noexcept {
        std::swap(table_, other.table_);
        handle_.swap(other.handle_);
        return *this;
    }
    ~HandleRef() { reset(); }

    // Drops this reference (the last one cleans the resource up, Abandoned).
    void reset() {
        if (table_) table_->drop(handle_);
        table_ = nullptr;
        handle_ = Value();
    }
    explicit operator bool() const { return table_ != nullptr; }
    const Value& value() const { return handle_; }

private:
    HandleTable* table_ = nullptr;
    Value handle_;
};

} // namespace nift::rt::handles
