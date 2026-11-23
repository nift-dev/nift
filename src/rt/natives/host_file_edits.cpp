#include "rt/natives/host_file_edits.h"
// rt-lint: cold-file managed file buffer edits, per-call host I/O

#include "rt/natives/host_file_handles_internal.h"
#include "rt/natives/format_methods.h"

namespace nift::rt::natives {
using namespace host_handle_detail;

namespace {
std::size_t buffer_growth(std::size_t required, std::size_t capacity) {
    return required <= capacity ? 0 : std::max(required, size_add(capacity, capacity));
}

bool reserve_buffer_edit(const Context& context, const FileResource& file, std::size_t required,
                         bool changes, std::size_t scratch = 0) {
    if (changes && file.saved_is_working)
        scratch = size_add(scratch, buffer_growth(file.working.size(), file.saved.capacity()));
    scratch = size_add(scratch, buffer_growth(required, file.working.capacity()));
    return reserve_result(context, scratch);
}

void write_buffer(FileResource& file, std::string_view data) {
    const std::size_t base = std::min(file.cursor, file.working.size());
    const std::size_t overlap = std::min(data.size(), file.working.size() - base);
    if (overlap != data.size() || file.working.compare(base, overlap, data) != 0) file.prepare_mutation();
    file.working.replace(base, overlap, data);
    file.cursor = base + data.size();
    file.update_dirty();
}
} // namespace

Status file_write(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "write", file, error, status)) return status;
    if (!file->open) { error = "write: file is not open"; return Status::Error; }
    if (file->mode == "r") { error = "write: file is not open for writing"; return Status::Error; }
    if (argc != 2) { error = "write: expected one value"; return Status::Error; }
    std::string scratch;
    std::string_view data;
    if (!render_arg(args[1], scratch, data)) {
        error = "write: value is not directly renderable";
        return Status::Error;
    }
    write_buffer(*file, data);
    out = Value::null();
    return Status::Ok;
}

Status file_write_val(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                      std::string& error) {
    if (argc != 2) return fail(error, "write_val: expected one value");
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "write_val", file, error, status)) return status;
    if (!file->open) return fail(error, "write_val: file is not open");
    if (file->mode == "r") return fail(error, "write_val: file is not open for writing");

    const auto& value = args[1];
    std::size_t text_bound = 64;
    switch (value.kind()) {
    case Kind::String: {
        const auto size = string_view_of(value).size();
        // Up to six characters per byte, two quotes and the newline.
        if (size >= (kMaxResultBytes - 3) / 6) return Status::Memory;
        text_bound = size * 6 + 3;
        break;
    }
    case Kind::Decimal:
        text_bound = size_add(decimal_spelling(value).size(), 1);
        break;
    case Kind::Null: case Kind::Bool: case Kind::Int: case Kind::Double:
        break;
    default:
        return fail(error, "write_val: unsupported for this value");
    }
    if (!reserve_result(context, text_bound)) return Status::Memory;
    std::string serialized;
    serialized.reserve(text_bound);
    bool internal = false;
    if (!serialize(value, false, serialized, error, internal))
        return internal ? Status::Internal : Status::Error;
    serialized.push_back('\n');
    const auto base = std::min(file->cursor, file->working.size());
    const auto overlap = std::min(serialized.size(), file->working.size() - base);
    const auto required = size_add(base, serialized.size());
    const bool changes = overlap != serialized.size() || file->working.compare(base, overlap, serialized) != 0;
    if (!reserve_buffer_edit(context, *file, required, changes, serialized.capacity()))
        return Status::Memory;
    write_buffer(*file, serialized);
    out = Value::null();
    return Status::Ok;
}

namespace {
enum class InsertAt { End, Start, Offset, Before, After };

Status insert_text(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                   std::string& error, InsertAt where, std::string_view name) {
    const auto reject = [&](std::string_view detail) {
        error = std::string(name) + ": " + std::string(detail);
        return Status::Error;
    };
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, name, file, error, status)) return status;
    if (!file->open) return reject("file is not open");
    if (file->mode == "r") return reject("file is not open for writing");
    const bool edge = where == InsertAt::End || where == InsertAt::Start;
    if (argc != (edge ? 2u : 3u))
        return reject(edge ? "expected text" : where == InsertAt::Offset ? "expected byte position and text"
                                                                        : "expected anchor and text");
    const auto& value = args[argc - 1];
    if (!value.is(Kind::String))
        return reject(where == InsertAt::Offset ? "invalid byte position or text" : "expected string argument");
    const auto data = string_view_of(value);
    std::size_t position = where == InsertAt::End ? file->working.size() : 0;
    if (where == InsertAt::Offset) {
        if (args[1].is(Kind::Decimal)) {
            const auto size = decimal_spelling(args[1]).size();
            if (size >= (kMaxResultBytes - 512) / 8 || !reserve_result(context, size * 8 + 512))
                return Status::Memory;
        }
        if (!number_to_size(args[1], position) || position > file->working.size())
            return reject("invalid byte position or text");
    } else if (!edge) {
        if (!args[1].is(Kind::String)) return reject("expected string argument");
        const auto anchor = string_view_of(args[1]);
        if (anchor.empty()) return reject("anchor must not be empty");
        position = file->working.find(anchor);
        if (position == std::string::npos || file->working.find(anchor, position + anchor.size()) != std::string::npos)
            return reject("expected exactly one anchor");
        if (where == InsertAt::After) position += anchor.size();
    }
    if (!data.empty()) {
        if (!reserve_buffer_edit(context, *file, size_add(file->working.size(), data.size()), true))
            return Status::Memory;
        file->prepare_mutation();
        file->working.insert(position, data);
    }
    file->update_dirty();
    out = Value::null();
    return Status::Ok;
}
Status replace_text(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                    std::string& error, bool once) {
    const std::string_view name = once ? "replace_once" : "replace";
    const auto reject = [&](std::string_view detail) {
        error = std::string(name) + ": " + std::string(detail);
        return Status::Error;
    };
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, name, file, error, status)) return status;
    if (argc != 3) return reject("expected old and replacement strings");
    if (!file->open) return reject("file is not open");
    if (file->mode == "r") return reject("file is not open for writing");
    if (!args[1].is(Kind::String) || !args[2].is(Kind::String)) return reject("expected string argument");
    const auto old = string_view_of(args[1]);
    const auto replacement = string_view_of(args[2]);
    if (old.empty()) return reject("old string must not be empty");

    std::size_t count = 0;
    for (auto pos = file->working.find(old); pos != std::string::npos;
         pos = file->working.find(old, pos + old.size()))
        ++count;
    if (once && count != 1) return reject("expected exactly one match, found " + std::to_string(count));

    if (count != 0 && old != replacement) {
        std::size_t required = file->working.size();
        if (replacement.size() > old.size()) {
            const auto growth = replacement.size() - old.size();
            if (count > (kMaxResultBytes - std::min(required, kMaxResultBytes)) / growth)
                return Status::Memory;
            required += count * growth;
        }
        if (!reserve_buffer_edit(context, *file, required, true)) return Status::Memory;
        // Allocate before the first replacement so a growth failure cannot
        // leave a partially replaced working buffer.
        file->working.reserve(required);
        file->prepare_mutation();
        for (auto pos = file->working.find(old); pos != std::string::npos;
             pos = file->working.find(old, pos + replacement.size()))
            file->working.replace(pos, old.size(), replacement);
        file->cursor = std::min(file->cursor, file->working.size());
    }
    file->update_dirty();
    out = once ? Value::null() : Value::integer(static_cast<std::int64_t>(count));
    return Status::Ok;
}
} // namespace

Status file_append(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                  std::string& error) {
    return insert_text(context, args, argc, out, error, InsertAt::End, "append");
}

Status file_prepend(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                  std::string& error) {
    return insert_text(context, args, argc, out, error, InsertAt::Start, "prepend");
}

Status file_insert(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                  std::string& error) {
    return insert_text(context, args, argc, out, error, InsertAt::Offset, "insert");
}

Status file_insert_before(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                  std::string& error) {
    return insert_text(context, args, argc, out, error, InsertAt::Before, "insert_before");
}

Status file_insert_after(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                  std::string& error) {
    return insert_text(context, args, argc, out, error, InsertAt::After, "insert_after");
}

Status file_replace(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return replace_text(context, args, argc, out, error, false);
}

Status file_replace_once(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                         std::string& error) {
    return replace_text(context, args, argc, out, error, true);
}

} // namespace nift::rt::natives
