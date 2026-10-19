// Focused tests for the ASCII case-conversion fast path (DeepSeek assignment).
// The contract is byte-for-byte identical to the C locale's toupper/tolower
// for every byte 0..255, independent of the process locale. Bytes >= 0x80 are
// never changed, so valid and invalid UTF-8 both pass through unchanged.
#include "rt/natives/builtin.h"
#include "rt/string.h"
#include "rt_test.h"

#include <clocale>
#include <cstdio>
#include <string>
#include <vector>

using namespace nift::rt;
using namespace nift::rt::natives;

namespace {

struct Result {
    Status status;
    Value out;
    std::string error;
};

Result call(BuiltinId id, const Value& receiver, std::size_t extra_args = 0) {
    std::vector<Value> args{receiver};
    for (std::size_t k = 0; k < extra_args; ++k) args.push_back(Value::integer(1));
    Result r{Status::Ok, Value(), {}};
    const Context context;
    r.status = call_builtin(builtin_spec(id), context, args.data(), static_cast<std::uint32_t>(args.size()), r.out,
                            r.error);
    return r;
}

std::string text_of(const Value& value) { return std::string(string_view_of(value)); }

// Fixed ASCII oracle, independent of the process locale and of the C library.
// This is the approved CP14 contract: A-Z and a-z only; every other byte is
// unchanged.
unsigned char ascii_upper(unsigned char c) {
    return c >= 'a' && c <= 'z' ? static_cast<unsigned char>(c - ('a' - 'A')) : c;
}

unsigned char ascii_lower(unsigned char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
}

std::string expected(const std::string& text, bool upper) {
    std::string out = text;
    for (char& c : out) {
        const unsigned char byte = static_cast<unsigned char>(c);
        c = static_cast<char>(upper ? ascii_upper(byte) : ascii_lower(byte));
    }
    return out;
}

void check_conversion(BuiltinId id, const std::string& input, bool upper) {
    const Result r = call(id, make_string(input));
    if (r.status != Status::Ok || !r.out.is(Kind::String)) {
        CHECK(false);
        return;
    }
    CHECK(text_of(r.out) == expected(input, upper));
}

} // namespace

int main() {
    const BuiltinId upper = BuiltinId::StrToUpper;
    const BuiltinId lower = BuiltinId::StrToLower;

    // Every byte value, including NUL, control bytes, ASCII and invalid UTF-8.
    if (std::setlocale(LC_CTYPE, "C") == nullptr) {
        std::fprintf(stderr, "setlocale(C) failed\n");
        return 1;
    }
    for (int value = 0; value < 256; ++value) {
        const std::string one(1, static_cast<char>(value));
        const unsigned char byte = static_cast<unsigned char>(value);
        const std::string want_upper(1, static_cast<char>(ascii_upper(byte)));
        const std::string want_lower(1, static_cast<char>(ascii_lower(byte)));
        const Result up = call(upper, make_string(one));
        const Result lo = call(lower, make_string(one));
        CHECK(up.status == Status::Ok && up.out.is(Kind::String));
        CHECK(lo.status == Status::Ok && lo.out.is(Kind::String));
        CHECK(text_of(up.out) == want_upper);
        CHECK(text_of(lo.out) == want_lower);
    }

    // All byte values in one string, both directions.
    std::string all;
    for (int value = 0; value < 256; ++value) all.push_back(static_cast<char>(value));
    check_conversion(upper, all, true);
    check_conversion(lower, all, false);

    // Empty and short strings, including the inline/heap boundary.
    check_conversion(upper, "", true);
    check_conversion(lower, "", false);
    for (std::size_t n = 1; n <= 64; ++n) {
        std::string input;
        for (std::size_t k = 0; k < n; ++k) input.push_back(static_cast<char>('a' + (k % 26)));
        check_conversion(upper, input, true);
        check_conversion(lower, input, false);
    }

    // Valid UTF-8: ASCII bytes change; multi-byte sequences are untouched.
    check_conversion(upper, "a\xc3\xa9z", true);
    check_conversion(lower, "A\xc3\x89Z", false);

    // Invalid UTF-8 is data, not a reason to fail.
    check_conversion(upper, "\xff\xfe a", true);
    check_conversion(lower, "\xff\xfe A", false);

    // Large input: 1 MiB of ASCII.
    {
        const std::string input(1u << 20, 'a');
        const Result r = call(upper, make_string(input));
        CHECK(r.status == Status::Ok);
        CHECK(r.out.is(Kind::String));
        const std::string out = text_of(r.out);
        CHECK(out.size() == input.size());
        CHECK(out.find_first_not_of('A') == std::string::npos);
    }

    // Locale independence: the same bytes must map the same way under a UTF-8
    // locale. Skip only when no UTF-8 locale is installed on the host.
    {
        const char* saved = std::setlocale(LC_CTYPE, nullptr);
        const std::string saved_copy = saved ? saved : "C";
        bool checked = false;
        for (const char* candidate : {"C.UTF-8", "C.utf8", "en_US.UTF-8", "en_US.utf8", "de_DE.UTF-8",
                                       "tr_TR.UTF-8", "tr_TR.ISO-8859-9"}) {
            if (std::setlocale(LC_CTYPE, candidate) == nullptr) continue;
            for (int value = 0; value < 256; ++value) {
                const std::string one(1, static_cast<char>(value));
                const Result up = call(upper, make_string(one));
                const Result lo = call(lower, make_string(one));
                const std::string want_up(1, static_cast<char>(ascii_upper(static_cast<unsigned char>(value))));
                const std::string want_lo(1, static_cast<char>(ascii_lower(static_cast<unsigned char>(value))));
                CHECK(up.status == Status::Ok && up.out.is(Kind::String) && text_of(up.out) == want_up);
                CHECK(lo.status == Status::Ok && lo.out.is(Kind::String) && text_of(lo.out) == want_lo);
            }
            checked = true;
            break;
        }
        std::setlocale(LC_CTYPE, saved_copy.c_str());
        if (!checked) std::fprintf(stderr, "note: no UTF-8 locale installed; locale-independence check skipped\n");
    }

    // Argument and receiver errors are unchanged.
    {
        const Result arity = call(upper, make_string("a"), 1);
        CHECK(arity.status == Status::Error);
        CHECK(arity.error == "to_upper: expected no arguments");
        const Result wrong_receiver = call(upper, Value::integer(1));
        CHECK(wrong_receiver.status == Status::Internal);
    }

    return rt_test::finish("case-conversion-unit");
}
