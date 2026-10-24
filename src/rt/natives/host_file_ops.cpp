#include "rt/natives/host_file_ops.h"
// rt-lint: cold-file file copy/move/remove operations and the free close(handle)

#include "rt/natives/host_file_handles.h"
#include "rt/natives/host_file_handles_internal.h"
#include "rt/natives/host_stream_handles.h"

namespace nift::rt::natives {
using namespace host_handle_detail;

Status handle_close(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc != 1 || !args[0].is(Kind::Handle)) {
        error = "close: expected a file or stream handle";
        return Status::Error;
    }
    if (args[0].handle_kind() == HandleKind::File) return file_close(context, args, argc, out, error);
    if (args[0].handle_kind() == HandleKind::Stream) return stream_close(context, args, argc, out, error);
    error = "close: expected a file or stream handle";
    return Status::Error;
}

Status file_save(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "save", file, error, status)) return status;
    if (!file->open) { error = "save: file is not open"; return Status::Error; }
    if (file->mode == "r") { error = "save: file is not open for writing"; return Status::Error; }
    if (!no_args(argc, "save", error)) return Status::Error;
    HostCall call(context.host, host::HostOp::Save, "save");
    call.adopt(file->path);
    if (call.begin() != host::HostStatus::Ok) { error = call.error().message; return Status::Error; }
    file->update_dirty();
    if (!file->dirty) { call.succeed(); out = Value::null(); return Status::Ok; }
    std::error_code ec;
    const auto parent = file->path.parent_path();
    if (!parent.empty() && !fs::exists(parent, ec)) {
        error = "save: parent directory does not exist";
        if (context.host.error_code) *context.host.error_code = "io.write_failed";
        call.fail(host::HostStatus::Recoverable, host_error("io.write_failed", error));
        return Status::Recoverable;
    }
    const fs::perms perms =
        fs::exists(file->path, ec) && !ec ? fs::status(file->path, ec).permissions() : fs::perms::unknown;
    fs::path temp = file->path;
    temp += ".nift-tmp-" + std::to_string(static_cast<unsigned long long>(
                               std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "save: cannot create temporary file";
            if (context.host.error_code) *context.host.error_code = "io.write_failed";
            call.fail(host::HostStatus::Recoverable, host_error("io.write_failed", error));
            return Status::Recoverable;
        }
        output.write(file->working.data(), static_cast<std::streamsize>(file->working.size()));
        output.flush();
        if (!output) {
            output.close();
            fs::remove(temp, ec);
            error = "save: temporary write failed";
            if (context.host.error_code) *context.host.error_code = "io.write_failed";
            call.fail(host::HostStatus::Recoverable, host_error("io.write_failed", error));
            return Status::Recoverable;
        }
    }
    if (perms != fs::perms::unknown) fs::permissions(temp, perms, ec);
    if (!filesystem::replace_file_atomic(temp, file->path)) {
        error = "save: atomic replace failed";
        if (context.host.error_code) *context.host.error_code = "io.atomic_replace_failed";
        call.fail(host::HostStatus::Recoverable, host_error("io.atomic_replace_failed", error));
        return Status::Recoverable;
    }
    file->saved_is_working = true;
    file->saved.clear();
    file->dirty = false;
    file->existed_at_open = true;
    call.succeed();
    out = Value::null();
    return Status::Ok;
}

Status file_copy(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "copy", file, error, status)) return status;
    if (file->open) { error = "copy: file must be closed"; return Status::Error; }
    if (argc != 2) { error = "copy: invalid arguments"; return Status::Error; }
    fs::path dest;
    if (!resolve_path(context, "copy", args[1], dest, error, status)) return status;
    std::error_code ec;
    if (fs::is_directory(dest, ec)) dest = (dest / file->path.filename()).lexically_normal();
    host_fs_detail::observe_copy_source(context, file->path);
    HostCall call(context.host, host::HostOp::CopyTo, "copy");
    call.adopt(dest);
    if (call.begin() != host::HostStatus::Ok) { error = call.error().message; return Status::Error; }
    fs::copy_file(file->path, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        call.fail(host::HostStatus::Recoverable, host_error("io.copy_failed", "copy: " + ec.message()));
        error = "copy: " + ec.message();
        if (context.host.error_code) *context.host.error_code = "io.copy_failed";
        return Status::Recoverable;
    }
    call.succeed();
    auto copy = std::make_shared<FileResource>();
    copy->path = dest;
    return create_handle(context, "copy", HandleKind::File, copy, out, error);
}

Status file_move(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "move", file, error, status)) return status;
    if (file->open) { error = "move: file must be closed"; return Status::Error; }
    if (argc != 2) { error = "move: invalid arguments"; return Status::Error; }
    fs::path dest;
    if (!resolve_path(context, "move", args[1], dest, error, status)) return status;
    std::error_code ec;
    if (fs::is_directory(dest, ec)) dest = (dest / file->path.filename()).lexically_normal();
    HostCall to(context.host, host::HostOp::MoveTo, "move");
    HostCall from(context.host, host::HostOp::MoveFrom, "move");
    to.adopt(dest);
    from.adopt(file->path);
    if (to.begin() != host::HostStatus::Ok || from.begin() != host::HostStatus::Ok) {
        error = to.error().message.empty() ? from.error().message : to.error().message;
        return Status::Error;
    }
    bool partial = false;
    filesystem::move_path(file->path, dest, ec, &partial);
    if (ec) {
        host_fs_detail::observe_copy_source(context, file->path);
        const std::string message = partial
            ? "move: copied to " + dest.generic_string() + " but could not remove the source " +
                  file->path.generic_string() + ": " + ec.message()
            : "move: " + ec.message();
        to.fail(host::HostStatus::Recoverable, host_error("io.move_failed", message));
        from.fail(host::HostStatus::Recoverable, host_error("io.move_failed", message));
        error = message;
        if (context.host.error_code) *context.host.error_code = "io.move_failed";
        return Status::Recoverable;
    }
    to.succeed();
    from.succeed();
    host_fs_detail::observe_move_absence(context, file->path);
    file->path = dest;
    auto moved = std::make_shared<FileResource>();
    moved->path = dest;
    return create_handle(context, "move", HandleKind::File, moved, out, error);
}

Status file_remove(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "remove", file, error, status)) return status;
    if (file->open) { error = "remove: file must be closed"; return Status::Error; }
    if (!no_args(argc, "remove", error)) return Status::Error;
    std::error_code ec;
    if (fs::is_directory(file->path, ec)) {
        error = "remove: directories are not removed recursively";
        return Status::Error;
    }
    HostCall call(context.host, host::HostOp::Remove, "remove");
    call.adopt(file->path);
    if (call.begin() != host::HostStatus::Ok) { error = call.error().message; return Status::Error; }
    if (!fs::remove(file->path, ec) && ec) {
        call.fail(host::HostStatus::Recoverable, host_error("io.remove_failed", "remove: " + ec.message()));
        error = "remove: " + ec.message();
        if (context.host.error_code) *context.host.error_code = "io.remove_failed";
        return Status::Recoverable;
    }
    call.succeed();
    out = Value::null();
    return Status::Ok;
}

} // namespace nift::rt::natives
