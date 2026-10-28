// CP17 N2 R1-10: observations feed the production incremental decision.
// A real PageObserver captures the VM native's cp/mv observations; the test
// writes page metadata and calls ProjectInfo::build_reasons (the production
// decision), then edits/recreates the source. --omit=cp|mv models the M15/M16
// missing-observation witness for the mutant harness.
#include "PageObserver.h"
#include "ProjectInfo.h"
#include "RenderHost.h"
#include "Types.h"
#include "ReadObservation.h"
#include "rt/natives/host_ops.h"
#include "rt/string.h"
#include "rt_test.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nift::rt::Value;
using nift::rt::make_string;

namespace {

struct TestHost final : RenderHost {
    fs::path root_path;
    const fs::path& root() const override { return root_path; }
    std::string relative(const fs::path& path) const override {
        std::error_code ec;
        const fs::path rel = fs::relative(path, root_path, ec);
        return (ec ? path : rel).generic_string();
    }
    const std::string& output_dir() const override { static const std::string d = "public"; return d; }
    int build_threads() const override { return 1; }
    fs::path content_path(const TrackedInfo&) const override { return {}; }
    fs::path output_path(const TrackedInfo&) const override { return {}; }
    fs::path pagination_output_path(const TrackedInfo&, std::size_t) const override { return {}; }
    bool has_output_context() const override { return true; }
    std::optional<TrackedOutput> tracked_output_path(const std::string&) const override { return std::nullopt; }
    const std::shared_ptr<const nift::RuntimeValue>* binding(const std::string&) const override { return nullptr; }
    bool is_contract_name(const std::string&) const override { return false; }
    const std::string* contract_source(const std::string&) const override { return nullptr; }
    HostSource read_shared_source(const fs::path&) const override { return {}; }
    std::shared_ptr<const json::Document> read_shared_json(const fs::path&, std::string&) const override {
        return nullptr;
    }
    std::shared_ptr<const nift::RuntimeValue> read_shared_runtime_json(const fs::path&, std::string&) const override {
        return nullptr;
    }
    bool source_exists(const fs::path&) const override { return false; }
    bool source_readable(const fs::path&) const override { return false; }
    nift::HostResult environment(const std::string&) const override { return {}; }
};

bool run_native(const fs::path& root, nift::rt::natives::NativeFn fn, const std::vector<std::string>& paths,
                RenderResult& result) {
    TestHost host;
    host.root_path = root;
    PageObserver observer(host, result, PageObserver::Mode{true, false});
    nift::rt::natives::Context context;
    context.host.observer = &observer;
    std::vector<Value> args;
    for (const auto& path : paths) args.push_back(make_string(path));
    Value out;
    std::string error;
    return fn(context, args.data(), static_cast<std::uint32_t>(args.size()), out, error) ==
           nift::rt::natives::Status::Ok;
}

std::string escaped(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

std::string list(const std::set<std::string>& values, bool absent) {
    if (!absent) return "[]";
    std::string out = "[";
    bool first = true;
    for (const auto& value : values) {
        if (!first) out += ",";
        out += "\"" + escaped(value) + "\"";
        first = false;
    }
    return out + "]";
}

fs::path info_path(const fs::path& root, const TrackedInfo& info) {
    return root / ".nift" / "public" / (info.name + ".info.json");
}

void write_metadata(const fs::path& root, const TrackedInfo& info, const std::set<std::string>& dependencies,
                    const std::set<std::string>& absent) {
    std::error_code ec;
    fs::create_directories(info_path(root, info).parent_path(), ec);
    std::ofstream out(info_path(root, info));
    out << "{\n"
        << "\"name\":\"" << escaped(info.name) << "\",\n"
        << "\"title\":\"" << escaped(info.title) << "\",\n"
        << "\"template\":\"\",\n"
        << "\"content\":\"content/index.html\",\n"
        << "\"output\":\"public/index.html\",\n"
        << "\"minify\":false,\n\"minify-version\":0,\n"
        << "\"pagination\":false,\n\"pagination-items-per-page\":0,\n"
        << "\"pagination-template\":\"\",\n\"pagination-separator\":\"\",\n\"pagination-pages\":0,\n"
        << "\"project-fingerprint\":\"\",\n\"hierarchy-fingerprint\":\"\",\n"
        << "\"build-hooks\":\"\",\n"
        << "\"dependencies\":" << list(dependencies, true) << ",\n"
        << "\"reqs\":[],\n\"dependency-hashes\":{},\n"
        << "\"site-unknown-values\":0,\n"
        << "\"absent\":" << list(absent, true) << "\n}\n";
}


bool setup(const fs::path& root, const std::string& name, const fs::path& info_output) {
    std::error_code ec;
    fs::create_directories(root / "content", ec);
    fs::create_directories(root / "public", ec);
    std::ofstream(root / "content/index.html") << "page\n";
    std::ofstream(root / info_output) << "page\n";
    (void)name;
    return true;
}

bool rebuild_for(const std::string& kind, bool omit_cp, bool omit_mv) {
    const fs::path root = fs::temp_directory_path() / ("n2-rebuild-" + kind);
    std::error_code ec;
    fs::remove_all(root, ec);
    setup(root, "index", "public/index.html");
    const fs::path source = root / (kind == "cp" ? "cp-src.txt" : "mv-src.txt");
    std::ofstream(source) << "one\n";

    RenderResult observed;
    const bool ok = kind == "cp"
        ? run_native(root, nift::rt::natives::host_cp,
                     {(root / "cp-src.txt").generic_string(), (root / "dest.txt").generic_string()}, observed)
        : run_native(root, nift::rt::natives::host_mv,
                     {(root / "mv-src.txt").generic_string(), (root / "dest.txt").generic_string()}, observed);
    CHECK(ok);

    std::set<std::string> dependencies = observed.dependencies;
    std::set<std::string> absent = observed.observed.absent;
    const char* mutant_env = std::getenv("NIFT_N2_MUTANT");
    const bool mutant_cp = mutant_env && std::string(mutant_env) == "15";
    const bool mutant_mv = mutant_env && std::string(mutant_env) == "16";
    if (kind == "cp" && mutant_cp) CHECK(dependencies.count("cp-src.txt") == 0);
    if (kind == "mv" && mutant_mv) CHECK(absent.count("mv-src.txt") == 0);
    if (kind == "cp" && omit_cp) dependencies.erase("cp-src.txt");
    if (kind == "mv" && omit_mv) absent.erase("mv-src.txt");

    TrackedInfo info;
    info.name = "index";
    info.title = "Index";
    write_metadata(root, info, dependencies, absent);

    ProjectInfo project;
    project.root = root;
    project.config.incremental_mode = "modified";
    const fs::path metadata = info_path(root, info);
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(source, now - std::chrono::seconds(100), ec);
    fs::last_write_time(metadata, now - std::chrono::seconds(50), ec);

    // Unchanged state: no rebuild.
    CHECK(project.build_reasons(info).empty());

    if (kind == "cp") {
        std::ofstream(source, std::ios::trunc) << "two\n";
        fs::last_write_time(source, now, ec);
        const auto reasons = project.build_reasons(info);
        if (omit_cp || mutant_cp) CHECK(reasons.empty());
        else CHECK(!reasons.empty());
    } else {
        std::ofstream(source, std::ios::trunc) << "back\n";
        fs::last_write_time(source, now, ec);
        const auto reasons = project.build_reasons(info);
        if (omit_mv || mutant_mv) CHECK(reasons.empty());
        else CHECK(!reasons.empty());
    }
    fs::remove_all(root, ec);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::string omit;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--omit") omit = argv[i + 1];
    CHECK(rebuild_for("cp", omit == "cp", false));
    CHECK(rebuild_for("mv", false, omit == "mv"));
    return rt_test::finish("rt N2 rebuild-decision");
}
