#pragma once
// CP16 host built-in stream reads: budgeted incremental buffering for files,
// pipes and other unknown-size inputs.
#include "rt/natives/host_util.h"

#include "ReadLimit.h"
#include "rt/heap_account.h"
#include "rt/natives/support.h"

#include <algorithm>
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

inline std::size_t final_slack(std::size_t size) {
    return std::max<std::size_t>(std::size_t(64) << 10, size / 8);
}

inline bool finalize_text(const Context& context, std::string& text) {
    if (text.capacity() - text.size() <= final_slack(text.size())) return true;
    if (!reserve_result(context, size_add(text.size(), text.size()))) return false;
    std::string exact(text.data(), text.size());
    text = std::move(exact);
    return true;
}

template <class Buffer>
inline bool finalize_bytes(const Context& context, Buffer& data) {
    if (data.capacity() - data.size() <= final_slack(data.size())) return true;
    if (!reserve_result(context, size_add(data.size(), data.size()))) return false;
    Buffer exact(data.begin(), data.end());
    data = std::move(exact);
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
    const std::size_t headroom = budget_headroom(context);
    const std::size_t initial = hint ? static_cast<std::size_t>(hint)
                                    : (headroom < (std::size_t(64) << 10) ? headroom : (std::size_t(64) << 10));
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
#if !defined(NIFT_HOST_MUTANT) || NIFT_HOST_MUTANT != 6
        if (initial) data.reserve(initial);
#endif
        while (in) {
            in.read(chunk, sizeof chunk);
            const std::streamsize got = in.gcount();
            if (in.bad()) return ReadOutcome::ReadFailed;
            if (got <= 0) break;
            if (total > setting.bytes - static_cast<std::uint64_t>(got)) {
                error = nift::readlimit::exceeded_message(std::string(operation), path.generic_string(), setting.bytes);
                return ReadOutcome::Limit;
            }
#if !defined(NIFT_HOST_MUTANT) || NIFT_HOST_MUTANT != 6
            const std::size_t needed = data.size() + static_cast<std::size_t>(got);
            if (!tail_reserved && hint == 0 && context.heap_limit != Context::kNoHeapLimit &&
                data.size() >= (std::size_t(1) << 20) && needed > data.capacity()) {
                const std::size_t available = budget_headroom(context);
                const std::size_t margin = std::max<std::size_t>(std::size_t(4) << 20, available / 16);
                const std::size_t target = available > margin ? available - margin : 0;
                if (target < needed || !data.reserve(target)) {
                    error = std::string(operation) + ": file exceeds the memory budget";
                    return ReadOutcome::Budget;
                }
                tail_reserved = true;
            }
            if (!data.append(std::string_view(chunk, static_cast<std::size_t>(got)))) {
                error = std::string(operation) + ": file exceeds the memory budget";
                return ReadOutcome::Budget;
            }
#endif
            total += static_cast<std::uint64_t>(got);
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
            in.read(chunk, sizeof chunk);
            const std::streamsize got = in.gcount();
            if (in.bad()) return ReadOutcome::ReadFailed;
            if (got <= 0) break;
            if (total > setting.bytes - static_cast<std::uint64_t>(got)) {
                error = nift::readlimit::exceeded_message(std::string(operation), path.generic_string(), setting.bytes);
                return ReadOutcome::Limit;
            }
#if !defined(NIFT_HOST_MUTANT) || NIFT_HOST_MUTANT != 6
            const std::size_t needed = data.size() + static_cast<std::size_t>(got);
            if (needed > data.capacity()) {
                if (!tail_reserved && hint == 0 && context.heap_limit != Context::kNoHeapLimit &&
                    data.size() >= (std::size_t(1) << 20)) {
                    const std::size_t available = budget_headroom(context);
                    const std::size_t margin = std::max<std::size_t>(std::size_t(4) << 20, available / 16);
                    const std::size_t target = available > margin ? available - margin : 0;
                    if (target < needed || !reserve_result(context, size_add(target, data.capacity()))) {
                        error = std::string(operation) + ": file exceeds the memory budget";
                        return ReadOutcome::Budget;
                    }
                    data.reserve(target);
                    tail_reserved = true;
                } else {
                    const std::size_t old = data.capacity();
                    const std::size_t doubled = size_mul(old, 2);
                    const std::size_t target = doubled < needed ? needed : doubled;
                    if (!reserve_result(context, size_add(target, old))) {
                        error = std::string(operation) + ": file exceeds the memory budget";
                        return ReadOutcome::Budget;
                    }
                    data.reserve(target);
                }
            }
#endif
            data.insert(data.end(), chunk, chunk + got);
            total += static_cast<std::uint64_t>(got);
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
