#include "rt/natives/host_std_stream.h"
// rt-lint: cold-file std-stream identity and end-of-run stream tail (output-order repair)

#include "rt/natives/builtin.h"

#include <cerrno>
#include <cstdio>
#include <mutex>
#include <system_error>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
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

bool save_target(const std::filesystem::path& path, std::filesystem::path& resolved, std::string& error) {
    namespace fs = std::filesystem;
    resolved = path;
    std::error_code ec;
#if !defined(_WIN32) && !(defined(NIFT_SAVE_MUTANT) && NIFT_SAVE_MUTANT == 8)  // test-only: any lstat error a new file
    struct stat raw {};
    if (::lstat(path.c_str(), &raw) != 0 && errno != ENOENT) {  // REVIEW-C142 L1: only ENOENT means a new file
        const int failure = errno;
        error = "save: cannot check the target: " + std::generic_category().message(failure);
        return false;
    }
#endif
    fs::file_status st = fs::symlink_status(path, ec);
    if (st.type() == fs::file_type::not_found) return true;  // a new file: save()'s parent check applies
    if (ec) { error = "save: cannot check the target: " + ec.message(); return false; }
#if !(defined(NIFT_SAVE_MUTANT) && NIFT_SAVE_MUTANT == 2)  // test-only: no std-stream refusal
    // REVIEW-C142 L4: before resolving, as stat() follows every link: /dev/stdout on a pipe (pipe:[...]) has no
    // canonical path, so it would otherwise read as dangling.
    if (std_stream_target(path)) { error = "save: target is the process's standard output or error"; return false; }
#endif
    if (fs::is_symlink(st)) {
        const fs::path target = fs::canonical(path, ec);  // follows the whole chain; fails if it dangles or loops
#if defined(NIFT_SAVE_MUTANT) && NIFT_SAVE_MUTANT == 1  // test-only: a dangling link is saved as a new file
        if (ec) return true;
#endif
        if (ec) { error = "save: dangling symlink"; return false; }
        st = fs::status(target, ec);
#if !(defined(NIFT_SAVE_MUTANT) && NIFT_SAVE_MUTANT == 5)  // test-only: the link itself is replaced
        resolved = target;
#endif
    }
#if !(defined(NIFT_SAVE_MUTANT) && NIFT_SAVE_MUTANT == 3)  // test-only: no special-file refusal
    if (ec || !fs::is_regular_file(st)) { error = "save: target is not a regular file"; return false; }
#endif
    bool read_only = (st.permissions() & fs::perms::owner_write) == fs::perms::none;
#if !defined(_WIN32)
    read_only = read_only || (::geteuid() != 0 && ::access(resolved.c_str(), W_OK) != 0);
#endif
#if defined(NIFT_SAVE_MUTANT) && NIFT_SAVE_MUTANT == 4  // test-only: no read-only refusal
    read_only = false;
#endif
    if (read_only) { error = "save: target is read-only"; return false; }
    return true;
}

// REVIEW-C139 C2 (narrowed in C139b): the null device (/dev/null; NUL on Windows) discards what is written, so a
// worker handle on it cannot lose or reorder output: the worker refusal does not apply to it. Only the null
// device: other character devices (terminals, /dev/kmsg, printers) keep the refusal.
bool null_device(const std::filesystem::path& path) {
#if defined(_WIN32)
    // C139c L-b (uncompiled; unverified until Windows CI): no handle is opened. Win32 path normalization resolves the
    // reserved device name to \\.\NUL; a literal file named NUL (made through a \\?\ path) is not normalized, so it
    // keeps the refusal. Where normalization does not map the name, the refusal applies (the safe direction).
    wchar_t full[MAX_PATH];
    const DWORD n = GetFullPathNameW(path.wstring().c_str(), MAX_PATH, full, nullptr);
    return n > 0 && n < MAX_PATH && _wcsicmp(full, L"\\\\.\\NUL") == 0;
#else
    struct stat target{}, null{};
    return ::stat(path.c_str(), &target) == 0 && ::stat("/dev/null", &null) == 0 && S_ISCHR(target.st_mode) &&
           S_ISCHR(null.st_mode) && target.st_rdev == null.st_rdev;  // C139c L-a: /dev/null itself a char device
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
