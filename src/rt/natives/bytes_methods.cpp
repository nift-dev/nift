#include "rt/natives/bytes_methods.h"

#include "Sha256.h"
#include "rt/aggregate.h"
#include "rt/decimal.h"
#include "rt/natives/support.h"
#include "rt/number.h"
#include "rt/string.h"

#include <cmath>
#include <vector>

namespace nift::rt::natives {

namespace {

Status internal(std::string& error, const char* what) {
    error = what;
    return Status::Internal;
}

// Legacy runtime_number_to_unsigned(v, 8): an integral number in [0, 255].
bool byte_value(const Value& v, std::uint8_t& out) {
    switch (v.kind()) {
        case Kind::Int:
            if (v.as_int() < 0 || v.as_int() > 255) return false;
            out = static_cast<std::uint8_t>(v.as_int());
            return true;
        case Kind::Double: {
            const double d = v.as_double();
            if (!std::isfinite(d) || std::trunc(d) != d || d < 0 || d >= 256.0) return false;
            out = static_cast<std::uint8_t>(d);
            return true;
        }
        case Kind::Decimal: {
            std::int64_t i = 0;
            if (!number_to_i64(v, i) || i < 0 || i > 255) return false;
            out = static_cast<std::uint8_t>(i);
            return true;
        }
        default: return false;
    }
}

// "utf-8" exactly (legacy compares the string; no aliases, no case folding).
bool utf8_name(const Value& v) {
    if (!v.is(Kind::String)) return false;
    const std::string_view name = string_view_of(v);
    return name.size() == 5 && name[0] == 'u' && name[1] == 't' && name[2] == 'f' && name[3] == '-' && name[4] == '8';
}

const std::vector<std::uint8_t>* bytes_receiver(const Value* args, std::uint32_t argc) {
    return argc > 0 && args[0].is(Kind::Bytes) ? &bytes_of(args[0]) : nullptr;
}

} // namespace

bool strict_utf8(std::string_view value) {
    for (std::size_t i = 0; i < value.size();) {
        const auto first = static_cast<unsigned char>(value[i]);
        std::size_t width = 0;
        std::uint32_t codepoint = 0;
        if (first <= 0x7f) { ++i; continue; }
        if (first >= 0xc2 && first <= 0xdf) { width = 2; codepoint = first & 0x1f; }
        else if (first >= 0xe0 && first <= 0xef) { width = 3; codepoint = first & 0x0f; }
        else if (first >= 0xf0 && first <= 0xf4) { width = 4; codepoint = first & 0x07; }
        else return false;
        if (i + width > value.size()) return false;
        for (std::size_t j = 1; j < width; ++j) {
            const auto continuation = static_cast<unsigned char>(value[i + j]);
            if ((continuation & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (continuation & 0x3f);
        }
        if ((width == 3 && codepoint < 0x800) || (width == 4 && codepoint < 0x10000) ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff)
            return false;
        i += width;
    }
    return true;
}

Status fn_bytes(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc > 1) return fail(error, "bytes: expected zero arguments or one byte array");
    std::vector<std::uint8_t> data;
    if (argc == 1) {
        if (!args[0].is(Kind::Array)) return fail(error, "bytes: expected an array of integers");
        const auto& items = array_items(args[0]);
        data.reserve(items.size());
        for (const Value& item : items) {
            std::uint8_t byte = 0;
            if (!byte_value(item, byte)) return fail(error, "bytes: array values must be integers from 0 to 255");
            data.push_back(byte);
        }
    }
    out = make_bytes(static_cast<std::vector<std::uint8_t>&&>(data));
    return Status::Ok;
}

Status str_encode(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc == 0 || !args[0].is(Kind::String)) return internal(error, "encode receiver");
    if (argc != 2) return fail(error, "encode: expected encoding");
    if (!utf8_name(args[1])) return fail(error, "encode: encoding must be exactly 'utf-8'");
    const std::string_view text = string_view_of(args[0]);
    if (!strict_utf8(text)) return fail(error, "encode: invalid UTF-8 text");
    out = make_bytes(std::vector<std::uint8_t>(text.begin(), text.end()));
    return Status::Ok;
}

Status bytes_length(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* data = bytes_receiver(args, argc);
    if (!data) return internal(error, "length receiver");
    if (!no_arguments(argc, "length", error)) return Status::Error;
    out = count_value(data->size());
    return Status::Ok;
}

Status bytes_size(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* data = bytes_receiver(args, argc);
    if (!data) return internal(error, "size receiver");
    if (!no_arguments(argc, "size", error)) return Status::Error;
    out = count_value(data->size());
    return Status::Ok;
}

Status bytes_empty(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* data = bytes_receiver(args, argc);
    if (!data) return internal(error, "empty receiver");
    if (!no_arguments(argc, "empty", error)) return Status::Error;
    out = Value::boolean(data->empty());
    return Status::Ok;
}

Status bytes_slice(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* data = bytes_receiver(args, argc);
    if (!data) return internal(error, "slice receiver");
    if (argc < 2 || argc > 3) return fail(error, "slice: expected start and optional end");
    std::size_t begin = 0, end = data->size();
    if (!number_to_size(args[1], begin)) return fail(error, "slice: invalid start");
    if (argc == 3 && !number_to_size(args[2], end)) return fail(error, "slice: invalid end");
    begin = std::min(begin, data->size());
    end = std::min(end, data->size());
    if (end < begin) end = begin;
    out = make_bytes(std::vector<std::uint8_t>(data->begin() + static_cast<std::ptrdiff_t>(begin),
                                               data->begin() + static_cast<std::ptrdiff_t>(end)));
    return Status::Ok;
}

Status bytes_decode(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* data = bytes_receiver(args, argc);
    if (!data) return internal(error, "decode receiver");
    if (argc != 2) return fail(error, "decode: expected encoding");
    if (!utf8_name(args[1])) return fail(error, "decode: encoding must be exactly 'utf-8'");
    // An empty vector's data() may be null: never pass (null, 0) on (CP09
    // make_string memcpy's its argument; UBSan rejects a null source).
    static const char kEmpty[] = "";
    const std::string_view text(data->empty() ? kEmpty : reinterpret_cast<const char*>(data->data()), data->size());
    if (!strict_utf8(text)) return fail(error, "decode: invalid UTF-8");
    out = make_string(text);
    return Status::Ok;
}

Status fn_sha256(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc != 1) return fail(error, "sha256: expected one string or bytes value");
    nift::crypto::Sha256Digest digest;
    if (args[0].is(Kind::Bytes)) {
        const auto& data = bytes_of(args[0]);
        static const std::uint8_t kNone = 0;
        digest = nift::crypto::sha256_bytes(data.empty() ? &kNone : data.data(), data.size());
    } else if (args[0].is(Kind::String)) {
        const std::string_view text = string_view_of(args[0]);
        digest = nift::crypto::sha256_bytes(text.data(), text.size());
    } else {
        return fail(error, "sha256: expected a string or bytes value");
    }
    char hex[nift::crypto::kSha256DigestSize * 2];
    nift::crypto::sha256_hex(digest, hex);
    out = make_string(std::string_view(hex, sizeof hex));
    return Status::Ok;
}

} // namespace nift::rt::natives
