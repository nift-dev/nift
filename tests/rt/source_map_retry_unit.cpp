// CP13B R1-2: a failed SourceMap initialization must not publish partial tables.
#include "rt/source_map.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
static int countdown = -1;
void* operator new(std::size_t n) {
    if (countdown == 0) { countdown = -1; throw std::bad_alloc(); }
    if (countdown > 0) --countdown;
    if (void* p = std::malloc(n)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
#pragma GCC diagnostic pop

int main() {
    using namespace nift::rt;
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* what) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "CHECK failed: %s\n", what); }
    };
    int failed_initializations = 0;
    for (int fail = 0; fail < 12; ++fail) {
        LinkedUnit link;
        for (int m = 0; m < 2; ++m) {
            auto module = std::make_unique<Module>();
            module->path = m ? "module.f" : "entry.f";
            module->source = m ? "a\nb\nc\n" : "xxxxxxxxxxxx";
            link.graph.modules.push_back(std::move(module));
        }
        link.segments.push_back(LinkSegment{0, 12, 0, 0});
        link.segments.push_back(LinkSegment{12, 18, 1, 0});
        SourceMap map = SourceMap::linked(link, "entry.f", "xxxxxxxxxxxx");
        countdown = fail;
        bool threw = false;
        try { (void)map.at(16); } catch (const std::bad_alloc&) { threw = true; }
        countdown = -1;
        if (threw) ++failed_initializations;
        const SourceOrigin entry = map.at(0);
        const SourceOrigin module = map.at(16);
        check(entry.path == "entry.f" && entry.line == 1 && entry.column == 1,
              "entry source position after retry");
        check(module.path == "module.f" && module.line == 3 && module.column == 1,
              "linked module position after retry (no duplicate tables)");
        std::atomic<int> wrong{0};
        std::vector<std::thread> readers;
        for (int t = 0; t < 4; ++t) {
            readers.emplace_back([&] {
                for (int i = 0; i < 200; ++i) {
                    const SourceOrigin a = map.at(0);
                    const SourceOrigin b = map.at(16);
                    if (!(a == entry) || !(b == module)) ++wrong;
                }
            });
        }
        for (auto& reader : readers) reader.join();
        check(wrong.load() == 0, "concurrent readers after retry");
    }
    check(failed_initializations > 0, "at least one initialization failed");
    std::printf("source_map_retry_unit: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
