// CP10: direct C++ Engine::render vs C ABI render overhead benchmark.
//
// Same engine state and identical repeated renders through the direct C++ API
// and the public C ABI. Prints median ns/render for each plus the ratio. This
// is an interoperability-overhead measurement, not the final 10k build
// campaign.
#include "nift/c_abi.h"
#include "nift/context.h"
#include "nift/engine.h"
#include "nift/source.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
constexpr int kSamples = 7;
constexpr int kIterations = 20000;
constexpr int kWarmup = 1000;

long long median(std::vector<long long>& values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}
}  // namespace

int main() {
    // Representative repeated render: page + template composition with a
    // binding and a small loop.
    const std::string page_text =
        "site=$[site]@for(x : items){<$[x.x]>}";
    const std::string tpl_text = "<main>@content</main>";
    const std::string site = "hello";
    const std::string items = "[{\"x\":\"a\"},{\"x\":\"b\"},{\"x\":\"c\"}]";

    // Direct C++.
    nift::Engine engine;
    engine.set("site", site);
    engine.set_json("items", items);
    nift::Source page(nift::Source::text(page_text));
    nift::Source tpl(nift::Source::text(tpl_text));
    nift::Context context;
    std::vector<long long> cpp_times;
    for (int s = 0; s < kSamples; ++s) {
        const int count = s == 0 ? kWarmup : kIterations;
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < count; ++i) {
            nift::RenderResult result = engine.render(page, tpl, context);
            if (!result.ok()) return 1;
        }
        auto end = std::chrono::steady_clock::now();
        if (s != 0) cpp_times.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / count);
    }

    // C ABI.
    nift_engine* abi_engine = nift_engine_new();
    nift_engine_set_string(abi_engine, "site", 4, site.data(), site.size());
    nift_engine_set_json(abi_engine, "items", 5, items.data(), items.size());
    nift_source page_src{};
    page_src.kind = NIFT_SOURCE_TEXT;
    page_src.data = page_text.data();
    page_src.length = page_text.size();
    nift_source tpl_src{};
    tpl_src.kind = NIFT_SOURCE_TEXT;
    tpl_src.data = tpl_text.data();
    tpl_src.length = tpl_text.size();
    nift_context* abi_context = nift_context_new();
    std::vector<long long> abi_times;
    for (int s = 0; s < kSamples; ++s) {
        const int count = s == 0 ? kWarmup : kIterations;
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < count; ++i) {
            nift_render_result* result = nullptr;
            if (nift_engine_render(abi_engine, &page_src, &tpl_src, abi_context, &result) != NIFT_OK) return 1;
            if (nift_render_result_ok(result) != 1) return 1;
            nift_render_result_free(result);
        }
        auto end = std::chrono::steady_clock::now();
        if (s != 0) abi_times.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / count);
    }
    nift_context_free(abi_context);
    nift_engine_free(abi_engine);

    const long long cpp = median(cpp_times);
    const long long abi = median(abi_times);
    std::printf("direct C++: %lld ns/render\n", cpp);
    std::printf("C ABI     : %lld ns/render\n", abi);
    std::printf("delta     : %lld ns (+%.1f%%)\n", abi - cpp,
                100.0 * static_cast<double>(abi - cpp) / static_cast<double>(cpp));
    return 0;
}
