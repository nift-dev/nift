// CP16: the host native's successful open() must reach the Observer (read).
#include "rt/host/observer.h"
#include "rt/natives/host_ops.h"
#include "rt/string.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
struct Spy final : nift::rt::host::Observer {
    int reads = 0;
    std::size_t bytes = 0;
    void presence(const std::filesystem::path&, bool, std::string_view) override {}
    void read(const std::filesystem::path&, std::string_view value) override { ++reads; bytes = value.size(); }
    void failed_read(const std::filesystem::path&) override {}
    void stream_open(const std::filesystem::path&) override {}
    void streamed_read(const std::filesystem::path&, std::uint64_t, const std::uint64_t*) override {}
    void metadata_read(const std::filesystem::path&) override {}
    void written(const std::filesystem::path&) override {}
    void listing(const std::string&, const std::filesystem::path&) override {}
    void environment(const std::string&, const std::optional<std::string>&) override {}
    void environment_snapshot() override {}
    void platform(const std::string&) override {}
};
}

int main() {
    const std::filesystem::path path = "host-native-observer.txt";
    { std::ofstream out(path, std::ios::binary); out << "x"; }
    Spy spy;
    nift::rt::natives::Context context;
    context.host.observer = &spy;
    nift::rt::Value argument = nift::rt::make_string(path.generic_string());
    nift::rt::Value result;
    std::string error;
    const nift::rt::natives::Status status =
        nift::rt::natives::host_open(context, &argument, 1, result, error);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    if (status != nift::rt::natives::Status::Ok || spy.reads != 1 || spy.bytes != 1) {
        std::fprintf(stderr, "host observer: status=%d reads=%d error=%s\n",
                     static_cast<int>(status), spy.reads, error.c_str());
        return 1;
    }
    return 0;
}
