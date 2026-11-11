#include "rt/natives/host_std_stream.h"
// rt-lint: cold-file std-stream identity and end-of-run stream tail (output-order repair)

#include "rt/natives/builtin.h"

#include <cstdio>
#include <mutex>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace nift::rt::natives {

#if defined(_WIN32)
namespace {
bool same_file(HANDLE a, HANDLE b) {
    BY_HANDLE_FILE_INFORMATION x{}, y{};
    return a != INVALID_HANDLE_VALUE && b != nullptr && b != INVALID_HANDLE_VALUE &&
           GetFileInformationByHandle(a, &x) && GetFileInformationByHandle(b, &y) &&
           x.dwVolumeSerialNumber == y.dwVolumeSerialNumber && x.nFileIndexHigh == y.nFileIndexHigh &&
           x.nFileIndexLow == y.nFileIndexLow;
}
} // namespace
#endif

bool std_stream_target(const std::filesystem::path& path) {
#if defined(NIFT_OUTPUT_ORDER_MUTANT) && NIFT_OUTPUT_ORDER_MUTANT == 2  // test-only: no identity check
    static_cast<void>(path);
    return false;
#elif defined(_WIN32)
    const std::wstring name = path.filename().wstring();
    if (_wcsicmp(name.c_str(), L"CON") == 0 || _wcsicmp(name.c_str(), L"CONOUT$") == 0) return true;
    // No access rights and every share mode: an identity query that neither reads nor blocks.
    HANDLE h = CreateFileW(path.wstring().c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const bool same = same_file(h, GetStdHandle(STD_OUTPUT_HANDLE)) || same_file(h, GetStdHandle(STD_ERROR_HANDLE));
    CloseHandle(h);
    return same;
#else
    struct stat target{}, fd{};
    if (::stat(path.c_str(), &target) != 0) return false;
    for (int n : {1, 2})
        if (::fstat(n, &fd) == 0 && fd.st_dev == target.st_dev && fd.st_ino == target.st_ino) return true;
    return false;
#endif
}

// REVIEW-C139 C2 (narrowed in C139b): the null device (/dev/null; NUL on Windows) discards what is written, so a
// worker handle on it cannot lose or reorder output: the worker refusal does not apply to it. Only the null
// device: other character devices (terminals, /dev/kmsg, printers) keep the refusal.
bool null_device(const std::filesystem::path& path) {
#if defined(_WIN32)
    return _wcsicmp(path.filename().wstring().c_str(), L"NUL") == 0;
#else
    struct stat target{}, null{};
    return ::stat(path.c_str(), &target) == 0 && ::stat("/dev/null", &null) == 0 && S_ISCHR(target.st_mode) &&
           target.st_rdev == null.st_rdev;
#endif
}

bool std_stream_open(const Context& context, const std::filesystem::path& path, std::string_view name,
                     bool& std_target, std::string& error) {
    std_target = std_stream_target(path);
    if (!std_target) return true;
#if !(defined(NIFT_OUTPUT_ORDER_MUTANT) && NIFT_OUTPUT_ORDER_MUTANT == 3)  // test-only: no worker refusal
    if (context.in_worker && !null_device(path)) {
        error = std::string(name) + ": a thread() or async() task cannot open the process's standard output or error";
        if (context.host.error_code)
            *context.host.error_code = name == "open" ? "io.open_failed" : "stream.open_failed";
        return false;
    }
#endif
    if (!context.in_worker) context.publish_output();
    return true;
}

void emit_program_output(const Context& context, const std::string& data) {
#if defined(NIFT_OUTPUT_ORDER_MUTANT) && NIFT_OUTPUT_ORDER_MUTANT == 1  // test-only: cat bypasses the VM output
    if (!data.empty()) std::fwrite(data.data(), 1, data.size(), stdout);
    std::fflush(stdout);
#else
    if (context.emit) context.emit(context.vm, data);
    else if (!data.empty()) std::fwrite(data.data(), 1, data.size(), stdout);
#endif
}

namespace {
std::mutex tail_mutex;
std::vector<std::shared_ptr<std::ofstream>>& tail() {
    static std::vector<std::shared_ptr<std::ofstream>> streams;
    return streams;
}
} // namespace

void hold_std_stream(std::shared_ptr<std::ofstream> stream) {
    const std::lock_guard<std::mutex> lock(tail_mutex);
    tail().push_back(std::move(stream));
}

void release_std_stream_tail() {
    std::vector<std::shared_ptr<std::ofstream>> streams;
    {
        const std::lock_guard<std::mutex> lock(tail_mutex);
        streams.swap(tail());
    }
    for (auto& stream : streams) stream->close();  // flushes, in the order the streams were created
}

} // namespace nift::rt::natives
