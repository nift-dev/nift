// Native SHA-256 for Nift (feature-sha256 checkpoint). See Sha256.h.
#include "Sha256.h"

#include "../third_party/bitcoin-sha256/sha256_bitcoin.h"

#include <atomic>
#include <cerrno>
#include <memory>
#include <new>
#include <type_traits>

#if defined(NIFT_SHA256_HAVE_X86_SHANI)
#include <cpuid.h>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

using TransformFn = void (*)(uint32_t*, const unsigned char*, size_t);
using Vendor = nift_vendor_bitcoin::CSHA256;

static_assert(std::is_trivially_destructible<Vendor>::value, "vendored state must not need destruction");
static_assert(std::is_trivially_copyable<Vendor>::value, "vendored state must be trivially copyable");

std::atomic<TransformFn> selected_transform{nullptr};
std::atomic<bool> forced_portable{false};

void portable_transform(uint32_t* s, const unsigned char* chunk, size_t blocks) {
    nift_vendor_bitcoin::portable::sha256::Transform(s, chunk, blocks);
}

#if defined(NIFT_SHA256_HAVE_X86_SHANI)
void x86_shani_transform(uint32_t* s, const unsigned char* chunk, size_t blocks) {
    nift_vendor_bitcoin::x86_shani::sha256_x86_shani::Transform(s, chunk, blocks);
}

bool cpu_has_x86_shani() {
    unsigned int max_leaf = __get_cpuid_max(0, nullptr);
    if (max_leaf < 7) return false;  // leaf 7 absent: never trust its registers
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    __cpuid_count(1, 0, eax, ebx, ecx, edx);
    const bool ssse3 = (ecx >> 9) & 1u;
    const bool sse41 = (ecx >> 19) & 1u;
    __cpuid_count(7, 0, eax, ebx, ecx, edx);
    const bool sha = (ebx >> 29) & 1u;
    return ssse3 && sse41 && sha;
}
#endif

#if defined(NIFT_SHA256_HAVE_X86_SHANI)
// Run once before an accelerated transform is trusted: the padded "abc" block
// must give the FIPS 180-4 chaining state, and a second, arbitrary block
// chained after it must match the portable transform.
bool transform_self_test(TransformFn transform) {
    static const unsigned char blocks[128] = {
        'a', 'b', 'c', 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x18,
        // Arbitrary second block: compared against the portable transform.
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        0xf0, 0xe1, 0xd2, 0xc3, 0xb4, 0xa5, 0x96, 0x87, 0x78, 0x69, 0x5a, 0x4b, 0x3c, 0x2d, 0x1e, 0x0f,
        0xff, 0x00, 0xff, 0x00, 0x80, 0x7f, 0x80, 0x7f, 0x01, 0xfe, 0x01, 0xfe, 0x55, 0xaa, 0x55, 0xaa,
    };
    // SHA-256("abc") chaining state after its single block.
    static const uint32_t abc[8] = {0xba7816bfu, 0x8f01cfeau, 0x414140deu, 0x5dae2223u,
                                    0xb00361a3u, 0x96177a9cu, 0xb410ff61u, 0xf20015adu};
    uint32_t state[8];
    nift_vendor_bitcoin::portable::sha256::Initialize(state);
    transform(state, blocks, 1);
    for (int i = 0; i < 8; ++i) if (state[i] != abc[i]) return false;
    uint32_t expected[8];
    std::memcpy(expected, abc, sizeof expected);
    portable_transform(expected, blocks + 64, 1);
    transform(state, blocks + 64, 1);
    return std::memcmp(state, expected, sizeof state) == 0;
}
#endif

TransformFn accelerated_transform() {
#if defined(NIFT_SHA256_HAVE_X86_SHANI)
    static const TransformFn accelerated = []() -> TransformFn {
        if (!cpu_has_x86_shani()) return nullptr;
        return transform_self_test(x86_shani_transform) ? x86_shani_transform : nullptr;
    }();
    return accelerated;
#else
    return nullptr;
#endif
}

TransformFn select_transform() {
    if (forced_portable.load(std::memory_order_relaxed)) return portable_transform;
    const TransformFn accelerated = accelerated_transform();
    return accelerated ? accelerated : portable_transform;
}

TransformFn current_transform() {
    TransformFn transform = selected_transform.load(std::memory_order_acquire);
    if (!transform) {
        transform = select_transform();
        selected_transform.store(transform, std::memory_order_release);
    }
    return transform;
}

Vendor* vendor(unsigned char* storage) noexcept {
    return std::launder(reinterpret_cast<Vendor*>(storage));
}

} // namespace

// The block transform the vendored CSHA256 driver calls.
void nift_vendor_bitcoin::Transform(uint32_t* s, const unsigned char* chunk, size_t blocks) {
    current_transform()(s, chunk, blocks);
}

namespace nift::crypto {

static_assert(sizeof(Vendor) <= sizeof(Sha256), "Sha256 storage too small for the vendored state");
static_assert(alignof(Vendor) <= 8, "Sha256 storage under-aligned for the vendored state");

Sha256::Sha256() noexcept { new (state_) Vendor(); }

Sha256& Sha256::update(const void* data, std::size_t size) noexcept {
    if (size != 0) vendor(state_)->Write(static_cast<const unsigned char*>(data), size);
    return *this;
}

Sha256Digest Sha256::finish() noexcept {
    Sha256Digest digest;
    vendor(state_)->Finalize(digest.data());
    vendor(state_)->Reset();
    return digest;
}

void Sha256::reset() noexcept { vendor(state_)->Reset(); }

Sha256Digest sha256_bytes(const void* data, std::size_t size) noexcept {
    Sha256 hasher;
    hasher.update(data, size);
    return hasher.finish();
}

void sha256_hex(const Sha256Digest& digest, char out[kSha256DigestSize * 2]) noexcept {
    static const char digits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < digest.size(); ++i) {
        out[2 * i] = digits[digest[i] >> 4];
        out[2 * i + 1] = digits[digest[i] & 0x0f];
    }
}

std::string sha256_hex(const Sha256Digest& digest) {
    std::string out(kSha256DigestSize * 2, '\0');
    sha256_hex(digest, &out[0]);
    return out;
}

namespace {

struct ReadBuffer {
    std::unique_ptr<std::uint8_t[]> data{new (std::nothrow) std::uint8_t[kSha256FileBufferSize]};
};

Sha256FileResult fail(Sha256FileStatus status, std::error_code error) {
    Sha256FileResult result;
    result.status = status;
    result.error = error;
    return result;
}

} // namespace

#ifdef _WIN32

Sha256FileResult sha256_file(const std::filesystem::path& path, Sha256ChunkObserver observer,
                             void* observer_context) noexcept {
    ReadBuffer buffer;
    if (!buffer.data) return fail(Sha256FileStatus::ReadFailed, std::make_error_code(std::errc::not_enough_memory));
    HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD code = ::GetLastError();
        const std::error_code error(static_cast<int>(code), std::system_category());
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return fail(Sha256FileStatus::NotFound, error);
        if (code == ERROR_ACCESS_DENIED) {
            // CreateFileW refuses directories without FILE_FLAG_BACKUP_SEMANTICS
            // with the same code as a permission failure; tell them apart.
            const DWORD attributes = ::GetFileAttributesW(path.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
                return fail(Sha256FileStatus::IsDirectory, error);
        }
        return fail(Sha256FileStatus::OpenFailed, error);
    }
    Sha256 hasher;
    std::uint64_t total = 0;
    for (;;) {
        DWORD received = 0;
        if (!::ReadFile(handle, buffer.data.get(), static_cast<DWORD>(kSha256FileBufferSize), &received, nullptr)) {
            const std::error_code error(static_cast<int>(::GetLastError()), std::system_category());
            ::CloseHandle(handle);
            return fail(Sha256FileStatus::ReadFailed, error);
        }
        if (received == 0) break;
        hasher.update(buffer.data.get(), received);
        if (observer) observer(observer_context, buffer.data.get(), received);
        total += received;
    }
    ::CloseHandle(handle);
    Sha256FileResult result;
    result.status = Sha256FileStatus::Ok;
    result.size = total;
    result.digest = hasher.finish();
    return result;
}

#else

Sha256FileResult sha256_file(const std::filesystem::path& path, Sha256ChunkObserver observer,
                             void* observer_context) noexcept {
    int fd;
    do {
        fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        const int code = errno;
        const std::error_code error(code, std::generic_category());
        // ENOTDIR: a parent component is not a directory, which
        // std::filesystem (and so exists()/stat()) also reports as not found.
        if (code == ENOENT || code == ENOTDIR) return fail(Sha256FileStatus::NotFound, error);
        if (code == EISDIR) return fail(Sha256FileStatus::IsDirectory, error);
        return fail(Sha256FileStatus::OpenFailed, error);
    }
    ReadBuffer buffer;
    if (!buffer.data) {
        ::close(fd);
        return fail(Sha256FileStatus::ReadFailed, std::make_error_code(std::errc::not_enough_memory));
    }
    Sha256 hasher;
    std::uint64_t total = 0;
    for (;;) {
        const ssize_t received = ::read(fd, buffer.data.get(), kSha256FileBufferSize);
        if (received < 0) {
            const int code = errno;
            if (code == EINTR) continue;
            ::close(fd);
            // A directory opens read-only on POSIX; reading it fails with EISDIR.
            return fail(code == EISDIR ? Sha256FileStatus::IsDirectory : Sha256FileStatus::ReadFailed,
                        std::error_code(code, std::generic_category()));
        }
        if (received == 0) break;
        const auto count = static_cast<std::size_t>(received);
        hasher.update(buffer.data.get(), count);
        if (observer) observer(observer_context, buffer.data.get(), count);
        total += count;
    }
    ::close(fd);
    Sha256FileResult result;
    result.status = Sha256FileStatus::Ok;
    result.size = total;
    result.digest = hasher.finish();
    return result;
}

#endif

const char* sha256_implementation() noexcept {
    return current_transform() == portable_transform ? "portable" : "x86-sha-ni";
}

namespace detail {

bool sha256_force_portable_for_tests(bool portable) noexcept {
    forced_portable.store(portable, std::memory_order_relaxed);
    selected_transform.store(select_transform(), std::memory_order_release);
    return accelerated_transform() != nullptr;
}

bool sha256_accelerated_available() noexcept { return accelerated_transform() != nullptr; }

} // namespace detail

} // namespace nift::crypto
