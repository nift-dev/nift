// Runtime 2.0 CP11: in-process VM runner for measurements and debugging.
//
//   vm-run [--repeat N] [--dump] FILE
//
// Parses and compiles FILE once, then executes the compiled Program N times
// (default 1) in the same process. The first run's output goes to stdout;
// one JSON line on stderr reports parse/compile time and the execution time
// of the first ("cold": first touch of code and data) and later ("warm")
// runs. No legacy engine is involved; a program that does not compile or
// that bails is reported as such (exit 2).
#include "rt_vm_tool.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

int main(int argc, char** argv) {
    int repeat = 1;
    bool dump = false;
    const char* file = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--repeat") && i + 1 < argc) repeat = std::max(1, std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--dump")) dump = true;
        else file = argv[i];
    }
    if (!file) { std::fprintf(stderr, "usage: vm-run [--repeat N] [--dump] FILE\n"); return 2; }
    std::ifstream in(file, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    rt_vm_tool::Compiled compiled = rt_vm_tool::compile(buffer.str());
    const auto t1 = Clock::now();
    if (!compiled.program) { std::fprintf(stderr, "not compiled: %s\n", compiled.reason.c_str()); return 2; }
    if (dump) std::cerr << nift::rt::disassemble(*compiled.program);
    std::vector<double> runs;
    for (int k = 0; k < repeat; ++k) {
        std::string output;
        const auto s = Clock::now();
        nift::rt::Vm vm(*compiled.program);
        const nift::rt::VmResult result = vm.run(output);
        runs.push_back(std::chrono::duration<double, std::micro>(Clock::now() - s).count());
        if (result.reason != nift::rt::BailReason::None) {
            std::fprintf(stderr, "bail: %s (%s)\n", nift::rt::bail_reason_name(result.reason), result.message.c_str());
            return 2;
        }
        if (k == 0) {
            std::fwrite(output.data(), 1, output.size(), stdout);
            if (result.returned && !result.return_text.empty()) std::printf("%s\n", result.return_text.c_str());
        }
    }
    std::vector<double> warm(runs.begin() + 1, runs.end());
    std::sort(warm.begin(), warm.end());
    const double warm_median = warm.empty() ? runs[0] : warm[warm.size() / 2];
    std::fprintf(stderr, "{\"compile_us\":%.1f,\"cold_run_us\":%.1f,\"warm_run_median_us\":%.1f,\"runs\":%d}\n",
                 std::chrono::duration<double, std::micro>(t1 - t0).count(), runs[0], warm_median, repeat);
    return 0;
}
