#include <minify/Minify.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
struct Workload {
    const char* name;
    minify::Format format;
    std::string unit;
};
}

int main(int argc, char** argv) {
    std::size_t repetitions = 10000;
    std::size_t iterations = 20;
    if (argc >= 2) repetitions = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
    if (argc >= 3) iterations = static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10));
    if (repetitions == 0 || iterations == 0) return 2;

    std::vector<Workload> workloads = {
        {"html", minify::Format::Html, "<section class=\"card\"> <h2> Title </h2> <p>alpha <b>beta</b></p> </section>\n"},
        {"css", minify::Format::Css, "@container card (width > 20rem) { .item { width: calc(100% - 2rem); color: rgb(10 20 30 / 80%); } }\n"},
        {"javascript", minify::Format::JavaScript, "const value = /[<>]/.test(text) ? object?.item ?? 0 : total / 2; console.log(value);\n"},
        {"jsx", minify::Format::Jsx, "const view = <Card<Map<string,number>> value={data?.item ?? 0}><span>{/[<>]/.test(text) ? 'a' : 'b'}</span></Card>;\n"},
        {"json", minify::Format::Json, "{\"name\":\"entry\",\"enabled\":true,\"items\":[1,2,3],\"meta\":{\"value\":null}}\n"},
        {"xml", minify::Format::Xml, "<entry xmlns:x=\"urn:x\"><x:name> alpha  beta </x:name><![CDATA[a < b]]></entry>\n"},
        {"svg", minify::Format::Svg, "<svg xmlns=\"http://www.w3.org/2000/svg\"><path d=\"M 0 0 L 10 10\"/><text>alpha  beta</text></svg>\n"}
    };

    std::cout << "format,input_bytes,output_bytes,iterations,median_ms,MiB_per_second\n";
    for (const auto& workload : workloads) {
        std::string input;
        input.reserve(workload.unit.size() * repetitions);
        if (workload.format == minify::Format::Json) {
            input = "[";
            for (std::size_t index = 0; index < repetitions; ++index) {
                if (index) input += ',';
                input += workload.unit;
            }
            input += "]";
        } else {
            for (std::size_t index = 0; index < repetitions; ++index) input += workload.unit;
        }

        std::vector<double> samples;
        std::size_t output_size = 0;
        for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
            std::string output, error;
            const auto start = std::chrono::steady_clock::now();
            if (!minify::run(workload.format, input, output, error)) {
                std::cerr << workload.name << " benchmark failed: " << error << '\n';
                return 1;
            }
            const auto finish = std::chrono::steady_clock::now();
            samples.push_back(std::chrono::duration<double, std::milli>(finish - start).count());
            output_size = output.size();
        }
        std::sort(samples.begin(), samples.end());
        const double median_ms = samples[samples.size() / 2];
        const double mib_per_second = (static_cast<double>(input.size()) / (1024.0 * 1024.0)) /
                                      (median_ms / 1000.0);
        std::cout << workload.name << ',' << input.size() << ',' << output_size << ','
                  << iterations << ',' << std::fixed << std::setprecision(3) << median_ms << ','
                  << std::setprecision(1) << mib_per_second << '\n';
    }
}
