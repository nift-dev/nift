// Focused benchmark for to_upper()/to_lower() (DeepSeek case-conversion task).
// Linked against the CP14 natives and the CP09 value model only. Reports
// nanoseconds per call for a 100,000-byte ASCII string and a short string.
#include "rt/natives/builtin.h"
#include "rt/string.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace nift::rt;
using namespace nift::rt::natives;

namespace {

double run(BuiltinId id, const Value& input, std::size_t reps, std::size_t& checksum) {
    const BuiltinSpec& spec = builtin_spec(id);
    const Context context;
    std::string error;
    Value out;
    std::vector<Value> args{input};
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t k = 0; k < reps; ++k) {
        if (call_builtin(spec, context, args.data(), 1, out, error) != Status::Ok) {
            std::fprintf(stderr, "case conversion failed: %s\n", error.c_str());
            std::exit(1);
        }
        if (out.is(Kind::String)) checksum += static_cast<unsigned char>(string_view_of(out)[0]);
    }
    const auto stop = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(stop - start).count();
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t reps = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 200;
    const Value long_lower_input = make_string(std::string(100000, 'A'));
    const Value long_upper_input = make_string(std::string(100000, 'a'));
    const Value short_lower_input = make_string("AbCdEfGh");
    const Value short_upper_input = make_string("aBcDeFgH");
    std::size_t checksum = 0;
    const double long_upper_ns = run(BuiltinId::StrToUpper, long_upper_input, reps, checksum);
    const double long_lower_ns = run(BuiltinId::StrToLower, long_lower_input, reps, checksum);
    const double short_upper_ns = run(BuiltinId::StrToUpper, short_upper_input, reps * 100, checksum);
    const double short_lower_ns = run(BuiltinId::StrToLower, short_lower_input, reps * 100, checksum);
    std::printf("long_100k_upper_ns_per_call %.2f\n", long_upper_ns / static_cast<double>(reps));
    std::printf("long_100k_lower_ns_per_call %.2f\n", long_lower_ns / static_cast<double>(reps));
    std::printf("short_8_upper_ns_per_call %.2f\n", short_upper_ns / static_cast<double>(reps * 100));
    std::printf("short_8_lower_ns_per_call %.2f\n", short_lower_ns / static_cast<double>(reps * 100));
    std::printf("checksum %zu\n", checksum);
    return 0;
}
