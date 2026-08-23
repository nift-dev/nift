// Conformance corpus runner: renders tracked pages from a Nift project via the
// public project-aware Engine API and writes each page's output, sorted
// dependencies and sorted requirements to <outdir>/<page>.out/.deps/.reqs.
//
// Used by tests/conformance/run_conformance.py to compare the C++ project-aware
// Engine byte-for-byte against the Nift CLI for the same fixture projects. Page
// name -> filename mapping mirrors the driver: "/" -> ROOT, "/" -> "_".
#include "nift/nift.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: cpp_runner <project_root> <outdir> <page>...\n";
        return 2;
    }
    nift::Engine engine(argv[1]);
    const fs::path outdir(argv[2]);
    std::error_code error;
    fs::create_directories(outdir, error);

    for (int i = 3; i < argc; ++i) {
        const std::string page = argv[i];
        std::string base = (page == "/") ? "ROOT" : page;
        for (char& c : base)
            if (c == '/') c = '_';

        const nift::RenderResult result = engine.render(page);

        std::ofstream out(outdir / (base + ".out"));
        if (result.ok()) out << result.output();
        else out << "ERROR: " << result.error().message << "\n";

        std::vector<std::string> deps = result.dependencies();
        std::sort(deps.begin(), deps.end());
        std::ofstream deps_file(outdir / (base + ".deps"));
        for (const auto& d : deps) deps_file << d << "\n";

        std::vector<std::string> reqs = result.requirements();
        std::sort(reqs.begin(), reqs.end());
        std::ofstream reqs_file(outdir / (base + ".reqs"));
        for (const auto& r : reqs) reqs_file << r << "\n";
    }
    return 0;
}
