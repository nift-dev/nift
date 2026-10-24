// CP17 N2: Host-level observer parity. Legacy's observation sequence and the
// VM native's sequence run through the same real PageObserver over a test
// RenderHost; the resulting RenderResult (dependencies/absent/written/
// unreadable) and the rebuild-relevant facts must be identical.
#include "PageObserver.h"
#include "RenderHost.h"
#include "Types.h"
#include "ReadObservation.h"
#include "rt/host/host_call.h"
#include "rt/natives/host_ops.h"
#include "rt/string.h"
#include "rt_test.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nift::rt::Value;
using nift::rt::make_string;

namespace nift::observe {
// The test supplies the only observation-spelling rule it needs; the real
// implementation lives in ParserReadObservation.cpp (not linked here).
std::string spell(const std::filesystem::path& root, const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path rel = std::filesystem::relative(path, root, ec);
    return (ec ? path : rel).generic_string();
}
} // namespace nift::observe

// Key function of RenderHost (normally in ParserObservation.cpp): defining it
// here keeps the test independent of the legacy Parser object file.
std::string RenderHost::listing_fingerprint(const std::string& key) const { return key; }

namespace {

struct TestHost final : RenderHost {
    fs::path root_path;
    mutable std::map<std::string, std::string> observations;
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

struct Pair {
    std::set<std::string> dependencies, absent, reqs;
    std::set<std::filesystem::path> written;
    std::map<std::string, std::string> unreadable;
    bool operator==(const Pair& o) const {
        return dependencies == o.dependencies && absent == o.absent && written == o.written &&
               unreadable == o.unreadable && reqs == o.reqs;
    }
};

Pair facts(const RenderResult& r) {
    Pair p;
    p.dependencies = r.dependencies;
    p.absent = r.observed.absent;
    p.written = r.observed.written;
    p.unreadable = r.observed.unreadable;
    p.reqs = r.reqs;
    return p;
}

bool run_native(const fs::path& root, nift::rt::natives::NativeFn fn, const std::vector<std::string>& paths,
                RenderResult& result) {
    TestHost host; host.root_path = root;
    PageObserver observer(host, result, PageObserver::Mode{true, false});
    nift::rt::natives::Context context;
    context.host.observer = &observer;
    std::vector<Value> args;
    for (const auto& p : paths) args.push_back(make_string(p));
    Value out; std::string error;
    const auto status = fn(context, args.data(), static_cast<std::uint32_t>(args.size()), out, error);
    return status == nift::rt::natives::Status::Ok;
}

void observe_legacy_cp(const fs::path& root, RenderResult& result) {
    TestHost host; host.root_path = root;
    PageObserver observer(host, result, PageObserver::Mode{true, false});
    observer.stream_open(root / "cp-src.txt");
    observer.written(root / "dst" / "cp-copy.txt");
}

void observe_legacy_mv(const fs::path& root, RenderResult& result) {
    TestHost host; host.root_path = root;
    PageObserver observer(host, result, PageObserver::Mode{true, false});
    observer.failed_read(root / "mv-src.txt");
    observer.written(root / "dst" / "mv-copy.txt");
}

void observe_legacy_cp_missing(const fs::path& root, RenderResult& result) {
    TestHost host; host.root_path = root;
    PageObserver observer(host, result, PageObserver::Mode{true, false});
    observer.failed_read(root / "missing.txt");
}

void observe_legacy_mv_failed(const fs::path& root, RenderResult& result) {
    TestHost host; host.root_path = root;
    PageObserver observer(host, result, PageObserver::Mode{true, false});
    observer.stream_open(root / "mv-fail-src.txt");
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "n2-parity";
    std::error_code ignored;
    fs::remove_all(root, ignored);
    fs::create_directories(root / "dst");
    { std::ofstream out(root / "cp-src.txt"); out << "cp\n"; }
    { std::ofstream out(root / "mv-src.txt"); out << "mv\n"; }
    { std::ofstream out(root / "mv-fail-src.txt"); out << "fail\n"; }

    RenderResult legacy_cp, vm_cp;
    observe_legacy_cp(root, legacy_cp);
    CHECK(run_native(root, nift::rt::natives::host_cp, {(root / "cp-src.txt").generic_string(),
                                                        (root / "dst" / "cp-copy.txt").generic_string()}, vm_cp));
    CHECK(facts(legacy_cp) == facts(vm_cp));

    RenderResult legacy_mv, vm_mv;
    CHECK(run_native(root, nift::rt::natives::host_mv, {(root / "mv-src.txt").generic_string(),
                                                        (root / "dst" / "mv-copy.txt").generic_string()}, vm_mv));
    observe_legacy_mv(root, legacy_mv);  // legacy's absence observation runs after the move
    CHECK(facts(legacy_mv) == facts(vm_mv));

    RenderResult legacy_missing, vm_missing;
    observe_legacy_cp_missing(root, legacy_missing);
    CHECK(!run_native(root, nift::rt::natives::host_cp, {(root / "missing.txt").generic_string(),
                                                         (root / "dst" / "missing-copy.txt").generic_string()}, vm_missing));
    CHECK(facts(legacy_missing) == facts(vm_missing));

    RenderResult legacy_failed, vm_failed;
    observe_legacy_mv_failed(root, legacy_failed);
    CHECK(!run_native(root, nift::rt::natives::host_mv, {(root / "mv-fail-src.txt").generic_string(),
                                                         (root / "no-dir" / "moved.txt").generic_string()}, vm_failed));
    CHECK(facts(legacy_failed) == facts(vm_failed));

    fs::remove_all(root, ignored);
    return rt_test::finish("rt N2 host-level observer parity");
}
