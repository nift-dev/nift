// Throughput of the native SHA-256 module (feature-sha256 evidence; not a
// test). Separates hashing from file access:
//   1. in-memory hashing (MB/s) per transform, for large and small messages;
//   2. one large warm file through sha256_file (read + hash);
//   3. many small warm files: sha256_file vs a plain open/read/close loop with
//      the same buffer, so the per-file filesystem cost is visible on its own.
// Usage: sha256-bench [directory for temporary files]
#include "Sha256.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

static double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

static volatile std::uint8_t sink;

static void memory(const char* impl) {
    std::vector<std::uint8_t> big(1 << 20);
    std::mt19937_64 rng(1);
    for (auto& b : big) b = static_cast<std::uint8_t>(rng());
    for (std::size_t size : {std::size_t(1) << 20, std::size_t(64 * 1024), std::size_t(4096), std::size_t(700), std::size_t(64)}) {
        // Calibrate to ~0.4 s.
        std::size_t iterations = std::max<std::size_t>(1, (64u << 20) / size);
        double best = 1e9;
        for (int round = 0; round < 3; ++round) {
            const auto start = Clock::now();
            for (std::size_t i = 0; i < iterations; ++i) {
                const auto d = nift::crypto::sha256_bytes(big.data(), size);
                sink = d[0];
            }
            const double t = seconds_since(start);
            if (t < best) best = t;
        }
        const double mb = static_cast<double>(size) * static_cast<double>(iterations) / 1e6;
        std::printf("memory  %-10s message %8zu B: %8.1f MB/s  (%.3f us/message)\n", impl, size, mb / best,
                    best * 1e6 / static_cast<double>(iterations));
    }
}

static void write_random(const fs::path& p, std::size_t n, std::mt19937_64& rng) {
    std::vector<char> data(n);
    for (auto& c : data) c = static_cast<char>(rng());
    std::ofstream(p, std::ios::binary).write(data.data(), static_cast<std::streamsize>(n));
}

#ifndef _WIN32
// The same loop sha256_file runs, minus hashing: open, read to EOF, close.
static std::uint64_t read_only(const fs::path& p) {
    static std::vector<std::uint8_t> buffer(nift::crypto::kSha256FileBufferSize);
    const int fd = ::open(p.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) std::abort();
    std::uint64_t total = 0;
    for (;;) {
        const ssize_t n = ::read(fd, buffer.data(), buffer.size());
        if (n <= 0) break;
        total += static_cast<std::uint64_t>(n);
    }
    ::close(fd);
    return total;
}
#endif

static void files(const char* impl, const fs::path& dir) {
    const fs::path large = dir / "large.bin";
    double best = 1e9;
    std::uint64_t size = 0;
    for (int round = 0; round < 3; ++round) {
        const auto start = Clock::now();
        const auto r = nift::crypto::sha256_file(large);
        const double t = seconds_since(start);
        if (!r.ok()) std::abort();
        size = r.size;
        if (t < best) best = t;
    }
    std::printf("file    %-10s %llu MiB warm file: %8.1f MB/s\n", impl, static_cast<unsigned long long>(size >> 20),
                static_cast<double>(size) / 1e6 / best);

    std::vector<fs::path> smalls;
    for (const auto& e : fs::directory_iterator(dir / "small")) smalls.push_back(e.path());
    double hash_best = 1e9;
    for (int round = 0; round < 5; ++round) {
        const auto start = Clock::now();
        for (const auto& p : smalls) {
            const auto r = nift::crypto::sha256_file(p);
            if (!r.ok()) std::abort();
            sink = r.digest[0];
        }
        const double t = seconds_since(start);
        if (t < hash_best) hash_best = t;
    }
    std::printf("small   %-10s %zu warm files (96-2048 B): sha256_file %.1f ms total, %.2f us/file\n", impl, smalls.size(),
                hash_best * 1e3, hash_best * 1e6 / static_cast<double>(smalls.size()));
#ifndef _WIN32
    double read_best = 1e9;
    for (int round = 0; round < 5; ++round) {
        const auto start = Clock::now();
        for (const auto& p : smalls) sink = static_cast<std::uint8_t>(read_only(p));
        const double t = seconds_since(start);
        if (t < read_best) read_best = t;
    }
    std::printf("small   %-10s same files, open/read/close only: %.1f ms total, %.2f us/file (hashing adds %.2f us/file)\n",
                impl, read_best * 1e3, read_best * 1e6 / static_cast<double>(smalls.size()),
                (hash_best - read_best) * 1e6 / static_cast<double>(smalls.size()));
#endif
}

int main(int argc, char** argv) {
    const fs::path dir = (argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path()) / "nift-sha256-bench";
    fs::remove_all(dir);
    fs::create_directories(dir / "small");
    std::mt19937_64 rng(7);
    write_random(dir / "large.bin", std::size_t(256) << 20, rng);
    const std::size_t sizes[] = {96, 192, 384, 768, 2048};
    for (int i = 0; i < 10000; ++i) write_random(dir / "small" / ("f" + std::to_string(i)), sizes[i % 5], rng);

    std::vector<bool> modes{true};
    if (nift::crypto::detail::sha256_accelerated_available()) modes.push_back(false);
    for (bool portable : modes) {
        nift::crypto::detail::sha256_force_portable_for_tests(portable);
        const std::string impl = nift::crypto::sha256_implementation();
        memory(impl.c_str());
        files(impl.c_str(), dir);
    }
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    return 0;
}
