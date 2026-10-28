// CP17 R2: stream peak-budget, lookahead backend failure and failed-close
// lifecycle, linked against the production natives and handle table.
#include "rt/handles/file_resource.h"
#include "rt/heap_account.h"
#include "rt/natives/host_file_handles.h"
#include "rt/natives/host_stream_handles.h"
#include "rt/string.h"
#include "rt_test.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <string>

#if !defined(__SANITIZE_ADDRESS__)
namespace {
struct alignas(std::max_align_t) Header {
    std::size_t bytes;
    bool tracked;
};
bool tracking = false;
std::size_t live = 0, peak = 0;
} // namespace

void* operator new(std::size_t n) {
    auto* h = static_cast<Header*>(std::malloc(sizeof(Header) + (n ? n : 1)));
    if (!h) throw std::bad_alloc();
    h->bytes = n;
    h->tracked = tracking;
    if (tracking) {
        live += n;
        peak = std::max(peak, live);
    }
    return h + 1;
}
void operator delete(void* p) noexcept {
    if (!p) return;
    auto* h = static_cast<Header*>(p) - 1;
    if (h->tracked) live -= h->bytes;
    std::free(h);
}
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
#endif

using namespace nift::rt;
using namespace nift::rt::handles;

namespace {

struct FailAtBoundary : std::streambuf {
    std::size_t position = 0;
    std::streamsize xsgetn(char* out, std::streamsize n) override {
        if (position == 2) throw std::ios_base::failure("injected read error");
        const std::size_t got = std::min<std::size_t>(static_cast<std::size_t>(n), 2 - position);
        std::memcpy(out, "ab" + position, got);
        position += got;
        return static_cast<std::streamsize>(got);
    }
};

std::string temp_file(const char* name, std::size_t bytes) {
    const std::string path = (std::filesystem::temp_directory_path() / name).generic_string();
    std::ofstream(path, std::ios::binary) << std::string(bytes, 'x');
    return path;
}

struct Fixture {
    HandleTable table;
    ContextId ctx = 0;
    natives::Context context;
    std::string code, error;
    Fixture() {
        KindSpec spec;
        spec.name = "stream";
        spec.cleanup = natives::cleanup_stream;
        table.register_typed_kind<StreamResource>(HandleKind::Stream, spec);
        table.open_context(ContextKind::Script, kDetached, ctx);
        context.handles = {&table, ctx};
        context.host.error_code = &code;
    }
    Value input(const std::string& path) {
        Value arg = make_string(path), out;
        CHECK(natives::ifstream_factory(context, &arg, 1, out, error) == natives::Status::Ok);
        return out;
    }
    Value output(const char* path) {
        Value arg = make_string(path), out;
        CHECK(natives::ofstream_factory(context, &arg, 1, out, error) == natives::Status::Ok);
        return out;
    }
};

} // namespace

int main() {
#if !defined(__SANITIZE_ADDRESS__)
    {
        const std::string path = temp_file("cp17-r2-peak", 1024 * 1024);
        Fixture f;
        f.context.heap_limit = 1600000;
        Value stream = f.input(path);
        Value args[2] = {stream, Value::integer(1024 * 1024)};
        Value out;
        peak = live = 0;
        tracking = true;
        const natives::Status status = natives::stream_read(f.context, args, 2, out, f.error);
        tracking = false;
        if (status != natives::Status::Ok) std::printf("line status=%d error=%s code=%s\n", int(status), f.error.c_str(), f.code.c_str());
        CHECK(status == natives::Status::Ok);
        CHECK(out.is(Kind::String) && string_view_of(out).size() == 1024 * 1024);
        CHECK(peak < f.context.heap_limit);
        std::printf("peak_read_bytes=%zu budget=%zu\n", peak, f.context.heap_limit);
        out = Value();
        natives::stream_close(f.context, &stream, 1, out, f.error);
        std::remove(path.c_str());
    }
    {
        const std::string path = temp_file("cp17-r2-line-peak", 512 * 1024);
        Fixture f;
        f.context.heap_limit = 1600000;
        Value stream = f.input(path);
        Value out;
        peak = live = 0;
        tracking = true;
        const natives::Status status = natives::stream_read_line(f.context, &stream, 1, out, f.error);
        tracking = false;
        if (status != natives::Status::Ok) std::printf("line status=%d error=%s code=%s\n", int(status), f.error.c_str(), f.code.c_str());
        CHECK(status == natives::Status::Ok);
        CHECK(out.is(Kind::String) && string_view_of(out).size() == 512 * 1024);
        CHECK(peak < f.context.heap_limit);
        out = Value();
        natives::stream_close(f.context, &stream, 1, out, f.error);
        std::remove(path.c_str());
    }
#endif
    {
        const std::string path = temp_file("cp17-r2-lookahead", 0);
        Fixture f;
        Value stream = f.input(path);
        std::shared_ptr<StreamResource> resource;
        CHECK(f.table.resolve_as<StreamResource>(stream, HandleKind::Stream, f.ctx, resource) == HandleStatus::Ok);
        FailAtBoundary injected;
        auto* saved = resource->input->std::basic_ios<char>::rdbuf(&injected);
        ::setenv("NIFT_READ_LIMIT", "2", 1);
        f.code.clear();
        Value out;
        const natives::Status status = natives::stream_read_all(f.context, &stream, 1, out, f.error);
        ::unsetenv("NIFT_READ_LIMIT");
        CHECK(status == natives::Status::Recoverable);
        CHECK(resource->input->bad());
        resource->input->std::basic_ios<char>::rdbuf(saved);
        CHECK(f.code == "stream.read_failed");
        CHECK(out.is(Kind::Null));
        stream = Value();
        std::remove(path.c_str());
    }
    if (std::filesystem::exists("/dev/full")) {
        Fixture f;
        Value out;
        Value stream = f.output("/dev/full");
        Value write_args[2] = {stream, make_string("hello")};
        CHECK(natives::stream_write(f.context, write_args, 2, out, f.error) == natives::Status::Ok);
        f.code.clear();
        const natives::Status status = natives::stream_close(f.context, &stream, 1, out, f.error);
        CHECK(status == natives::Status::Recoverable);
        CHECK(f.code == "stream.close_failed");
        std::shared_ptr<StreamResource> resource;
        CHECK(f.table.resolve_as<StreamResource>(stream, HandleKind::Stream, f.ctx, resource) == HandleStatus::Closed);
        f.code.clear();
        CHECK(natives::stream_write(f.context, write_args, 2, out, f.error) == natives::Status::Recoverable);
        CHECK(f.code == "runtime.handle_closed");
        CHECK(natives::stream_close(f.context, &stream, 1, out, f.error) == natives::Status::Ok);
        CHECK(f.table.stats().live == 0);
        const CloseReport report = f.table.close_context(f.ctx);
        CHECK(report.failures.empty());
        CHECK(f.table.stats().pending_obligations == 0);
    }
    return rt_test::finish("rt stream resource guards");
}
