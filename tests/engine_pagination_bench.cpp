// CP8 pagination benchmark: standalone C++ Engine ns/render for a tracked
// paginated page (3 pages, 3 items). Renders the primary page via the
// project-aware render -- the render that also assembles the complete
// pagination set. Prints the median "<median> ns/render\n" on stdout,
// mirroring engine_bench.cpp.
#include "nift/nift.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
void write_file(const fs::path& path, const std::string& contents) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary);
    out << contents;
}
}  // namespace

int main(int argc, char** argv) {
    const int samples = argc > 1 ? std::atoi(argv[1]) : 7;
    const int iterations = 5000;
    const int warmup = 200;

    fs::path root = fs::temp_directory_path() / "nift-cpp-pg-bench";
    fs::remove_all(root);
    write_file(root / ".nift/config.json",
               R"({"config":{"content-dir":"content/","output-dir":"public/","default-template":"templates/template.html","incremental-mode":"modified"}})");
    write_file(root / ".nift/tracked.json",
               R"({"tracked":[{"name":"blog","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":1}}]})");
    write_file(root / "templates/template.html", "<main>$[title]</main>\n@content");
    write_file(root / "content/blog.html",
               "@item{one}@item{two}@item{three}@paginate");
    write_file(root / "content/blog.paginate.html",
               "<section>page $[paginate.current]/$[paginate.total]:[$[paginate.items]]</section>");

    nift::Engine engine(root);
    std::vector<long long> times;
    times.reserve(samples);
    for (int s = 0; s < samples; ++s) {
        const int count = s == 0 ? warmup : iterations;
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < count; ++i) {
            nift::RenderResult result = engine.render("blog");
            if (!result.ok() || result.pagination().size() != 2) {
                std::fprintf(stderr, "unexpected render\n");
                return 1;
            }
        }
        auto end = std::chrono::steady_clock::now();
        if (s != 0) {
            times.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / count);
        }
    }
    std::sort(times.begin(), times.end());
    std::printf("%lld ns/render\n", times[times.size() / 2]);
    return 0;
}
