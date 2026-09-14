#include <minify/Minify.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
std::string read_file(const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error(std::string("cannot open ") + path);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: minifypp-css-fixture-benchmark <css-file>...\n";
        return 2;
    }
    std::size_t warmups = 5;
    std::size_t iterations = 45;
    if (const char* value = std::getenv("BENCH_WARMUPS")) warmups = std::strtoull(value, nullptr, 10);
    if (const char* value = std::getenv("BENCH_ITERATIONS")) iterations = std::strtoull(value, nullptr, 10);
    if (iterations == 0) return 2;

    std::cout << "fixture,input_bytes,output_bytes,median_ms,MiB_per_second,iterations\n";
    for (int index = 1; index < argc; ++index) {
        std::string input;
        try {
            input = read_file(argv[index]);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }

        std::vector<double> samples;
        std::size_t output_size = 0;
        for (std::size_t iteration = 0; iteration < warmups + iterations; ++iteration) {
            std::string output, error;
            const auto start = std::chrono::steady_clock::now();
            if (!minify::run(minify::Format::Css, input, output, error)) {
                std::cerr << argv[index] << ": " << error << '\n';
                return 1;
            }
            const auto finish = std::chrono::steady_clock::now();
            if (iteration >= warmups) {
                samples.push_back(std::chrono::duration<double, std::milli>(finish - start).count());
            }
            output_size = output.size();
        }
        std::sort(samples.begin(), samples.end());
        const double median_ms = samples[samples.size() / 2];
        const double mib_per_second = (static_cast<double>(input.size()) / (1024.0 * 1024.0)) /
                                      (median_ms / 1000.0);
        const std::string path = argv[index];
        const auto slash = path.find_last_of("/\\");
        const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        std::cout << name << ',' << input.size() << ',' << output_size << ',' << std::fixed
                  << std::setprecision(3) << median_ms << ',' << std::setprecision(1)
                  << mib_per_second << ',' << iterations << '\n';
    }
}
