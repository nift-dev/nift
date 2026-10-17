// CP09 value-model micro benchmarks: rt::Value vs legacy nift::RuntimeValue.
//
//   value_bench <rt|legacy> <copy|push|index|objset|objget>
//
// Each run executes one workload so callgrind (tools/rt-bench.sh) can count
// instructions per engine and workload; counts are deterministic, wall time is
// not used.
#include "RuntimeValue.h"
#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/string.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace nift::rt;
using nift::RuntimeValue;

namespace {

constexpr int kItems = 10000;
constexpr int kCopies = 1000;
constexpr int kKeys = 64;
constexpr int kRounds = 2000;
volatile double sink = 0;

std::vector<std::string> key_names() {
    std::vector<std::string> keys;
    for (int i = 0; i < kKeys; ++i) keys.push_back("field_" + std::to_string(i));
    return keys;
}

void rt_workload(const char* name) {
    if (!std::strcmp(name, "copy")) {
        Value a = make_array(kItems);
        for (int i = 0; i < kItems; ++i) array_push(a, make_string("item " + std::to_string(i)));
        std::vector<Value> copies(kCopies);
        for (auto& c : copies) c = a;
        sink = static_cast<double>(array_size(copies.back()));
    } else if (!std::strcmp(name, "push") || !std::strcmp(name, "index")) {
        Value a = make_array(0);
        for (int i = 0; i < kItems * 10; ++i) array_push(a, Value::number(i));
        if (!std::strcmp(name, "index")) {
            double total = 0;
            for (int r = 0; r < 10; ++r)
                for (const Value& v : array_items(a)) total += v.as_double();
            sink = total;
        }
    } else {
        const auto names = key_names();
        std::vector<Atom> keys;
        for (const auto& n : names) keys.push_back(atom(n));
        Value o = make_object();
        for (int r = 0; r < (std::strcmp(name, "objset") ? 1 : kRounds); ++r)
            for (int k = 0; k < kKeys; ++k) object_set(o, keys[k], Value::number(r + k));
        if (!std::strcmp(name, "objget")) {
            double total = 0;
            for (int r = 0; r < kRounds; ++r)
                for (int k = 0; k < kKeys; ++k) total += object_get(o, keys[k])->as_double();
            sink = total;
        }
    }
}

void legacy_workload(const char* name) {
    if (!std::strcmp(name, "copy")) {
        RuntimeValue a = RuntimeValue::make_array();
        for (int i = 0; i < kItems; ++i) a.push_back(RuntimeValue("item " + std::to_string(i)));
        std::vector<RuntimeValue> copies(kCopies);
        for (auto& c : copies) c = a;
        sink = static_cast<double>(copies.back().array.size());
    } else if (!std::strcmp(name, "push") || !std::strcmp(name, "index")) {
        RuntimeValue a = RuntimeValue::make_array();
        for (int i = 0; i < kItems * 10; ++i) a.push_back(RuntimeValue(static_cast<double>(i)));
        if (!std::strcmp(name, "index")) {
            double total = 0;
            for (int r = 0; r < 10; ++r)
                for (const RuntimeValue& v : a.array) total += v.num;
            sink = total;
        }
    } else {
        const auto keys = key_names();
        RuntimeValue o = RuntimeValue::make_object();
        for (int r = 0; r < (std::strcmp(name, "objset") ? 1 : kRounds); ++r)
            for (int k = 0; k < kKeys; ++k) o[keys[k]] = RuntimeValue(static_cast<double>(r + k));
        if (!std::strcmp(name, "objget")) {
            double total = 0;
            for (int r = 0; r < kRounds; ++r)
                for (int k = 0; k < kKeys; ++k) total += o.find_member(keys[k])->num;
            sink = total;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && !std::strcmp(argv[1], "sizes")) {
        std::printf("sizeof rt::Value %zu, sizeof legacy RuntimeValue %zu\n", sizeof(Value), sizeof(RuntimeValue));
        return 0;
    }
    if (argc != 3) { std::fprintf(stderr, "usage: value_bench <rt|legacy> <workload> | sizes\n"); return 2; }
    if (!std::strcmp(argv[1], "rt")) rt_workload(argv[2]);
    else legacy_workload(argv[2]);
    return 0;
}
