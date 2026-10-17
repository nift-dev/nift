// Unit tests for the native SHA-256 module (src/Sha256.*), feature-sha256.
// Runs every check against each available transform (portable and, when the
// CPU supports it, x86 SHA-NI) and cross-checks both against an independent
// textbook implementation written here from FIPS 180-4.
#include "Sha256.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using nift::crypto::Sha256;
using nift::crypto::Sha256Digest;
using nift::crypto::Sha256FileStatus;

static int failures = 0;
static int checks = 0;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

// ---- Independent reference (FIPS 180-4 section 6.2), deliberately simple.
namespace ref {
const std::uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
Sha256Digest hash(const std::vector<std::uint8_t>& input) {
    std::vector<std::uint8_t> m(input);
    const std::uint64_t bits = static_cast<std::uint64_t>(input.size()) * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; --i) m.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    std::uint32_t H[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    for (std::size_t off = 0; off < m.size(); off += 64) {
        std::uint32_t W[64];
        for (int t = 0; t < 16; ++t)
            W[t] = (std::uint32_t(m[off + 4 * t]) << 24) | (std::uint32_t(m[off + 4 * t + 1]) << 16) |
                   (std::uint32_t(m[off + 4 * t + 2]) << 8) | std::uint32_t(m[off + 4 * t + 3]);
        for (int t = 16; t < 64; ++t) {
            const std::uint32_t s0 = rotr(W[t - 15], 7) ^ rotr(W[t - 15], 18) ^ (W[t - 15] >> 3);
            const std::uint32_t s1 = rotr(W[t - 2], 17) ^ rotr(W[t - 2], 19) ^ (W[t - 2] >> 10);
            W[t] = W[t - 16] + s0 + W[t - 7] + s1;
        }
        std::uint32_t a = H[0], b = H[1], c = H[2], d = H[3], e = H[4], f = H[5], g = H[6], h = H[7];
        for (int t = 0; t < 64; ++t) {
            const std::uint32_t T1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[t] + W[t];
            const std::uint32_t T2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g; g = f; f = e; e = d + T1; d = c; c = b; b = a; a = T1 + T2;
        }
        H[0] += a; H[1] += b; H[2] += c; H[3] += d; H[4] += e; H[5] += f; H[6] += g; H[7] += h;
    }
    Sha256Digest out;
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 4; ++j) out[4 * i + j] = static_cast<std::uint8_t>(H[i] >> (24 - 8 * j));
    return out;
}
} // namespace ref

static std::string hex_of(const std::string& s) {
    return nift::crypto::sha256_hex(nift::crypto::sha256_bytes(s.data(), s.size()));
}

static std::vector<std::uint8_t> random_bytes(std::mt19937_64& rng, std::size_t n) {
    std::vector<std::uint8_t> v(n);
    for (auto& b : v) b = static_cast<std::uint8_t>(rng());
    return v;
}

static void known_answers(const char* impl) {
    // FIPS 180-4 / NIST CSRC example values (SHA256.pdf, SHA2_Additional.pdf).
    CHECK(hex_of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "[%s] empty", impl);
    CHECK(hex_of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "[%s] abc", impl);
    CHECK(hex_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "[%s] 448-bit", impl);
    CHECK(hex_of("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu") ==
              "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1", "[%s] 896-bit", impl);
    // One million 'a', fed in uneven pieces to exercise buffering.
    {
        Sha256 h;
        const std::string piece(997, 'a');
        std::size_t left = 1000000;
        while (left) { const std::size_t n = left < piece.size() ? left : piece.size(); h.update(piece.data(), n); left -= n; }
        CHECK(nift::crypto::sha256_hex(h.finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
              "[%s] one million a", impl);
        // finish() resets: the object is reusable.
        h.update("abc", 3);
        CHECK(nift::crypto::sha256_hex(h.finish()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "[%s] reuse after finish", impl);
        h.update("garbage", 7); h.reset(); h.update("abc", 3);
        CHECK(nift::crypto::sha256_hex(h.finish()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "[%s] reset", impl);
    }
    // SHA2_Additional.pdf: one byte 0xbd; four bytes c98c8e55; 55/56/57 zero bytes, 1000 'A', 1005 'U'.
    CHECK(hex_of(std::string(1, '\xbd')) == "68325720aabd7c82f30f554b313d0570c95accbb7dc4b5aae11204c08ffe732b", "[%s] 0xbd", impl);
    CHECK(hex_of(std::string("\xc9\x8c\x8e\x55", 4)) == "7abc22c0ae5af26ce93dbb94433a0e0b2e119d014f8e7f65bd56c61ccccd9504", "[%s] c98c8e55", impl);
    CHECK(hex_of(std::string(55, '\0')) == "02779466cdec163811d078815c633f21901413081449002f24aa3e80f0b88ef7", "[%s] 55 NUL", impl);
    CHECK(hex_of(std::string(56, '\0')) == "d4817aa5497628e7c77e6b606107042bbba3130888c5f47a375e6179be789fbb", "[%s] 56 NUL", impl);
    CHECK(hex_of(std::string(57, '\0')) == "65a16cb7861335d5ace3c60718b5052e44660726da4cd13bb745381b235a1785", "[%s] 57 NUL", impl);
    CHECK(hex_of(std::string(64, '\0')) == "f5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b", "[%s] 64 NUL", impl);
    CHECK(hex_of(std::string(1000, 'A')) == "c2e686823489ced2017f6059b8b239318b6364f6dcd835d0a519105a1eadd6e4", "[%s] 1000 A", impl);
    CHECK(hex_of(std::string(1005, 'U')) == "f4d62ddec0f3dd90ea1380fa16a5ff8dc4c54b21740650f24afc4120903552b0", "[%s] 1005 U", impl);
    // UTF-8 text is hashed as its bytes (no normalisation): "é" composed vs decomposed differ.
    CHECK(hex_of("\xc3\xa9") != hex_of("e\xcc\x81"), "[%s] NFC vs NFD distinct", impl);
}

static void differential(const char* impl, std::mt19937_64& rng) {
    std::vector<std::size_t> sizes;
    for (std::size_t n = 0; n <= 260; ++n) sizes.push_back(n);
    for (std::size_t n : {511u, 512u, 513u, 1023u, 1024u, 4095u, 4096u, 4097u, 65535u, 65536u, 65537u, 100003u}) sizes.push_back(n);
    for (std::size_t n : sizes) {
        const auto data = random_bytes(rng, n);
        const Sha256Digest expected = ref::hash(data);
        CHECK(nift::crypto::sha256_bytes(data.data(), data.size()) == expected, "[%s] one-shot size %zu", impl, n);
        // Random split into pieces, including empty updates.
        Sha256 h;
        std::size_t off = 0;
        while (off < n) {
            std::size_t piece = static_cast<std::size_t>(rng() % 150);
            if (piece > n - off) piece = n - off;
            h.update(data.data() + off, piece);
            off += piece;
        }
        h.update(nullptr, 0);
        CHECK(h.finish() == expected, "[%s] split size %zu", impl, n);
    }
}

struct Collect {
    std::vector<std::uint8_t> bytes;
    std::size_t calls = 0;
    std::size_t largest = 0;
};
static void collect(void* context, const std::uint8_t* data, std::size_t size) {
    auto* c = static_cast<Collect*>(context);
    c->bytes.insert(c->bytes.end(), data, data + size);
    ++c->calls;
    if (size > c->largest) c->largest = size;
}

static fs::path write_file(const fs::path& dir, const std::string& name, const std::vector<std::uint8_t>& data) {
    const fs::path p = dir / name;
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return p;
}

static void files(const char* impl, const fs::path& dir, std::mt19937_64& rng) {
    const std::size_t B = nift::crypto::kSha256FileBufferSize;
    for (std::size_t n : {std::size_t(0), std::size_t(1), std::size_t(55), std::size_t(56), std::size_t(63), std::size_t(64),
                          std::size_t(65), B - 1, B, B + 1, 2 * B, 3 * B + 17, std::size_t(5 * 1024 * 1024 + 3)}) {
        const auto data = random_bytes(rng, n);
        const fs::path p = write_file(dir, "f" + std::to_string(n), data);
        Collect seen;
        const auto r = nift::crypto::sha256_file(p, collect, &seen);
        CHECK(r.ok(), "[%s] file %zu status", impl, n);
        CHECK(r.size == n, "[%s] file %zu size %llu", impl, n, static_cast<unsigned long long>(r.size));
        CHECK(r.digest == ref::hash(data), "[%s] file %zu digest", impl, n);
        CHECK(seen.bytes == data, "[%s] file %zu observer saw the exact bytes", impl, n);
        CHECK(seen.largest <= B, "[%s] file %zu bounded chunks", impl, n);
        const auto again = nift::crypto::sha256_file(p);
        CHECK(again.ok() && again.digest == r.digest, "[%s] file %zu repeat", impl, n);
    }
    // Embedded NULs and every byte value.
    std::vector<std::uint8_t> all;
    for (int i = 0; i < 256; ++i) all.push_back(static_cast<std::uint8_t>(i));
    all.insert(all.end(), 100, 0);
    const auto pa = write_file(dir, "allbytes.bin", all);
    CHECK(nift::crypto::sha256_file(pa).digest == ref::hash(all), "[%s] all byte values", impl);

    const auto missing = nift::crypto::sha256_file(dir / "missing.bin");
    CHECK(missing.status == Sha256FileStatus::NotFound && missing.error, "[%s] missing -> NotFound", impl);
    const auto through_file = nift::crypto::sha256_file(pa / "child");
    CHECK(through_file.status == Sha256FileStatus::NotFound, "[%s] file used as directory -> NotFound (%d)", impl,
          static_cast<int>(through_file.status));
    const auto directory = nift::crypto::sha256_file(dir);
    CHECK(directory.status == Sha256FileStatus::IsDirectory, "[%s] directory -> IsDirectory (%d)", impl,
          static_cast<int>(directory.status));
#ifndef _WIN32
    // Symlinks are followed.
    const fs::path link = dir / "link.bin";
    fs::create_symlink(pa, link);
    CHECK(nift::crypto::sha256_file(link).digest == ref::hash(all), "[%s] symlink followed", impl);
    const fs::path dangling = dir / "dangling.bin";
    fs::create_symlink(dir / "nowhere", dangling);
    CHECK(nift::crypto::sha256_file(dangling).status == Sha256FileStatus::NotFound, "[%s] dangling symlink", impl);
    if (::geteuid() != 0) {
        const auto locked = write_file(dir, "locked.bin", all);
        ::chmod(locked.c_str(), 0);
        const auto r = nift::crypto::sha256_file(locked);
        CHECK(r.status == Sha256FileStatus::OpenFailed && r.error.value() == EACCES, "[%s] unreadable -> OpenFailed", impl);
        ::chmod(locked.c_str(), 0600);
    } else {
        std::printf("note: running as root; unreadable-file check skipped (root bypasses permissions)\n");
    }
#endif
}

static void concurrent(const char* impl) {
    std::vector<std::thread> threads;
    std::vector<int> ok(8, 0);
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([t, &ok] {
            std::mt19937_64 rng(1000 + t);
            int good = 1;
            for (int i = 0; i < 200; ++i) {
                const auto data = random_bytes(rng, static_cast<std::size_t>(rng() % 5000));
                if (nift::crypto::sha256_bytes(data.data(), data.size()) != ref::hash(data)) good = 0;
            }
            ok[static_cast<std::size_t>(t)] = good;
        });
    }
    for (auto& th : threads) th.join();
    for (int t = 0; t < 8; ++t) CHECK(ok[static_cast<std::size_t>(t)] == 1, "[%s] concurrent thread %d", impl, t);
}

int main() {
    const fs::path dir = fs::temp_directory_path() / ("nift-sha256-unit-" + std::to_string(
#ifdef _WIN32
        0
#else
        ::getpid()
#endif
        ));
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::printf("selected implementation: %s\n", nift::crypto::sha256_implementation());
    const bool accelerated = nift::crypto::detail::sha256_accelerated_available();
    std::vector<bool> modes{true};
    if (accelerated) modes.push_back(false);
    else std::printf("note: no accelerated transform on this CPU/target; portable only\n");
    for (bool portable : modes) {
        nift::crypto::detail::sha256_force_portable_for_tests(portable);
        const std::string impl = nift::crypto::sha256_implementation();
        CHECK(portable ? impl == "portable" : impl != "portable", "implementation switch (%s)", impl.c_str());
        std::mt19937_64 rng(42);
        known_answers(impl.c_str());
        differential(impl.c_str(), rng);
        const fs::path sub = dir / impl;
        fs::create_directories(sub);
        files(impl.c_str(), sub, rng);
        concurrent(impl.c_str());
    }
    nift::crypto::detail::sha256_force_portable_for_tests(false);
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    std::printf("sha256 unit: %d checks, %d failures (implementations: %s)\n", checks, failures,
                accelerated ? "portable, x86-sha-ni" : "portable");
    return failures == 0 ? 0 : 1;
}
