// feature-host-deps (D-028): unit tests for src/ReadLimit.h — setting parsing
// (overflow-safe), exact boundary vs one byte over, bounded buffer capacity
// (never reserved beyond the limit, growth never replaces a buffer larger than
// limit/2), max_bytes reads, release on overflow, and stream error reporting.
#include "ReadLimit.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

namespace rl = nift::readlimit;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

// An endless source of zeros (like /dev/zero), counting bytes handed out.
struct EndlessBuf : std::streambuf {
    char block[4096] = {};
    unsigned long long served = 0;
    int_type underflow() override {
        setg(block, block, block + sizeof block);
        served += sizeof block;
        return traits_type::to_int_type(block[0]);
    }
};

// A source that fails (badbit) after `ok` bytes.
struct FailingBuf : std::streambuf {
    std::string data;
    explicit FailingBuf(std::size_t ok) : data(ok, 'x') { setg(&data[0], &data[0], &data[0] + data.size()); }
    int_type underflow() override { throw std::ios_base::failure("injected"); }
};

// Records the largest capacity the buffer ever had, through every growth step.
template <class Buffer>
static std::size_t max_capacity_reading(std::istream& in, Buffer& out, std::uint64_t limit, rl::Status& status,
                                        std::uint64_t max_bytes = std::numeric_limits<std::uint64_t>::max()) {
    status = rl::read_bounded(in, out, limit, max_bytes);
    return out.capacity();
}

int main() {
    // --- parsing -------------------------------------------------------------
    CHECK(rl::parse(nullptr).valid && rl::parse(nullptr).bytes == rl::kDefaultReadLimit);
    CHECK(rl::parse("").valid && rl::parse("").bytes == (1ull << 30));
    CHECK(rl::parse("1").bytes == 1);
    CHECK(rl::parse("4096").bytes == 4096);
    CHECK(rl::parse("1K").bytes == 1024 && rl::parse("1kib").bytes == 1024 && rl::parse("1KB").bytes == 1024);
    CHECK(rl::parse("3M").bytes == 3ull << 20 && rl::parse("2GiB").bytes == 2ull << 30 && rl::parse("1t").bytes == 1ull << 40);
    CHECK(rl::parse("18446744073709551615").valid && rl::parse("18446744073709551615").bytes == ~0ull);
    for (const char* bad : {"0", "0K", "-1", "abc", "12Q", "K", "1.5M", " 1", "1 ", "18446744073709551616",
                            "99999999999999999999", "16777216T", "17179869184G"})
        CHECK(!rl::parse(bad).valid && rl::parse(bad).raw == bad && rl::parse(bad).bytes == rl::kDefaultReadLimit);
    CHECK(rl::parse("16777215T").valid && rl::parse("16777215T").bytes == 16777215ull << 40);
    CHECK(rl::exceeded_message("open", "/dev/zero", 1024) ==
          "open: /dev/zero exceeds the read limit of 1024 bytes (NIFT_READ_LIMIT)");

    // --- exact boundary vs one byte over (string and bytes) --------------------
    for (std::uint64_t limit : {1ull, 2ull, 1000ull, 65535ull, 65536ull, 65537ull, 200000ull}) {
        {
            std::istringstream in(std::string(limit, 'a'));
            std::string out;
            CHECK(rl::read_bounded(in, out, limit) == rl::Status::Ok && out.size() == limit && out.capacity() <= std::max<std::uint64_t>(limit, 15));
        }
        {
            std::istringstream in(std::string(limit + 1, 'a'));
            std::string out;
            CHECK(rl::read_bounded(in, out, limit) == rl::Status::Exceeded && out.empty());
        }
        {
            std::istringstream in(std::string(limit, 'b'));
            std::vector<std::uint8_t> out;
            CHECK(rl::read_bounded(in, out, limit) == rl::Status::Ok && out.size() == limit && out.capacity() <= limit);
        }
        {
            std::istringstream in(std::string(limit + 1, 'b'));
            std::vector<std::uint8_t> out;
            CHECK(rl::read_bounded(in, out, limit) == rl::Status::Exceeded && out.empty() && out.capacity() == 0);
        }
    }
    {
        std::istringstream in("");
        std::string out;
        CHECK(rl::read_bounded(in, out, 10) == rl::Status::Ok && out.empty());
    }

    // --- endless source: stops after limit + 1 bytes of probe, never more ------
    for (std::uint64_t limit : {1ull, 4096ull, 100000ull, 3ull << 20, (3ull << 20) + 1}) {
        EndlessBuf buf;
        std::istream in(&buf);
        std::vector<std::uint8_t> out;
        rl::Status status;
        max_capacity_reading(in, out, limit, status);
        CHECK(status == rl::Status::Exceeded && out.empty());
        // At most one stream-buffer block beyond the limit was pulled.
        CHECK(buf.served <= limit + 1 + sizeof buf.block);
    }
    // Capacity never exceeds the limit, and the replaced buffer in each growth
    // step is at most limit/2 (so a growth step's transient footprint is
    // <= 1.5 x limit).
    for (std::uint64_t limit : {100000ull, (1ull << 20) + 7, 3ull << 20, (1ull << 22) - 1}) {
        std::uint64_t capacity = 0, worst_old = 0;
        for (std::uint64_t needed = 1; needed <= limit; ) {
            if (needed > capacity) {
                const std::uint64_t next = rl::growth_target(capacity, needed, limit, rl::kChunk);
                CHECK(next <= limit && next >= needed);
                if (capacity > worst_old) worst_old = capacity;
                capacity = next;
            }
            needed += 4093;
        }
        CHECK(worst_old <= limit / 2);
        std::string data(limit - 3, 'z');
        std::istringstream in(data);
        std::string out;
        CHECK(rl::read_bounded(in, out, limit) == rl::Status::Ok && out == data && out.capacity() <= limit);
    }

    // --- max_bytes: bounded request, buffer, not cumulative --------------------
    {
        EndlessBuf buf;
        std::istream in(&buf);
        for (int i = 0; i < 50; ++i) {  // 50 x 30000 bytes from a limit of 40000
            std::string out;
            CHECK(rl::read_bounded(in, out, 40000, 30000) == rl::Status::Ok && out.size() == 30000);
            CHECK(out.capacity() <= 40000);
        }
        std::string out;
        CHECK(rl::read_bounded(in, out, 40000, 40000) == rl::Status::Ok && out.size() == 40000);
        std::string over;
        CHECK(rl::read_bounded(in, over, 40000, 40001) == rl::Status::Exceeded && over.empty());
    }
    {
        std::istringstream in("short");
        std::vector<std::uint8_t> out;
        CHECK(rl::read_bounded(in, out, 10, 1000000) == rl::Status::Ok && out.size() == 5 && out.capacity() <= 10);
    }
    {
        // A small max_bytes read does not reserve a whole chunk.
        std::istringstream in(std::string(100000, 'q'));
        std::vector<std::uint8_t> out;
        CHECK(rl::read_bounded(in, out, 1ull << 30, 10) == rl::Status::Ok && out.size() == 10 && out.capacity() <= 10);
    }
    {
        // A regular file that grows past its pre-sized length: capacity still
        // never passes the limit (libstdc++ would round reserve() up to 2x).
        std::istringstream in(std::string(9000, 'g'));
        std::string out;
        CHECK(rl::read_bounded(in, out, 10000, std::numeric_limits<std::uint64_t>::max(), 7000) == rl::Status::Ok &&
              out.size() == 9000 && out.capacity() <= 10000);
        std::istringstream over(std::string(10001, 'g'));
        std::string out2;
        CHECK(rl::read_bounded(over, out2, 10000, std::numeric_limits<std::uint64_t>::max(), 7000) == rl::Status::Exceeded &&
              out2.empty());
    }
    {
        // Size hint pre-sizes exactly.
        std::istringstream in(std::string(5000, 'h'));
        std::string out;
        CHECK(rl::read_bounded(in, out, 1ull << 30, std::numeric_limits<std::uint64_t>::max(), 5000) == rl::Status::Ok &&
              out.size() == 5000 && out.capacity() < 5000 + 64);
    }

    // --- stream failures are read errors, not limit errors or success ----------
    {
        FailingBuf buf(100);
        std::istream in(&buf);
        std::string out;
        CHECK(rl::read_bounded(in, out, 1000) == rl::Status::ReadError);
    }

    // --- append_bounded (lines, tokens) ---------------------------------------
    {
        std::string line;
        for (int i = 0; i < 7; ++i) CHECK(rl::append_bounded(line, 'l', 7));
        CHECK(!rl::append_bounded(line, 'l', 7) && line.size() == 7 && line.capacity() <= 32);
        std::string small;
        CHECK(rl::append_bounded(small, 'x', 1ull << 30) && small.capacity() < 64);
    }

    if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    std::puts("read-limit unit: all checks passed");
    return 0;
}
