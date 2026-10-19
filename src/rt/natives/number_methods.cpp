#include "rt/natives/number_methods.h"

#include "rt/natives/support.h"
#include "rt/number.h"

#include <cmath>

namespace nift::rt::natives {

namespace {

Status rounded(const Value* args, std::uint32_t argc, Value& out, std::string& error, std::string_view name,
               double (*f)(double)) {
    if (argc == 0 || !args[0].is_number()) { error = "number receiver"; return Status::Internal; }
    if (!no_arguments(argc, name, error)) return Status::Error;
    // Legacy applies the C function to the nearest double (`base.num`):
    // inexact beyond 2^53 (design record N-Q1).
    out = Value::number(f(number_approx(args[0])));
    return Status::Ok;
}

double absolute(double d) { return std::fabs(d); }
double down(double d) { return std::floor(d); }
double up(double d) { return std::ceil(d); }
double nearest(double d) { return std::round(d); }  // halves away from zero

} // namespace

Status num_to_int(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc == 0 || !args[0].is_number()) { error = "to_int receiver"; return Status::Internal; }
    if (!no_arguments(argc, "to_int", error)) return Status::Error;
    std::int64_t v = 0;
    if (!number_to_i64(args[0], v)) return fail(error, "to_int: invalid or out-of-range integer");
    out = Value::integer(v);  // legacy runtime_integer: exact at any int64
    return Status::Ok;
}

Status num_abs(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return rounded(args, argc, out, error, "abs", absolute);
}

Status num_floor(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return rounded(args, argc, out, error, "floor", down);
}

Status num_ceil(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return rounded(args, argc, out, error, "ceil", up);
}

Status num_round(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return rounded(args, argc, out, error, "round", nearest);
}

} // namespace nift::rt::natives
