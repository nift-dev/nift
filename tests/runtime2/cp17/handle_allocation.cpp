// CP17 x CP18: allocation failure in handle-using programs run through the
// CP18 compiled-program API (nift_program_run), which admits CP17 file
// handles. Every allocation of each run is failed in turn (operator new
// countdown, as tests/runtime2/cp18/cow_allocation.cpp). For each injected
// failure the run must RETURN (NIFT_ERROR_INTERNAL, no result), leak neither
// VM heap nor allocations, and leave the program reusable: the next clean
// run equals the baseline exactly (outcome, output, message, code).
//
// A run that does not return within the watchdog (a separate thread; the run
// itself stays on the main thread) is reported as
// "HANG program=<name> site=<n>" and the process exits 3: the defect this
// test exists for is an indefinite wait (a cleanup's in-flight count or
// obligation never released after a bookkeeping allocation failed), so the
// site and program are the witness; the watchdog only bounds the run.
#include "nift/c_abi.h"
#include "rt/heap_account.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <unistd.h>

static long remaining = -1;
static bool failed = false;
static long outstanding_allocations = 0;
void* operator new(std::size_t bytes) {
    if (remaining == 0) { remaining = -1; failed = true; throw std::bad_alloc(); }
    if (remaining > 0) --remaining;
    if (void* p = std::malloc(bytes ? bytes : 1)) { ++outstanding_allocations; return p; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { if (p) --outstanding_allocations; std::free(p); }
void operator delete[](void* p) noexcept { if (p) --outstanding_allocations; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { if (p) --outstanding_allocations; std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { if (p) --outstanding_allocations; std::free(p); }

static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); } } while (0)
static std::size_t live() { return nift::rt::live_heap_bytes() - nift::rt::heap_charged_bytes(); }

namespace {

struct Snapshot {
    int outcome = -1;
    std::string output, message, code;
};

std::string text(nift_string s) { return s.data ? std::string(s.data, s.length) : std::string(); }

Snapshot snapshot(nift_program_result* r) {
    Snapshot s;
    s.outcome = static_cast<int>(nift_program_result_outcome(r));
    nift_string v{};
    if (nift_program_result_output(r, &v) == NIFT_OK) s.output = text(v);
    if (nift_program_result_error_message(r, &v) == NIFT_OK) s.message = text(v);
    if (nift_program_result_error_code(r, &v) == NIFT_OK) s.code = text(v);
    return s;
}

// Runs once on this thread (programs are isolate-bound), failing allocation
// `site` (-1: none), under a watchdog thread that reports a run which does not
// return within 30 s as a hang (and exits 3 with the program and site).
const char* g_name = "";
bool run_bounded(nift_program* program, long site, nift_status& status, nift_program_result*& result) {
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    std::thread watchdog([&] {
        std::unique_lock<std::mutex> lock(mu);
        if (!cv.wait_for(lock, std::chrono::seconds(30), [&] { return done; })) {
            std::fprintf(stderr, "HANG program=%s site=%ld\n", g_name, site);
            std::fflush(stderr);
            _exit(3);
        }
    });
    failed = false;
    remaining = site;
    status = nift_program_run(program, 0, 0, 0, &result);
    remaining = -1;
    {
        std::lock_guard<std::mutex> lock(mu);
        done = true;
    }
    cv.notify_all();
    watchdog.join();
    return true;
}

void campaign(const char* name, const char* text_source, int expected_outcome) {
    // Diagnostics: NIFT_ALLOC_ONLY=<program> limits the campaign to one program.
    if (const char* only = std::getenv("NIFT_ALLOC_ONLY"); only && std::strcmp(only, name) != 0) return;
    g_name = name;
    nift_source source = {NIFT_SOURCE_TEXT, text_source, std::strlen(text_source), "h.f", 3};
    nift_program* program = nullptr;
    nift_program_result* result = nullptr;
    CHECK(nift_program_compile(&source, &program, &result) == NIFT_OK && program);
    nift_program_result_free(result);
    result = nullptr;
    nift_status status = NIFT_OK;
    // Warm-up run (interned names, thread-local storage), then the baseline.
    CHECK(run_bounded(program, -1, status, result) && status == NIFT_OK);
    nift_program_result_free(result);
    result = nullptr;
    CHECK(run_bounded(program, -1, status, result) && status == NIFT_OK);
    const Snapshot baseline = snapshot(result);
    nift_program_result_free(result);
    result = nullptr;
    if (baseline.outcome != expected_outcome)
        std::fprintf(stderr, "%s: baseline outcome %d message '%s' code '%s'\n", name, baseline.outcome,
                     baseline.message.c_str(), baseline.code.c_str());
    CHECK(baseline.outcome == expected_outcome);
    const auto base_live = live();
    const auto base_allocations = outstanding_allocations;
    int sites = 0;
    bool completed = false;
    for (long i = 0; i < 20000 && !completed; ++i) {
        result = nullptr;
        if (!run_bounded(program, i, status, result)) {
            std::fprintf(stderr, "HANG program=%s site=%ld\n", name, i);
            std::fflush(stderr);
            _exit(3);
        }
        if (!failed) {
            CHECK(status == NIFT_OK && result);
            if (result) {
                const Snapshot s = snapshot(result);
                CHECK(s.outcome == baseline.outcome && s.output == baseline.output && s.message == baseline.message &&
                      s.code == baseline.code);
            }
            nift_program_result_free(result);
            completed = true;
            break;
        }
        ++sites;
        if (status != NIFT_ERROR_INTERNAL || result) {
            const Snapshot s = result ? snapshot(result) : Snapshot{};
            std::fprintf(stderr, "%s site %ld: status %d, outcome %d, message '%s'\n", name, i, int(status), s.outcome,
                         s.message.c_str());
        }
        const bool contained = status == NIFT_ERROR_INTERNAL && !result;
        const int injected_status = int(status);
        const bool injected_result = result != nullptr;
        CHECK(contained);
        nift_program_result_free(result);
        const long live_delta = static_cast<long>(live()) - static_cast<long>(base_live);
        const long alloc_delta = outstanding_allocations - base_allocations;
        CHECK(live_delta == 0);
        CHECK(alloc_delta == 0);
        // Reusable after the failure, with the exact baseline result.
        result = nullptr;
        if (!run_bounded(program, -1, status, result)) {
            std::fprintf(stderr, "HANG program=%s rerun-after-site=%ld\n", name, i);
            std::fflush(stderr);
            _exit(3);
        }
        CHECK(status == NIFT_OK && result);
        bool rerun_matches = false;
        if (result) {
            const Snapshot s = snapshot(result);
            rerun_matches = s.outcome == baseline.outcome && s.output == baseline.output &&
                            s.message == baseline.message && s.code == baseline.code;
            CHECK(rerun_matches);
        }
        nift_program_result_free(result);
        // One record per injected site (the campaign's evidence).
        std::printf("site program=%s index=%ld status=%d result=%s heap_delta=%ld alloc_delta=%ld rerun=%s\n", name, i,
                    injected_status, injected_result ? "present" : "none", live_delta,
                    alloc_delta, rerun_matches ? "baseline" : "MISMATCH");
    }
    CHECK(completed && sites > 0);
    nift_program_free(program);
    std::printf("%s: %d run sites, baseline outcome %d\n", name, sites, baseline.outcome);
}

} // namespace

int main() {
    const auto dir = std::filesystem::temp_directory_path() / ("cp17-handle-allocation-" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    std::filesystem::current_path(dir);
    std::ofstream("p") << "one\ntwo\n";
    std::ofstream("q") << "q\n";
    // Closed normally: cleanup completes.
    campaign("closed", "f := file(\"p\")\nf.open(\"r\")\nprint(f.read_line())\nf.close()\n", NIFT_PROGRAM_COMPLETE);
    // Left open: the context close runs the file hook, which reports a failure
    // (left open) through the cleanup bookkeeping.
    campaign("left-open", "f := file(\"p\")\nf.open(\"r\")\nprint(f.read_line())\n", NIFT_PROGRAM_ERROR);
    // Decision B: an unwound frame's file is discarded silently.
    campaign("unwound", "fn(w()) { f := file(\"p\"); f.open(\"r\"); throw error(\"s\") }\n"
                        "try { w() } catch(e) { print(e.message) }\n", NIFT_PROGRAM_COMPLETE);
    // Statement rollback: a leaked callee file's report is withdrawn.
    campaign("rollback", "fn(leak()) { g := file(\"q\"); g.open(\"r\"); return 1 }\n"
                         "fn(work()) { throw error(\"s\") }\n"
                         "try { x := leak() + work() } catch(e) { print(e.message) }\n", NIFT_PROGRAM_COMPLETE);
    // Normal return leaving a file open: dropped and reported at the return.
    campaign("dropped-open", "fn(leak()) { g := file(\"q\"); g.open(\"r\"); return 1 }\nprint(leak())\n",
             NIFT_PROGRAM_ERROR);
    std::filesystem::current_path(std::filesystem::temp_directory_path());
    std::filesystem::remove_all(dir);
    std::printf("CP17 handle allocation containment: %d failures\n", failures);
    return failures ? 1 : 0;
}
