// CP13B A1: SourceMap ownership, lazy positions, concurrency and no-success-lookup.
#include "rt/source_map.h"
#include "rt_test.h"
#include "rt_vm_tool.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace nift::rt;

namespace {

SourceOrigin origin_of(std::string_view source, std::uint32_t offset, const std::string& path) {
    const Location where = locate(source, offset);
    return SourceOrigin{path, static_cast<std::uint32_t>(where.line), static_cast<std::uint32_t>(where.column)};
}

void check_single_positions() {
    const std::string text = "a\nbc\n\nd\nef\n";
    const SourceMap map = SourceMap::single("entry.f", text);
    for (std::uint32_t offset = 0; offset <= text.size(); ++offset)
        CHECK(map.at(offset) == origin_of(text, offset, "entry.f"));
}

std::unique_ptr<Module> module(std::string path, std::string source) {
    auto m = std::make_unique<Module>();
    m->path = std::move(path);
    m->source = std::move(source);
    return m;
}

void check_linked_positions_and_lifetime() {
    const std::string entry = "print(1)\n";
    const std::string mod = "one\ntwo\nthree\n";
    LinkedUnit link;
    link.linked = true;
    link.graph.modules.push_back(module("entry.f", entry));
    link.graph.modules.push_back(module("lib.nft", mod));
    const std::size_t begin_mod = entry.size();
    link.segments.push_back(LinkSegment{0, entry.size(), 0, 0});
    link.segments.push_back(LinkSegment{begin_mod, begin_mod + mod.size(), 1, 0});
    const std::string linked_text = entry + mod;
    SourceMap map = SourceMap::linked(link, "entry.f", entry);
    // The map owns the module source: release the caller's buffer.
    link.graph.modules[1]->source.assign(64, 'x');
    for (std::uint32_t offset = 0; offset <= linked_text.size(); ++offset) {
        const std::string_view expected_source = offset < begin_mod ? std::string_view(entry) : std::string_view(mod);
        const std::uint32_t expected_offset = offset < begin_mod ? offset : offset - static_cast<std::uint32_t>(begin_mod);
        const std::string path = offset < begin_mod ? "entry.f" : "lib.nft";
        CHECK(map.at(offset) == origin_of(expected_source, expected_offset, path));
    }
    CHECK(map.at(static_cast<std::uint32_t>(begin_mod + 4)) == (SourceOrigin{"lib.nft", 2, 1}));
}

void check_lifetime_under_asan() {
    auto* buffer = new std::string(1024, 'q');
    (*buffer)[100] = '\n';
    SourceMap map = SourceMap::single("heap.f", *buffer);
    delete buffer;
    const SourceOrigin at = map.at(101);  // the old buffer must not be read
    CHECK(at.line == 2);
    CHECK(at.column == 1);
    CHECK(at.path == "heap.f");
}

void check_concurrency() {
    const std::string text = "l0\nl1\nl2\nl3\nl4\nl5\nl6\nl7\n";
    const SourceMap map = SourceMap::single("threads.f", text);
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&, t] {
            for (std::uint32_t i = 0; i < 2000; ++i) {
                const std::uint32_t offset = (i * 7 + static_cast<std::uint32_t>(t)) % (text.size() + 1);
                if (!(map.at(offset) == origin_of(text, offset, "threads.f"))) ++failures;
            }
        });
    }
    // A concurrent lookup after the first (first-use) initialization.
    const SourceOrigin first = map.at(0);
    CHECK(first.line == 1 && first.column == 1);
    for (auto& thread : threads) thread.join();
    CHECK(failures.load() == 0);
}

void check_no_lookup_on_success() {
    const std::string source = "x := 1\nprint(x)\n";
    const SourceMap map = SourceMap::single("success.f", source);
    map.reset_lookup_count();
    VmOptions options;
    options.source_map = &map;
    const rt_vm_tool::Run run = rt_vm_tool::run(source, options);
    CHECK(run.reason.empty());
    CHECK(run.result.reason == StopReason::None);
    CHECK(run.output == "1\n");
    CHECK(map.lookup_count() == 0);
}

} // namespace

int main() {
    check_single_positions();
    check_linked_positions_and_lifetime();
    check_lifetime_under_asan();
    check_concurrency();
    check_no_lookup_on_success();
    return rt_test::finish("source_map_unit");
}
