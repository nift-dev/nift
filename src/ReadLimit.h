#pragma once
// feature-host-deps (D-028): bounded whole reads.
//
// Every operation that materialises a whole file, the rest of a stream, one
// stream line/token or a process capture in memory is bounded by one
// configurable limit. The bound is enforced while reading (st_size is only a
// hint: pipes and devices report 0), the buffer never holds or reserves more
// than the limit (the one-byte probe that proves "too large" goes to a small
// separate buffer), and the arithmetic cannot overflow. Exceeding it is a
// fatal, uncatchable limit error naming the operation and the limit; nothing
// is ever silently truncated.
//
// Configuration: NIFT_READ_LIMIT=<bytes>[K|M|G|T|KiB|MiB|GiB|TiB] (binary
// multiples, at least 1 byte). Unset or empty: kDefaultReadLimit (1 GiB, the VM heap budget,
// so a value one engine can hold the other can too). An invalid value is
// reported as an error by every bounded read (never silently replaced).
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <istream>
#include <limits>
#include <string>

namespace nift::readlimit {

inline constexpr std::uint64_t kDefaultReadLimit = std::uint64_t(1) << 30;  // 1 GiB
inline constexpr const char* kSettingName = "NIFT_READ_LIMIT";
inline constexpr std::size_t kChunk = 64 * 1024;

struct Setting {
    std::uint64_t bytes = kDefaultReadLimit;
    bool valid = true;
    std::string raw;  // the rejected text when !valid
};

// Parses the setting text (exposed for unit tests). Overflow-safe.
inline Setting parse(const char* value) {
    Setting setting;
    if (!value || !*value) return setting;
    const std::string text(value);
    std::size_t i = 0;
    std::uint64_t number = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        const std::uint64_t digit = static_cast<std::uint64_t>(text[i] - '0');
        if (number > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) { setting.valid = false; break; }
        number = number * 10 + digit;
        ++i;
    }
    std::string suffix = setting.valid ? text.substr(i) : std::string();
    for (auto& c : suffix) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    unsigned shift = 0;
    if (suffix.empty() || suffix == "B") shift = 0;
    else if (suffix == "K" || suffix == "KB" || suffix == "KIB") shift = 10;
    else if (suffix == "M" || suffix == "MB" || suffix == "MIB") shift = 20;
    else if (suffix == "G" || suffix == "GB" || suffix == "GIB") shift = 30;
    else if (suffix == "T" || suffix == "TB" || suffix == "TIB") shift = 40;
    else setting.valid = false;
    if (i == 0 || number == 0) setting.valid = false;  // no digits, or zero (0 would mean "unbounded" to the process layer)
    if (setting.valid && shift && number > (std::numeric_limits<std::uint64_t>::max() >> shift)) setting.valid = false;
    if (!setting.valid) { setting.bytes = kDefaultReadLimit; setting.raw = text; return setting; }
    setting.bytes = number << shift;
    return setting;
}

inline Setting current() { return parse(std::getenv(kSettingName)); }

// "<op>: <what> exceeds the read limit of N bytes (NIFT_READ_LIMIT)".
inline std::string exceeded_message(const std::string& operation, const std::string& what, std::uint64_t limit) {
    return operation + ": " + what + " exceeds the read limit of " + std::to_string(limit) + " bytes (" +
           kSettingName + ")";
}
inline std::string invalid_message(const std::string& operation, const Setting& setting) {
    return operation + ": invalid " + std::string(kSettingName) + " value '" + setting.raw +
           "' (expected a byte count, optionally with K, M, G or T)";
}

enum class Status { Ok, Exceeded, ReadError };

// Grows `buffer` so that it can hold `needed` bytes, never reserving more than
// `limit` (callers guarantee needed <= limit). Doubling keeps appends linear;
// once a doubling would pass limit/2 the buffer jumps straight to `limit`, so
// the buffer being replaced is never larger than limit/2 and the transient
// address space of a growth step stays below 1.5 x limit (regular files are
// pre-sized from st_size and normally never grow).
inline std::uint64_t growth_target(std::uint64_t capacity, std::uint64_t needed, std::uint64_t limit,
                                   std::uint64_t minimum) {
    std::uint64_t target = capacity > limit / 2 ? limit : capacity * 2;
    if (target < needed) target = needed;
    if (target < minimum) target = minimum;
    if (target > limit / 2) target = limit;
    return target;
}
template <class Buffer>
inline void reserve_within(Buffer& buffer, std::uint64_t needed, std::uint64_t limit, std::uint64_t minimum = kChunk) {
    if (needed <= buffer.capacity()) return;
    std::uint64_t target = growth_target(static_cast<std::uint64_t>(buffer.capacity()), needed, limit, minimum);
    if (target > buffer.max_size()) target = buffer.max_size();
    // Reserve into a fresh buffer: std::string::reserve on a non-empty string
    // may round a request up to twice the old capacity (libstdc++), which
    // would pass the limit; an empty buffer reserves exactly.
    Buffer grown;
    grown.reserve(static_cast<std::size_t>(target));
    grown.insert(grown.end(), buffer.begin(), buffer.end());
    buffer.swap(grown);
}

// Appends the rest of `in` (up to `max_bytes` more bytes when max_bytes is
// not npos-like) to `out`, which never exceeds `limit` bytes in size or
// capacity. Returns Exceeded as soon as one byte beyond the limit is seen
// (the buffer is then released), ReadError when the stream reports badbit.
// `size_hint` (a regular file's size, 0 if unknown) only pre-sizes the buffer.
template <class Buffer>
inline Status read_bounded(std::istream& in, Buffer& out, std::uint64_t limit,
                           std::uint64_t max_bytes = std::numeric_limits<std::uint64_t>::max(),
                           std::uint64_t size_hint = 0) {
    static_assert(sizeof(typename Buffer::value_type) == 1, "byte buffer");
    if (size_hint && out.size() < limit) {
        // Exact pre-size for a regular file (one allocation, no growth).
        const std::uint64_t want = size_hint < max_bytes ? size_hint : max_bytes;
        const std::uint64_t room = limit - out.size();
        const std::uint64_t exact = out.size() + (want < room ? want : room);
        if (exact > out.capacity() && exact <= out.max_size()) {
            Buffer sized;
            sized.reserve(static_cast<std::size_t>(exact));
            sized.insert(sized.end(), out.begin(), out.end());
            out.swap(sized);
        }
    }
    char chunk[kChunk];
    std::uint64_t remaining = max_bytes;
    const std::uint64_t minimum = max_bytes < kChunk ? max_bytes : kChunk;
    while (remaining > 0 && in) {
        const std::uint64_t held = out.size();
        const std::uint64_t room = held < limit ? limit - held : 0;
        // Ask for at most one byte beyond the room: enough to prove "too large".
        std::uint64_t request = room < kChunk ? room + 1 : kChunk;
        if (request > remaining) request = remaining;
        in.read(chunk, static_cast<std::streamsize>(request));
        const std::streamsize got_signed = in.gcount();
        if (in.bad()) return Status::ReadError;
        if (got_signed <= 0) break;
        const std::uint64_t got = static_cast<std::uint64_t>(got_signed);
        if (got > room) {
            Buffer().swap(out);  // release, never truncate silently
            return Status::Exceeded;
        }
        reserve_within(out, held + got, limit, minimum);
        out.insert(out.end(), chunk, chunk + got);
        remaining -= got;
        if (got < request) break;
    }
    if (in.bad()) return Status::ReadError;
    return Status::Ok;
}

// Appends one byte to a line/token buffer that may hold at most `limit`
// bytes; false (nothing appended) once the buffer is full.
inline bool append_bounded(std::string& out, char c, std::uint64_t limit) {
    if (out.size() >= limit) return false;
    reserve_within(out, static_cast<std::uint64_t>(out.size()) + 1, limit, 32);
    out.push_back(c);
    return true;
}

} // namespace nift::readlimit
