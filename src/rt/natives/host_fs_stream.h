#pragma once
// CP16 host built-in stream reads: budgeted incremental buffering for files,
// pipes and other unknown-size inputs.
#include "rt/natives/host_util.h"

#include "ReadLimit.h"
#include "rt/heap_account.h"
#include "rt/natives/support.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace nift::rt::natives {

namespace fs = std::filesystem;

enum class ReadOutcome { Ok, Limit, Budget, InvalidLimit, OpenFailed, ReadFailed };

inline std::size_t budget_headroom(const Context& context) {
    if (context.heap_limit == Context::kNoHeapLimit) return kMaxResultBytes;
    const std::size_t live = live_heap_bytes();
    return live < context.heap_limit ? context.heap_limit - live : 0;
}

// Unknown-size streams grow to at most 9/17 of the entry headroom. A final
// trim then costs capacity + size and still fits: the trim only runs when
// capacity - size > size/8, so size < 8/9 capacity and capacity + size <
// 17/9 capacity <= 17/9 * 9/17 headroom = headroom.
inline std::size_t unknown_capacity_cap(const Context& context) {
    if (context.heap_limit == Context::kNoHeapLimit) return kMaxResultBytes;
    return size_mul(budget_headroom(context), 9) / 17;
}

inline std::size_t final_slack(std::size_t size) {
    return std::max<std::size_t>(std::size_t(64) << 10, size / 8);
}

inline bool finalize_text(const Context& context, std::string& text) {
    if (text.capacity() - text.size() <= final_slack(text.size())) return true;
#if defined(NIFT_HOST_MUTANT) && NIFT_HOST_MUTANT == 10
    const std::size_t charged = size_add(text.size(), text.size());
#else
    const std::size_t charged = size_add(text.capacity(), text.size());
#endif
    if (const char* probe = std::getenv("NIFT_HOST_TRIM_PROBE"); probe && *probe)
        std::fprintf(stderr, "NIFT_TRIM charge=%zu capacity=%zu size=%zu\n", charged, text.capacity(), text.size());
    if (!reserve_result(context, charged)) return false;
    std::string exact(text.data(), text.size());
    text = std::move(exact);
    return true;
}

template <class Buffer>
inline bool finalize_bytes(const Context& context, Buffer& data) {
    if (data.capacity() - data.size() <= final_slack(data.size())) return true;
#if defined(NIFT_HOST_MUTANT) && NIFT_HOST_MUTANT == 10
    const std::size_t charged = size_add(data.size(), data.size());
#else
    const std::size_t charged = size_add(data.capacity(), data.size());
#endif
    if (const char* probe = std::getenv("NIFT_HOST_TRIM_PROBE"); probe && *probe)
        std::fprintf(stderr, "NIFT_TRIM charge=%zu capacity=%zu size=%zu\n", charged, data.capacity(), data.size());
    if (!reserve_result(context, charged)) return false;
    Buffer exact(data.begin(), data.end());
    data = std::move(exact);
    return true;
}

// Growth of a read buffer: BudgetedText charges and sizes itself exactly
// (support.cpp); a byte vector's reserve() allocates the request.
inline bool reserve_buffer(BudgetedText& data, std::size_t bytes) { return data.reserve(bytes); }
template <class Buffer>
inline bool reserve_buffer(Buffer& data, std::size_t bytes) {
    data.reserve(bytes);
    return true;
}

template <class Buffer>
inline bool grow_unknown(const Context& context, Buffer& data, std::size_t needed, std::size_t cap,
                         bool& tail_reserved, std::size_t& reserved, std::size_t limit = kSizeOverflow) {
    (void)tail_reserved;
    (void)reserved;
    if (needed > cap) return false;
    const std::size_t old = data.capacity();
    const std::size_t doubled = size_mul(old, 2);
    std::size_t target = doubled < needed ? needed : doubled;
    // B01: past half the read limit, straight to the limit (ReadLimit.h growth_target), so a
    // buffer of unknown final size holds at most 1.5 x the limit while it grows (old <= limit / 2
    // plus the new one), never a doubling beyond it.
    if (limit != kSizeOverflow && target > limit / 2) target = limit;
    if (target > cap) target = cap;
    if (target < needed) target = needed;
#ifdef _LIBCPP_VERSION
    if constexpr (std::is_same_v<Buffer, BudgetedText>) {
        // libc++ rounds long string allocations, including the terminator.
        // A 16-byte multiple also fits versions using 8-byte alignment.
        constexpr std::size_t alignment = 16;
        if (cap >= alignment - 1) {
            const std::size_t aligned = cap - (cap - (alignment - 1)) % alignment;
            if (target > aligned && needed <= aligned) target = aligned;
        }
    }
#endif
    if (!reserve_result(context, size_add(target, old))) return false;
    if (!reserve_buffer(data, target)) return false;
    // CP16 R6: the trim proof needs the bound on the capacity actually
    // allocated, not the one requested; a library that rounds past the cap
    // is refused here rather than allowed to break monotonicity later.
    if (data.capacity() > cap) return false;
    return true;
}

template <class Buffer>
inline ReadOutcome read_whole(const Context& context, const fs::path& path, Buffer& out,
                              std::string_view operation, std::string& error) {
    const auto setting = nift::readlimit::current();
    if (!setting.valid) {
        error = nift::readlimit::invalid_message(std::string(operation), setting);
        return ReadOutcome::InvalidLimit;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) return ReadOutcome::OpenFailed;
    std::uint64_t hint = 0;
    std::error_code size_error;
    if (fs::is_regular_file(path, size_error)) {
        const auto size = fs::file_size(path, size_error);
        if (!size_error) hint = size;
    }
#if !(defined(NIFT_READ_LIMIT_MUTANT) && (NIFT_READ_LIMIT_MUTANT == 1 || NIFT_READ_LIMIT_MUTANT == 5))
    // B01 (combined-b3-b5.md section 43): a regular file larger than the read limit is rejected from
    // its size, unread and before any buffer is sized from it (legacy's rule, ReadLimit.h): the
    // buffer used to be pre-sized to the whole file, so a file 3 x the limit was a bad_alloc under
    // an address-space limit (exit 70) instead of the limit error.
    if (hint > setting.bytes) {
        error = nift::readlimit::exceeded_message(std::string(operation), path.generic_string(), setting.bytes);
        return ReadOutcome::Limit;
    }
#endif
    const std::size_t headroom = budget_headroom(context);
    std::size_t cap = unknown_capacity_cap(context);
#if !(defined(NIFT_READ_LIMIT_MUTANT) && NIFT_READ_LIMIT_MUTANT == 1)
    // B01: nor does a buffer of unknown final size (FIFO, device, pipe) grow past the limit: the
    // chunk check below keeps every `needed` within it, so this cap only stops doubling beyond it.
    if (cap > setting.bytes) cap = static_cast<std::size_t>(setting.bytes);
    const std::size_t growth_limit = static_cast<std::size_t>(setting.bytes);
#else
    const std::size_t growth_limit = kSizeOverflow;
#endif
    std::size_t initial = hint ? static_cast<std::size_t>(hint)
                               : (headroom < (std::size_t(64) << 10) ? headroom : (std::size_t(64) << 10));
    if (!hint && initial > cap) initial = cap;
#if !defined(NIFT_HOST_MUTANT) || NIFT_HOST_MUTANT != 6
    if (initial && !reserve_result(context, initial)) {
        error = std::string(operation) + ": file exceeds the memory budget";
        return ReadOutcome::Budget;
    }
#endif
    std::uint64_t total = 0;
    char chunk[64 * 1024];
    if constexpr (std::is_same_v<Buffer, std::string>) {
        BudgetedText data(context);
        bool tail_reserved = false;
        std::size_t reserved = 0;
#if !defined(NIFT_HOST_MUTANT) || NIFT_HOST_MUTANT != 6
        if (initial) data.reserve(initial);
#endif
        while (in) {
#if defined(NIFT_READ_LIMIT_MUTANT) && NIFT_READ_LIMIT_MUTANT == 6
            in.readsome(chunk, sizeof chunk);  // mutant 6: what is available now, so a short read ends the read
#else
            // istream::read extracts `sizeof chunk` bytes or stops at end of file only: the filebuf
            // repeats read(2) across short reads (a FIFO or pipe written in pieces), and `total` below
            // accumulates over the whole read, so pieces add up against the limit.
            in.read(chunk, sizeof chunk);
#endif
            const std::streamsize got = in.gcount();
            if (in.bad()) return ReadOutcome::ReadFailed;
            if (got <= 0) break;
            // B01: `total <= limit` holds here, so `limit - total` cannot wrap (`limit - got` did when one
            // 64 KiB chunk was larger than the limit: a 1 KiB limit was never enforced).
#if defined(NIFT_READ_LIMIT_MUTANT) && NIFT_READ_LIMIT_MUTANT == 1
            if (total > setting.bytes - static_cast<std::uint64_t>(got)) {  // mutant 1: the pre-B01 check
#elif defined(NIFT_READ_LIMIT_MUTANT) && NIFT_READ_LIMIT_MUTANT == 3
            if (false) {  // mutant 3: no per-chunk limit check at all
#else
            if (static_cast<std::uint64_t>(got) > setting.bytes - total) {
#endif
                error = nift::readlimit::exceeded_message(std::string(operation), path.generic_string(), setting.bytes);
                return ReadOutcome::Limit;
            }
#if !defined(NIFT_HOST_MUTANT) || NIFT_HOST_MUTANT != 6
            const std::size_t needed = data.size() + static_cast<std::size_t>(got);
            if (needed > data.capacity() &&
                !grow_unknown(context, data, needed, cap, tail_reserved, reserved, growth_limit)) {
                error = std::string(operation) + ": file exceeds the memory budget";
                return ReadOutcome::Budget;
            }
            if (!data.append(std::string_view(chunk, static_cast<std::size_t>(got)))) {
                error = std::string(operation) + ": file exceeds the memory budget";
                return ReadOutcome::Budget;
            }
#endif
#if defined(NIFT_READ_LIMIT_MUTANT) && NIFT_READ_LIMIT_MUTANT == 7
            total = static_cast<std::uint64_t>(got);  // mutant 7: the count restarts at every read
#else
            total += static_cast<std::uint64_t>(got);
#endif
        }
        std::string text = data.take();
#if !defined(NIFT_HOST_MUTANT) || NIFT_HOST_MUTANT != 8
        if (!finalize_text(context, text)) {
            error = std::string(operation) + ": file exceeds the memory budget";
            return ReadOutcome::Budget;
        }
#endif
        out = std::move(text);
    } else {
        Buffer data;
        bool tail_reserved = false;
        std::size_t reserved = 0;
#if !defined(NIFT_HOST_MUTANT) || NIFT_HOST_MUTANT != 6
        if (initial) {
            if (!reserve_result(context, initial)) {
                error = std::string(operation) + ": file exceeds the memory budget";
                return ReadOutcome::Budget;
            }
            data.reserve(initial);
        }
#endif
        while (in) {
#if defined(NIFT_READ_LIMIT_MUTANT) && NIFT_READ_LIMIT_MUTANT == 6
            in.readsome(chunk, sizeof chunk);  // mutant 6: what is available now, so a short read ends the read
#else
            // istream::read extracts `sizeof chunk` bytes or stops at end of file only: the filebuf
            // repeats read(2) across short reads (a FIFO or pipe written in pieces), and `total` below
            // accumulates over the whole read, so pieces add up against the limit.
            in.read(chunk, sizeof chunk);
#endif
            const std::streamsize got = in.gcount();
            if (in.bad()) return ReadOutcome::ReadFailed;
            if (got <= 0) break;
            // B01: `total <= limit` holds here, so `limit - total` cannot wrap (`limit - got` did when one
            // 64 KiB chunk was larger than the limit: a 1 KiB limit was never enforced).
#if defined(NIFT_READ_LIMIT_MUTANT) && NIFT_READ_LIMIT_MUTANT == 1
            if (total > setting.bytes - static_cast<std::uint64_t>(got)) {  // mutant 1: the pre-B01 check
#elif defined(NIFT_READ_LIMIT_MUTANT) && NIFT_READ_LIMIT_MUTANT == 3
            if (false) {  // mutant 3: no per-chunk limit check at all
#else
            if (static_cast<std::uint64_t>(got) > setting.bytes - total) {
#endif
                error = nift::readlimit::exceeded_message(std::string(operation), path.generic_string(), setting.bytes);
                return ReadOutcome::Limit;
            }
#if !defined(NIFT_HOST_MUTANT) || NIFT_HOST_MUTANT != 6
            const std::size_t needed = data.size() + static_cast<std::size_t>(got);
            if (needed > data.capacity() &&
                !grow_unknown(context, data, needed, cap, tail_reserved, reserved, growth_limit)) {
                error = std::string(operation) + ": file exceeds the memory budget";
                return ReadOutcome::Budget;
            }
#endif
            data.insert(data.end(), chunk, chunk + got);
#if defined(NIFT_READ_LIMIT_MUTANT) && NIFT_READ_LIMIT_MUTANT == 7
            total = static_cast<std::uint64_t>(got);  // mutant 7: the count restarts at every read
#else
            total += static_cast<std::uint64_t>(got);
#endif
        }
#if !defined(NIFT_HOST_MUTANT) || NIFT_HOST_MUTANT != 8
        if (!finalize_bytes(context, data)) {
            error = std::string(operation) + ": file exceeds the memory budget";
            return ReadOutcome::Budget;
        }
#endif
        out = std::move(data);
    }
    return ReadOutcome::Ok;
}

} // namespace nift::rt::natives
