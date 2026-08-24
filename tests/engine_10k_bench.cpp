// NR12 follow-up: C++ Embedded Engine 10k-page render characterization.
// Opens the 10k-page project once, then renders every page by tracked name.
// Reports open/init cost, the 10k render batch, and the total.
#include "nift/nift.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const int pages = argc > 2 ? std::atoi(argv[2]) : 10000;
    const char* root = argc > 1 ? argv[1] : "/tmp/nift-10k-prof";

    auto open_start = std::chrono::steady_clock::now();
    nift::Engine engine(root);
    auto open_end = std::chrono::steady_clock::now();
    const double open_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(open_end - open_start).count();
    if (!engine.is_open()) {
        std::cerr << "engine open failed: " << engine.open_error() << "\n";
        return 2;
    }

    nift::Context context;
    auto render_start = std::chrono::steady_clock::now();
    std::size_t failures = 0;
    for (int i = 0; i < pages; ++i) {
        const std::string page = "p" + std::to_string(i);
        nift::RenderResult result = engine.render(page, context);
        if (!result.ok()) ++failures;
    }
    auto render_end = std::chrono::steady_clock::now();
    const double render_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(render_end - render_start).count();

    std::cout << "embed open:  " << open_ns / 1e6 << " ms\n";
    std::cout << "embed 10k render: " << render_ns / 1e6 << " ms ("
              << render_ns / pages << " ns/render)\n";
    std::cout << "embed total: " << (open_ns + render_ns) / 1e6 << " ms\n";
    std::cout << "failures: " << failures << "\n";
    return 0;
}
