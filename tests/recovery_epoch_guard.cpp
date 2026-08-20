#include "FileSystem.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static bool require(bool condition, const std::string& message) {
    if (condition) return true;
    std::cerr << "FAIL: " << message << '\n';
    return false;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: recovery_epoch_guard TMPDIR\n";
        return 2;
    }
    const fs::path root = argv[1];
    std::error_code cleanup_error;
    fs::remove_all(root, cleanup_error);
    const fs::path public_dir = root / "public";
    const fs::path state_dir = root / ".nift/output";
    fs::create_directories(public_dir);
    fs::create_directories(state_dir);

    const fs::path output = public_dir / "index.html";
    const fs::path metadata = state_dir / "index.info.json";
    if (!filesystem::write_readonly_file(output, "same\n") ||
        !filesystem::write_readonly_file(metadata, "{}\n")) return 3;

    filesystem::begin_recovery_epoch();
    filesystem::reset_recovery_scan_count_for_tests();
    for (int i = 0; i < 10000; ++i) {
        if (!filesystem::write_readonly_file(output, "same\n")) return 4;
        if (!filesystem::write_readonly_file(metadata, "{}\n")) return 5;
    }
    if (!require(filesystem::recovery_scan_count_for_tests() == 2,
                 "10k repeated writes across two parents must scan exactly once per parent in one epoch")) return 1;

    filesystem::begin_recovery_epoch();
    if (!filesystem::write_readonly_file(output, "same\n") ||
        !filesystem::write_readonly_file(metadata, "{}\n")) return 6;
    if (!require(filesystem::recovery_scan_count_for_tests() == 4,
                 "a new epoch must make each touched parent eligible for exactly one new scan")) return 1;

    for (int i = 0; i < 1000; ++i)
        if (!filesystem::write_readonly_file(output, "same\n")) return 7;
    if (!require(filesystem::recovery_scan_count_for_tests() == 4,
                 "same-parent writes later in an epoch must not rescan")) return 1;

    std::cout << "PASS: recovery scans are bounded to one per distinct parent per epoch\n";
    fs::remove_all(root, cleanup_error);
    return 0;
}
