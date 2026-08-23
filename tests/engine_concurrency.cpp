// CP7a: concurrency contract stress test. One Engine, configured once, is
// served concurrently from many threads: engine defaults are read in parallel,
// per-render Contexts are independent, and the mutex-protected source cache is
// exercised through a memory loader. Run normally and under ThreadSanitizer
// (test-engine-concurrency / test-engine-concurrency-tsan).
#include "nift/nift.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

int main() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "nift-engine-concurrency";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    nift::Engine engine;
    engine.set_root(root);
    engine.set("title", std::string("Concurrent"));
    engine.set("count", 100);
    engine.set_loader([](std::string_view path) -> std::optional<std::string> {
        const std::string key(path);
        const std::size_t slash = key.find_last_of("/\\");
        const std::string base = slash == std::string::npos ? key : key.substr(slash + 1);
        if (base == "nav.html") return std::string("<nav>C</nav>");
        return std::nullopt;
    });

    constexpr int kThreads = 8;
    constexpr int kPerThread = 200;
    std::atomic<int> failures{0};
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&engine, &failures, t] {
            for (int i = 0; i < kPerThread; ++i) {
                nift::Context context;
                context.set("request_id", i);
                auto r = engine.render(
                    nift::Source::text("<h1>hi</h1>"),
                    nift::Source::text(
                        "<body>@input(\"nav.html\")$[title]/$[count]/$[request_id]@content</body>"),
                    context);
                const std::string expected =
                    "<body><nav>C</nav>Concurrent/100/" + std::to_string(i) + "<h1>hi</h1></body>";
                if (!r.ok() || r.output() != expected) ++failures;
            }
        });
    }
    for (auto& worker : workers) worker.join();

    std::filesystem::remove_all(root);
    if (failures.load() != 0) {
        std::fprintf(stderr, "engine concurrency FAIL: %d bad render(s)\n", failures.load());
        return 1;
    }
    std::printf("engine concurrency test passed (%d threads x %d renders)\n", kThreads, kPerThread);
    return 0;
}
