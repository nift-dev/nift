#pragma once
// Shared helpers for the CP17 file and stream handle natives (resolution,
// handle access, arity/text helpers and HostCall reads).
#include "rt/natives/host_file_handles.h"

#include "FileSystem.h"
#include "rt/handles/file_resource.h"
#include "rt/natives/host_fs_internal.h"
#include "rt/natives/host_fs_observe.h"
#include "rt/number.h"
#include "rt/render.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>

namespace nift::rt::natives {

namespace host_handle_detail {

namespace fs = std::filesystem;
namespace host = nift::rt::host;
using handles::FileResource;
using handles::HandleAccess;
using handles::StreamResource;
using host::HostCall;
using nift::rt::HandleKind;
using nift::rt::HandleStatus;


namespace fs = std::filesystem;
namespace host = nift::rt::host;
using handles::FileResource;
using handles::HandleAccess;
using handles::StreamResource;
using host::HostCall;
using nift::rt::HandleKind;
using nift::rt::HandleStatus;

inline Status map_handle(HandleStatus status) {
    switch (nift::rt::handle_error_class(status)) {
        case nift::rt::HandleErrorClass::Recoverable: return Status::Recoverable;
        case nift::rt::HandleErrorClass::Limit: return Status::Limit;
        case nift::rt::HandleErrorClass::Error: return Status::Error;
        default: return Status::Internal;
    }
}

inline const host::PathPolicy& default_policy() {
    static const host::PathPolicy policy;
    return policy;
}

inline bool resolve_path(const Context& context, std::string_view builtin, const Value& arg, fs::path& out,
                  std::string& error, Status& status) {
    if (!arg.is(Kind::String)) {
        error = std::string(builtin) + ": expected string path";
        status = Status::Error;
        return false;
    }
    const host::PathPolicy& policy = context.host.paths ? *context.host.paths : default_policy();
    host::Resolved resolved;
    host::HostError failure;
    const host::HostStatus h = host::resolve_path(policy, string_view_of(arg), builtin, resolved, failure);
    if (h != host::HostStatus::Ok) {
        error = failure.message;
        status = h == host::HostStatus::Fatal ? Status::Error : Status::Recoverable;
        return false;
    }
    out = resolved.path;
    return true;
}

template <class T>
bool resource_arg(const Context& context, const Value* args, std::uint32_t argc, std::string_view builtin,
                  std::shared_ptr<T>& resource, std::string& error, Status& status) {
    if (argc == 0) { error = std::string(builtin) + ": handle expected"; status = Status::Error; return false; }
    const HandleStatus h =
        handles::native_handle_arg<T>(context.handles, builtin, args[0], HandleKind::File, resource, error);
    if (h != HandleStatus::Ok) { status = map_handle(h); return false; }
    return true;
}

template <class T>
bool stream_arg(const Context& context, const Value* args, std::uint32_t argc, std::string_view builtin,
                std::shared_ptr<T>& resource, std::string& error, Status& status) {
    if (argc == 0) { error = std::string(builtin) + ": handle expected"; status = Status::Error; return false; }
    const HandleStatus h =
        handles::native_handle_arg<T>(context.handles, builtin, args[0], HandleKind::Stream, resource, error);
    if (h != HandleStatus::Ok) { status = map_handle(h); return false; }
    return true;
}

inline Status create_handle(const Context& context, std::string_view builtin, HandleKind kind,
                     std::shared_ptr<void> resource, Value& out, std::string& error) {
    const HandleStatus h = handles::native_handle_result(context.handles, builtin, kind, std::move(resource),
                                                                   out, error, handles::CreateOptions{true});
    if (h != HandleStatus::Ok) return map_handle(h);
    return Status::Ok;
}

inline bool no_args(std::uint32_t argc, std::string_view name, std::string& error) {
    if (argc <= 1) return true;
    error = std::string(name) + ": expected no arguments";
    return false;
}

inline bool string_arg(const Value& value, std::string& out) {
    if (!value.is(Kind::String)) return false;
    out.assign(string_view_of(value));
    return true;
}

inline bool render_arg(const Value& value, std::string& out) {
    if (value.is(Kind::String)) { out.assign(string_view_of(value)); return true; }
    if (value.is(Kind::Bytes)) {
        const auto& bytes = *value.heap_as<BytesObject>();
        out.assign(bytes.data.begin(), bytes.data.end());
        return true;
    }
    return append_rendered(value, out);
}

inline bool read_whole_path(const Context& context, HostCall& call, std::string_view name, std::string& data,
                     std::string& error, Status& status) {
    const ReadOutcome outcome = read_whole(context, call.target().path, data, name, error);
    if (outcome == ReadOutcome::Ok) return true;
    status = host_fs_detail::read_failure(call, outcome, name, error, error);
    return false;
}

} // namespace host_handle_detail

} // namespace nift::rt::natives
