#include "rt/handles/native_access.h"

#include "rt/handles/handle_box.h"

namespace nift::rt::handles {

void describe_handle_failure(HandleStatus status, std::string_view builtin, HandleKind kind, const Value& handle,
                             const HandleTable* table, std::string& error) {
    error.assign(builtin);
    error += ": ";
    error += handle_status_text(status);
    if (status == HandleStatus::LimitExceeded && table) {
        error += " (" + std::to_string(table->capacity()) + " handles, " + kHandleLimitSetting + ")";
    } else if (status == HandleStatus::InvalidSetting && table) {
        error += ": " + std::string(kHandleLimitSetting) + "='" + table->invalid_setting() +
                 "' (expected a handle count from 1 to 4294967295, optionally with K or M)";
    }
    error += " (";
    const KindSpec* spec = table ? table->kind_spec(kind) : nullptr;
    if (spec && !spec->name.empty()) error += spec->name;
    else error += "kind " + std::to_string(static_cast<unsigned>(kind));
    if (handle.is(Kind::Handle)) {
        error += " handle ";
        error += std::to_string(static_cast<std::uint32_t>(handle.handle_id()));
        error += '/';
        error += std::to_string(handle.handle_generation());
    } else if (!handle.is_null()) {
        error += " handle expected, got ";
        error += kind_name(handle.kind());
    }
    error += ", ";
    const KindSpec* own = table && status == HandleStatus::DetachForbidden ? spec : nullptr;
    error += own && !own->detach_forbidden_code.empty() ? own->detach_forbidden_code.c_str()
                                                        : handle_status_code(status);
    error += ')';
}

HandleStatus native_handle_close(const HandleAccess& access, std::string_view builtin, const Value& arg,
                                 HandleKind kind, std::string& error, CleanupFailure* failure) {
    CleanupFailure local;
    CleanupFailure& f = failure ? *failure : local;
    const HandleStatus status = access.table ? access.table->close(arg, kind, access.context, &f)
                                             : HandleStatus::UnknownContext;
    if (status == HandleStatus::CleanupFailed) {
        // The failure's own code and text (never "alive" unless it is Failed).
        error.assign(builtin);
        error += ": " + f.message + " (";
        const KindSpec* spec = access.table->kind_spec(kind);
        error += spec && !spec->name.empty() ? spec->name : "kind " + std::to_string(static_cast<unsigned>(kind));
        error += " handle " + std::to_string(static_cast<std::uint32_t>(arg.handle_id())) + "/" +
                 std::to_string(arg.handle_generation()) + ", " + f.code + ")";
    } else if (status != HandleStatus::Ok) {
        describe_handle_failure(status, builtin, kind, arg, access.table, error);
    }
    return status;
}

HandleStatus native_handle_detach(const HandleAccess& access, std::string_view builtin, const Value& arg,
                                  HandleKind kind, std::string& error) {
    const HandleStatus status = access.table ? access.table->detach_as(arg, kind, access.context)
                                             : HandleStatus::UnknownContext;
    if (status != HandleStatus::Ok) describe_handle_failure(status, builtin, kind, arg, access.table, error);
    return status;
}

HandleStatus native_handle_result(const HandleAccess& access, std::string_view builtin, HandleKind kind,
                                  std::shared_ptr<void> resource, Value& out, std::string& error,
                                  CreateOptions options) {
    HandleStatus status = HandleStatus::UnknownContext;
    if (access.table) status = access.table->create(access.context, kind, std::move(resource), out, options);
    if (status == HandleStatus::Ok && options.counted && access.table) out = box_counted_handle(*access.table, out);
    if (status != HandleStatus::Ok) describe_handle_failure(status, builtin, kind, Value(), access.table, error);
    return status;
}

} // namespace nift::rt::handles
