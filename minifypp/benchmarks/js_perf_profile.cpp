#include <minify/Minify.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: js-perf-profile [--runs N] [--mode default|structured|aggressive] file.js ...\n";
        return 2;
    }
    int runs = 5;
    int first = 1;
    std::string selected_mode;
    while (first + 1 < argc) {
        const std::string option = argv[first];
        if (option == "--runs") {
            runs = std::max(1, std::stoi(argv[first + 1]));
            first += 2;
        } else if (option == "--mode") {
            selected_mode = argv[first + 1];
            if (selected_mode != "default" && selected_mode != "structured" &&
                selected_mode != "aggressive") {
                std::cerr << "unknown mode: " << selected_mode << "\n";
                return 2;
            }
            first += 2;
        } else {
            break;
        }
    }
    if (first >= argc) {
        std::cerr << "no input files supplied\n";
        return 2;
    }
    std::cout << "file,bytes,mode,run,elapsed_ms,output_bytes\n";
    for (int arg = first; arg < argc; ++arg) {
        const std::string path = argv[arg];
        const std::string input = read_file(path);
        for (const auto level : {minify::OptimizationLevel::Conservative,
                                 minify::OptimizationLevel::Structured,
                                 minify::OptimizationLevel::Aggressive}) {
            const char* mode = level == minify::OptimizationLevel::Conservative ? "default" :
                               level == minify::OptimizationLevel::Structured ? "structured" : "aggressive";
            if (!selected_mode.empty() && selected_mode != mode) continue;
            minify::Options options;
            options.optimization = level;
            // One unreported warmup prevents first-allocation noise from dominating tiny inputs.
            std::string output, error;
            if (!minify::javascript(input, output, error, options)) {
                std::cerr << path << " " << mode << ": " << error << "\n";
                return 1;
            }
            for (int run = 0; run < runs; ++run) {
                const auto begin = std::chrono::steady_clock::now();
                if (!minify::javascript(input, output, error, options)) {
                    std::cerr << path << " " << mode << ": " << error << "\n";
                    return 1;
                }
                const auto end = std::chrono::steady_clock::now();
                const double ms = std::chrono::duration<double, std::milli>(end - begin).count();
                std::cout << '"' << path << "\"," << input.size() << ',' << mode << ',' << run + 1
                          << ',' << std::fixed << std::setprecision(3) << ms << ',' << output.size() << '\n';
            }
        }
    }
}
