#pragma once
// Native SHA-256 (FIPS 180-4) for Nift: one implementation shared by the
// legacy interpreter's built-ins (`sha256`, `sha256_file`) and, later, the
// Runtime 2.0 VM's native table. The interface is deliberately independent of
// the textual evaluator: it takes raw byte spans and filesystem paths and
// returns typed results, so a caller never re-parses arguments or converts
// values to strings to use it.
//
// The block transforms are a vendored subset of Bitcoin Core's implementation
// (third_party/bitcoin-sha256, MIT). On x86/x86-64 the SHA-NI transform is
// used when CPUID reports SHA and SSE4.1 and a known-answer self-test passes;
// otherwise the portable transform is used. Selection happens once, lazily and
// thread-safely.
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace nift::crypto {

inline constexpr std::size_t kSha256DigestSize = 32;
inline constexpr std::size_t kSha256BlockSize = 64;
// Read size used by sha256_file: large enough that per-read overhead is small
// next to hashing, small enough to stay cache-resident.
inline constexpr std::size_t kSha256FileBufferSize = 64 * 1024;

using Sha256Digest = std::array<std::uint8_t, kSha256DigestSize>;

// Incremental hasher. update() may be called any number of times with any
// span sizes; finish() pads, returns the digest and resets the hasher so it
// can be reused. Not thread-safe per object; distinct objects are independent.
class Sha256 {
public:
    Sha256() noexcept;
    Sha256& update(const void* data, std::size_t size) noexcept;
    Sha256Digest finish() noexcept;
    void reset() noexcept;

private:
    // Opaque storage for the vendored streaming state (8 words of chaining
    // state, a 64-byte block buffer and a 64-bit byte count).
    alignas(8) unsigned char state_[8 * 4 + 64 + 8];
};

// One-shot digest of a byte span (NUL bytes are ordinary data).
Sha256Digest sha256_bytes(const void* data, std::size_t size) noexcept;

// Lowercase hexadecimal spelling (64 characters).
void sha256_hex(const Sha256Digest& digest, char out[kSha256DigestSize * 2]) noexcept;
std::string sha256_hex(const Sha256Digest& digest);

enum class Sha256FileStatus : std::uint8_t {
    Ok,
    NotFound,     // the path (or a parent component) does not exist
    IsDirectory,  // the path names a directory
    OpenFailed,   // any other open failure (permissions, invalid name, ...)
    ReadFailed,   // an I/O error after the file was opened
};

struct Sha256FileResult {
    Sha256FileStatus status = Sha256FileStatus::OpenFailed;
    std::error_code error;   // the OS error when status != Ok
    std::uint64_t size = 0;  // bytes hashed (valid when status == Ok)
    Sha256Digest digest{};   // valid when status == Ok
    bool ok() const noexcept { return status == Sha256FileStatus::Ok; }
};

// Called with each chunk exactly as it is hashed, in order. Lets a caller
// derive something else from the same bytes in the same pass (Nift's
// dependency snapshot) without a second read.
using Sha256ChunkObserver = void (*)(void* context, const std::uint8_t* data, std::size_t size);

// Digest of the file's raw bytes, read from the start to end-of-file in
// kSha256FileBufferSize chunks. Symlinks are followed (ordinary open). The
// whole file is never held in memory. Paths are used as given: relative paths
// resolve against the process working directory; on Windows the native wide
// path is used.
Sha256FileResult sha256_file(const std::filesystem::path& path,
                             Sha256ChunkObserver observer = nullptr,
                             void* observer_context = nullptr) noexcept;

// Name of the transform in use: "x86-sha-ni" or "portable".
const char* sha256_implementation() noexcept;

namespace detail {
// Tests only: force the portable transform (true) or return to automatic
// selection (false). Returns false when the accelerated path is unavailable.
bool sha256_force_portable_for_tests(bool portable) noexcept;
bool sha256_accelerated_available() noexcept;
} // namespace detail

} // namespace nift::crypto
