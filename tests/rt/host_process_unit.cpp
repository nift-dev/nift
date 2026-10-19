// CP16 process model tests (design §11.12, the cases that apply before the VM
// wiring): launch vs exit, reaping, deadlock-free draining, deadlines while
// blocked, SIGTERM-ignoring children, pipeline/child cancellation, abandoned
// handles and context ends (through the TEMPORARY handle adapter), C1
// leftovers, escapees and the Linux subreaper, detached processes, exit/cancel
// races, capture limits, low descriptor limits, trace v2 spawn lines, C2
// (SIGTERM to Nift) and PDEATHSIG.
//
// Every test process carries NIFT_TEST_PROC_TAG=<tag> so survivors are found
// by scanning /proc/*/environ (orphan detection, §11.12). Run under ulimit -f,
// timeout and child cleanup (D-031); see the Makefile target.
#include "rt/host/handle_adapter_tmp.h"
#include "rt/host/process.h"
#include "rt/host/process_call.h"
#include "rt/host/process_cleanup.h"
#include "rt/host/process_runtime.h"
#include "rt_test.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <functional>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace nift::rt::host;
using std::chrono::milliseconds;

namespace {

const char* kSelf = nullptr;

std::string new_tag() {
    unsigned char raw[8] = {};
    const int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) { ssize_t n = ::read(fd, raw, sizeof raw); (void)n; ::close(fd); }
    static const char* hex = "0123456789abcdef";
    std::string tag = "cp16-";
    for (unsigned char c : raw) { tag.push_back(hex[c >> 4]); tag.push_back(hex[c & 15]); }
    return tag;
}

std::vector<long> tagged(const std::string& tag) {
    std::vector<long> out;
    const std::string needle = "NIFT_TEST_PROC_TAG=" + tag;
    DIR* dir = ::opendir("/proc");
    if (!dir) return out;
    while (dirent* e = ::readdir(dir)) {
        if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
        const long pid = std::strtol(e->d_name, nullptr, 10);
        runtime::ProcInfo info;
        if (!runtime::proc_read(pid, info) || info.state == 'Z') continue;
        const std::string path = std::string("/proc/") + e->d_name + "/environ";
        const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        std::string env;
        char buf[4096];
        ssize_t n;
        while ((n = ::read(fd, buf, sizeof buf)) > 0) env.append(buf, static_cast<std::size_t>(n));
        ::close(fd);
        std::size_t start = 0;
        while (start < env.size()) {
            const std::size_t end = env.find('\0', start);
            if (env.compare(start, (end == std::string::npos ? env.size() : end) - start, needle) == 0) { out.push_back(pid); break; }
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    ::closedir(dir);
    return out;
}

bool wait_until(const std::function<bool()>& cond, milliseconds bound) {
    const auto end = Clock::now() + bound;
    while (Clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(milliseconds(10));
    }
    return cond();
}

bool none_tagged(const std::string& tag, milliseconds bound = milliseconds(3000)) {
    return wait_until([&] { return tagged(tag).empty(); }, bound);
}

// Kills and reaps anything left with this tag (test hygiene, never an assertion).
void kill_tagged(const std::string& tag) {
    for (long pid : tagged(tag)) ::kill(static_cast<pid_t>(pid), SIGKILL);
    wait_until([&] { return tagged(tag).empty(); }, milliseconds(2000));
}

int zombie_children() {
    int n = 0;
    const long self = ::getpid();
    for (const auto& p : runtime::proc_scan()) if (p.ppid == self && p.state == 'Z') ++n;
    return n;
}

int fd_count() {
    int n = 0;
    DIR* dir = ::opendir("/proc/self/fd");
    if (!dir) return -1;
    while (dirent* e = ::readdir(dir)) if (e->d_name[0] != '.') ++n;
    ::closedir(dir);
    return n - 1;  // the DIR's own descriptor
}

double ms_since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

SpawnRequest req(std::vector<std::string> argv, OutputMode mode, const std::string& tag) {
    SpawnRequest r;
    SpawnStage s;
    s.program = argv.front();
    s.args.assign(argv.begin() + 1, argv.end());
    s.env.push_back({"NIFT_TEST_PROC_TAG", tag});
    r.stages.push_back(s);
    r.mode = mode;
    r.stdin_mode = StdinMode::Null;
    r.kill_after = milliseconds(300);
    r.post_kill_wait = milliseconds(2000);
    return r;
}

void add_stage(SpawnRequest& r, std::vector<std::string> argv, const std::string& tag) {
    SpawnStage s;
    s.program = argv.front();
    s.args.assign(argv.begin() + 1, argv.end());
    s.env.push_back({"NIFT_TEST_PROC_TAG", tag});
    r.stages.push_back(s);
}

ProcessResult run_ok(SpawnRequest r) {
    HostStatus st;
    HostError e;
    auto result = ProcessGroup::run(std::move(r), st, e);
    CHECK(st == HostStatus::Ok);
    return result;
}

ProcessGroupPtr start_ok(SpawnRequest r) {
    HostStatus st;
    HostError e;
    auto g = ProcessGroup::start(std::move(r), st, e);
    CHECK(st == HostStatus::Ok && g);
    return g;
}

const OutputMode kBlockingModes[] = {OutputMode::FileCapture, OutputMode::PipeCapture, OutputMode::Inherit};

// ------------------------------------------------------------------ tests
void launch_vs_exit_each_mode() {
    const std::string tag = new_tag();
    for (OutputMode mode : {OutputMode::FileCapture, OutputMode::PipeCapture, OutputMode::Inherit, OutputMode::Interactive}) {
        auto g = start_ok(req({"definitely-missing-cp16"}, mode, tag));
        const auto& r = g->wait();
        CHECK(!r.launched);
        CHECK(r.exit_code == 127);
        CHECK(r.error == "executable not found: definitely-missing-cp16");
        CHECK(r.state == GroupState::LaunchFailed);
        CHECK(r.stages.size() == 1 && r.stages[0].state == StageState::LaunchFailed);
        auto g2 = start_ok(req({"sh", "-c", "exit 3"}, mode, tag));
        const auto& r2 = g2->wait();
        CHECK(r2.launched && r2.exit_code == 3 && r2.error.empty() && r2.state == GroupState::Exited);
        CHECK(r2.stages[0].state == StageState::Exited);
    }
    // permission denied (126) and interpreter missing
    char dir[] = "/tmp/cp16-launch-XXXXXX";
    CHECK(::mkdtemp(dir) != nullptr);
    const std::string noexec = std::string(dir) + "/noexec.sh";
    const std::string badinterp = std::string(dir) + "/badinterp.sh";
    { FILE* f = std::fopen(noexec.c_str(), "w"); std::fputs("#!/bin/sh\necho hi\n", f); std::fclose(f); ::chmod(noexec.c_str(), 0644); }
    { FILE* f = std::fopen(badinterp.c_str(), "w"); std::fputs("#!/missing-interp-cp16\n", f); std::fclose(f); ::chmod(badinterp.c_str(), 0755); }
    auto r = run_ok(req({noexec}, OutputMode::FileCapture, tag));
    CHECK(!r.launched && r.exit_code == 126 && r.error == "permission denied: " + noexec);
    r = run_ok(req({badinterp}, OutputMode::FileCapture, tag));
    CHECK(!r.launched && r.exit_code == 127 && r.error == "interpreter not found: /missing-interp-cp16 (from " + badinterp + ")");
    // setup failure in the child (cwd) is launched:false, 126
    auto bad_cwd = req({"true"}, OutputMode::FileCapture, tag);
    bad_cwd.stages[0].cwd = "/nonexistent-cp16-dir";
    r = run_ok(bad_cwd);
    CHECK(!r.launched && r.exit_code == 126 && r.error.rfind("cannot change directory to /nonexistent-cp16-dir: ", 0) == 0);
    // pipeline: middle stage missing; top-level launched (stages started), per-stage failure kept
    auto p = req({"sh", "-c", "echo a"}, OutputMode::PipeCapture, tag);
    add_stage(p, {"missing-mid-cp16"}, tag);
    add_stage(p, {"cat"}, tag);
    r = run_ok(p);
    CHECK(r.launched && r.stages.size() == 3);
    CHECK(r.stages[1].state == StageState::LaunchFailed && !r.stages[1].launched && r.stages[1].exit_code == 127);
    CHECK(r.stages[1].error == "executable not found: missing-mid-cp16");
    CHECK(r.stages[2].state == StageState::Exited && r.exit_code == 0);
    // signals: 128+n
    r = run_ok(req({"sh", "-c", "kill -TERM $$"}, OutputMode::FileCapture, tag));
    CHECK(r.launched && r.exit_code == 143 && r.signal == SIGTERM && r.stages[0].state == StageState::Signaled);
    CHECK(none_tagged(tag));
    std::remove(noexec.c_str());
    std::remove(badinterp.c_str());
    ::rmdir(dir);
}

void capture_basic() {
    const std::string tag = new_tag();
    for (OutputMode mode : {OutputMode::FileCapture, OutputMode::PipeCapture}) {
        auto r = run_ok(req({"sh", "-c", "printf out; printf err >&2; exit 2"}, mode, tag));
        CHECK(r.out == "out" && r.err == "err" && r.exit_code == 2);
        auto t = req({"sh", "-c", "cat; echo done"}, mode, tag);
        t.stdin_mode = StdinMode::Text;
        t.stdin_text = "line1\nline2\n";
        r = run_ok(t);
        CHECK(r.out == "line1\nline2\ndone\n");
        // stderr of every stage goes to the one capture; stdout of the last
        auto p = req({"sh", "-c", "echo one; echo e1 >&2"}, mode, tag);
        add_stage(p, {"sh", "-c", "tr a-z A-Z; echo e2 >&2"}, tag);
        r = run_ok(p);
        CHECK(r.out == "ONE\n");
        CHECK(r.err == "e1\ne2\n" || r.err == "e2\ne1\n");
    }
    // legacy FileCapture: children see a regular, seekable stdout
    auto r = run_ok(req({"sh", "-c", "test -f /dev/stdout && echo regular"}, OutputMode::FileCapture, tag));
    CHECK(r.out == "regular\n");
    r = run_ok(req({"sh", "-c", "test -p /dev/stdout && echo pipe"}, OutputMode::PipeCapture, tag));
    CHECK(r.out == "pipe\n");
    // C3: own process group, not Nift's
    r = run_ok(req({"sh", "-c", "ps -o pgid= -p $$ 2>/dev/null || cut -d' ' -f5 /proc/$$/stat"}, OutputMode::FileCapture, tag));
    CHECK(std::strtol(r.out.c_str(), nullptr, 10) != ::getpgrp());
    // redirections
    char path[] = "/tmp/cp16-redir-XXXXXX";
    const int fd = ::mkstemp(path);
    ::close(fd);
    auto red = req({"sh", "-c", "echo to-file"}, OutputMode::FileCapture, tag);
    red.stages[0].stdout_path = path;
    r = run_ok(red);
    CHECK(r.out.empty());
    red.stages[0].stdout_append = true;
    r = run_ok(red);
    FILE* f = std::fopen(path, "r");
    char buf[64] = {};
    std::size_t n = std::fread(buf, 1, sizeof buf - 1, f);
    std::fclose(f);
    CHECK(std::string(buf, n) == "to-file\nto-file\n");
    auto in = req({"cat"}, OutputMode::FileCapture, tag);
    in.stdin_mode = StdinMode::Inherit;
    in.stages[0].stdin_path = path;
    r = run_ok(in);
    CHECK(r.out == "to-file\nto-file\n");
    std::remove(path);
    // environment overrides
    auto env = req({"sh", "-c", "printf %s \"$CP16_X\""}, OutputMode::PipeCapture, tag);
    env.stages[0].env.push_back({"CP16_X", "v1"});
    r = run_ok(env);
    CHECK(r.out == "v1");
}

void own_blocking_reaped() {
    const std::string tag = new_tag();
    const int fds_before = fd_count();
    for (int i = 0; i < 300; ++i) {
        const OutputMode mode = kBlockingModes[i % 2];
        auto r = run_ok(req({"true"}, mode, tag));
        if (!(r.launched && r.exit_code == 0)) { CHECK(false); break; }
    }
    CHECK(zombie_children() == 0);
    CHECK(fd_count() == fds_before);
    CHECK(none_tagged(tag, milliseconds(500)));
}

void drain_both_large() {
    const std::string tag = new_tag();
    // 16 MiB on each stream, interleaved in 64 KiB writes: never one stream to EOF first.
    const char* script =
        "python3 -c \"import os,sys\n"
        "a=b'a'*65536; b=b'b'*65536\n"
        "for i in range(256):\n"
        "  os.write(1,a); os.write(2,b)\n\"";
    for (OutputMode mode : {OutputMode::PipeCapture, OutputMode::FileCapture, OutputMode::Interactive}) {
        auto r0 = req({"sh", "-c", script}, mode, tag);
        r0.timeout = milliseconds(60000);
        auto g = start_ok(r0);
        const auto& r = g->wait();  // interactive: wait() without reading cannot deadlock (§11.6)
        CHECK(r.exit_code == 0 && !r.timed_out);
        CHECK(r.out.size() == 16u * 1024 * 1024 && r.err.size() == 16u * 1024 * 1024);
        CHECK(r.out.find_first_not_of('a') == std::string::npos);
        CHECK(r.err.find_first_not_of('b') == std::string::npos);
    }
}

void drain_stdin_before_read() {
    const std::string tag = new_tag();
    // child writes 1 MiB to stderr BEFORE reading 1 MiB of stdin
    const char* script = "head -c 1048576 /dev/zero >&2; wc -c";
    auto t = req({"sh", "-c", script}, OutputMode::PipeCapture, tag);
    t.stdin_mode = StdinMode::Text;
    t.stdin_text.assign(1u << 20, 'x');
    t.timeout = milliseconds(30000);
    auto r = run_ok(t);
    CHECK(!r.timed_out && r.err.size() == (1u << 20));
    CHECK(std::strtol(r.out.c_str(), nullptr, 10) == (1 << 20));
    // the same through the handle API
    auto g = start_ok(req({"sh", "-c", script}, OutputMode::Interactive, tag));
    HostError e;
    CHECK(g->write(std::string(1u << 20, 'y'), e) == HostStatus::Ok);
    CHECK(g->close_stdin(e) == HostStatus::Ok);
    auto line = g->read_line();
    CHECK(line && std::strtol(line->c_str(), nullptr, 10) == (1 << 20));
    const auto& rr = g->wait();
    CHECK(rr.exit_code == 0 && rr.err.size() == (1u << 20));
    CHECK(g->write("late", e) == HostStatus::Fatal && e.code == "process.stdin_closed");
}

void interactive_lines() {
    const std::string tag = new_tag();
    auto g = start_ok(req({"sh", "-c", "while read x; do echo \"got $x\"; done; echo bye"}, OutputMode::Interactive, tag));
    HostError e;
    CHECK(g->running());
    CHECK(g->write("one\n", e) == HostStatus::Ok);
    auto l1 = g->read_line();
    CHECK(l1 && *l1 == "got one");
    CHECK(g->write("two\nthree\n", e) == HostStatus::Ok);
    CHECK(g->read_line().value_or("") == "got two");
    CHECK(g->read_line().value_or("") == "got three");
    CHECK(g->close_stdin(e) == HostStatus::Ok);
    CHECK(g->close_stdin(e) == HostStatus::Ok);  // idempotent
    CHECK(g->read_line().value_or("") == "bye");
    CHECK(!g->read_line());  // EOF
    const auto& r = g->wait();
    CHECK(r.exit_code == 0 && !r.cancelled);
    CHECK(&g->wait() == &r);  // repeatable
    CHECK(!g->running());
    CHECK(!g->cancel());  // after exit: no-op
    CHECK(!g->wait().cancelled);
}

void deadlines() {
    const std::string tag = new_tag();
    // sleeping child
    auto t0 = Clock::now();
    auto s = req({"sleep", "30"}, OutputMode::FileCapture, tag);
    s.timeout = milliseconds(200);
    auto r = run_ok(s);
    double ms = ms_since(t0);
    CHECK(r.timed_out && !r.cancelled && r.state == GroupState::TimedOut);
    CHECK(r.stages[0].signal == SIGTERM && r.exit_code == 143);
    CHECK(ms >= 190 && ms < 2000);
    // blocked reading a stdin Nift never closes (interactive, never written)
    t0 = Clock::now();
    auto br = req({"sh", "-c", "read x; echo never"}, OutputMode::Interactive, tag);
    br.timeout = milliseconds(200);
    auto g = start_ok(br);
    r = g->wait();
    ms = ms_since(t0);
    CHECK(r.timed_out && r.out.empty() && ms < 3000);
    // blocked writing to a full pipe nobody reads
    t0 = Clock::now();
    auto bw = req({"yes"}, OutputMode::Interactive, tag);
    bw.timeout = milliseconds(300);
    bw.buffer_limit = 64 * 1024;
    bw.capture_limit = 64u * 1024 * 1024;
    g = start_ok(bw);
    std::this_thread::sleep_for(milliseconds(500));  // the child blocks on the full pipe meanwhile
    r = g->wait();
    ms = ms_since(t0);
    CHECK((r.timed_out || !r.capture_limit_exceeded.empty()) && ms < 4000);
    // child ignores SIGTERM: SIGKILL after kill_after
    t0 = Clock::now();
    auto it = req({"sh", "-c", "trap '' TERM; while :; do sleep 0.05; done"}, OutputMode::PipeCapture, tag);
    it.timeout = milliseconds(200);
    it.kill_after = milliseconds(400);
    r = run_ok(it);
    ms = ms_since(t0);
    CHECK(r.timed_out && r.stages[0].signal == SIGKILL && r.exit_code == 137);
    CHECK(ms >= 550 && ms < 3000);
    // a deadline never applies to a call that did not ask for one: a quick run is unaffected
    r = run_ok(req({"sh", "-c", "sleep 0.3; echo ok"}, OutputMode::PipeCapture, tag));
    CHECK(!r.timed_out && r.out == "ok\n");
    CHECK(none_tagged(tag));
    CHECK(zombie_children() == 0);
}

void default_grace_2000ms() {
    const std::string tag = new_tag();
    auto r0 = req({"sh", "-c", "trap '' TERM; while :; do sleep 0.05; done"}, OutputMode::Interactive, tag);
    r0.kill_after = milliseconds(2000);  // the D-030 default value, set explicitly here
    CHECK(SpawnRequest().kill_after == milliseconds(2000));
    auto g = start_ok(r0);
    std::this_thread::sleep_for(milliseconds(100));
    const auto t0 = Clock::now();
    CHECK(g->cancel());
    const auto& r = g->wait();
    const double ms = ms_since(t0);
    CHECK(r.cancelled && r.stages[0].signal == SIGKILL);
    CHECK(ms >= 1900 && ms < 5000);
}

void cancellation() {
    const std::string tag = new_tag();
    // pipeline: every stage gets SIGTERM at once (no SIGPIPE-first artefact)
    auto p = req({"yes"}, OutputMode::Interactive, tag);
    add_stage(p, {"gzip", "-c"}, tag);
    add_stage(p, {"cat"}, tag);
    auto g = start_ok(p);
    std::this_thread::sleep_for(milliseconds(200));
    CHECK(g->running());
    CHECK(g->cancel());
    CHECK(g->cancel());  // idempotent while running
    const auto& r = g->wait();
    CHECK(r.cancelled && !r.timed_out && r.state == GroupState::Cancelled);
    CHECK(r.stages.size() == 3);
    for (const auto& st : r.stages) CHECK(st.state == StageState::Signaled && st.signal == SIGTERM && st.exit_code == 143);
    CHECK(!g->cancel());  // after exit
    // Child scope: only the targeted stage is signaled
    auto c = req({"sleep", "30"}, OutputMode::Interactive, tag);
    add_stage(c, {"sleep", "30"}, tag);
    g = start_ok(c);
    std::this_thread::sleep_for(milliseconds(100));
    CHECK(g->cancel(CancelScope::Child));
    CHECK(g->pump(Clock::now() + milliseconds(300)) == false);  // stage 2 still running
    auto stages = g->stages();
    CHECK(stages[0].state == StageState::Signaled && stages[0].signal == SIGTERM);
    CHECK(stages[1].state == StageState::Running);
    CHECK(g->running());
    // finish it (pipeline), it is still the same sequence object: escalation per group
    ::kill(static_cast<pid_t>(stages[1].pid), SIGKILL);
    const auto& r2 = g->wait();
    CHECK(r2.stages[1].signal == SIGKILL);
    // Tree scope (Linux): a descendant that left the group is signaled too
    auto t = req({"sh", "-c", "setsid sleep 30 & wait"}, OutputMode::Interactive, tag);
    g = start_ok(t);
    CHECK(wait_until([&] { return tagged(tag).size() >= 2; }, milliseconds(2000)));
    CHECK(g->cancel(CancelScope::Tree));
    g->wait();
    CHECK(none_tagged(tag));
    CHECK(zombie_children() == 0);
}

void exit_cancel_race() {
    const std::string tag = new_tag();
    int done = 0;
    for (int i = 0; i < 200; ++i) {
        auto g = start_ok(req({"true"}, OutputMode::Interactive, tag));
        if (i % 3 == 1) std::this_thread::sleep_for(std::chrono::microseconds(200));
        g->cancel();
        const auto& r = g->wait();
        if (r.state == GroupState::Exited || r.state == GroupState::Cancelled) ++done;
    }
    CHECK(done == 200);
    CHECK(zombie_children() == 0);
    CHECK(none_tagged(tag, milliseconds(500)));
}

void capture_limit_loud() {
    const std::string tag = new_tag();
    for (OutputMode mode : {OutputMode::PipeCapture, OutputMode::FileCapture}) {
        auto r0 = req({"sh", "-c", "head -c 8388608 /dev/zero; sleep 30"}, mode, tag);
        r0.capture_limit = 1u << 20;
        const auto t0 = Clock::now();
        auto r = run_ok(r0);
        CHECK(r.capture_limit_exceeded == "stdout");
        CHECK(r.out.empty() && r.err.empty());
        CHECK(r.limit_message == "run: captured stdout of sh exceeds the read limit of 1048576 bytes (NIFT_READ_LIMIT)");
        CHECK(ms_since(t0) < 3000);  // the group is cancelled, not waited for
    }
    // exactly at the limit is fine
    auto ok = req({"head", "-c", "4096", "/dev/zero"}, OutputMode::PipeCapture, tag);
    ok.capture_limit = 4096;
    auto r = run_ok(ok);
    CHECK(r.capture_limit_exceeded.empty() && r.out.size() == 4096);
    ok.mode = OutputMode::FileCapture;
    r = run_ok(ok);
    CHECK(r.capture_limit_exceeded.empty() && r.out.size() == 4096);
    auto err = req({"sh", "-c", "head -c 5000 /dev/zero >&2"}, OutputMode::FileCapture, tag);
    err.capture_limit = 4096;
    r = run_ok(err);
    CHECK(r.capture_limit_exceeded == "stderr");
    CHECK(none_tagged(tag));
}

void leftovers_c1_and_holders() {
    const std::string tag = new_tag();
    // FileCapture (legacy): run() returns after the stage; the background
    // descendant stays alive until the context ends, then is terminated silently.
    auto g = start_ok(req({"sh", "-c", "sleep 30 & echo started"}, OutputMode::FileCapture, tag));
    const auto t0 = Clock::now();
    const auto& r = g->wait();
    CHECK(r.out == "started\n" && ms_since(t0) < 2000);
    CHECK(tagged(tag).size() == 1);  // the sleep survives the call (§11.3 row 1)
    std::vector<std::string> warnings;
    runtime::set_warning_sink([&](const std::string& w) { warnings.push_back(w); });
    CleanupReport report;
    {
        ContextCleanup end("script", false);
        report = process_cleanup(g, ProcessRelease::ContextEnd);
        CHECK(end.wait() == 0);
    }
    // (queued or already settled: a plain `sleep` dies of SIGTERM at once)
    CHECK(report.leftovers_terminated && !report.live_stage_terminated && !report.warned && !report.handed_to_reaper);
    CHECK(warnings.empty());
    CHECK(none_tagged(tag));
    // PipeCapture: a descendant holding the pipe does not hang the call; it is terminated.
    const auto t1 = Clock::now();
    auto h = req({"sh", "-c", "(sleep 30 &); echo x"}, OutputMode::PipeCapture, tag);
    auto rr = run_ok(h);
    CHECK(rr.out == "x\n" && ms_since(t1) < 2000 && !rr.cancelled);
    CHECK(none_tagged(tag));
    runtime::set_warning_sink(nullptr);
}

// The process hook as CP16 registers it for HandleKind::Process (here on
// the TEMPORARY stand-in table; with CP17 the lambda body is unchanged).
tmp_handles::CleanupOutcome process_hook(const tmp_handles::CleanupEvent& ev) {
    using tmp_handles::ReleaseReason;
    const ProcessRelease r = ev.reason == ReleaseReason::Explicit ? ProcessRelease::Explicit
                           : ev.reason == ReleaseReason::Abandoned ? ProcessRelease::Abandoned
                           : ev.reason == ReleaseReason::ContextEnd ? ProcessRelease::ContextEnd : ProcessRelease::Shutdown;
    process_cleanup(std::static_pointer_cast<ProcessGroup>(ev.resource), r);
    return tmp_handles::CleanupOutcome::success();
}

void adapter_lifecycle() {
    using namespace tmp_handles;
    const std::string tag = new_tag();
    std::vector<std::string> warnings;
    runtime::set_warning_sink([&](const std::string& w) { warnings.push_back(w); });
    TemporaryTable table(process_hook);  // TEMPORARY until CP17 (see handle_adapter_tmp.h)
    const ContextId script = table.open_context(ContextKind::Script, kDetached);
    // abandoned while running: async termination + one warning, script not blocked
    auto running = start_ok(req({"sh", "-c", "trap '' TERM; sleep 30"}, OutputMode::Interactive, tag));
    const long pid = running->pid();
    const auto h = table.create(script, running);
    running.reset();
    auto t0 = Clock::now();
    CHECK(table.drop(h));
    CHECK(ms_since(t0) < 200);  // asynchronous
    CHECK(warnings.size() == 1 && warnings[0] == "warning: process 'sh' (pid " + std::to_string(pid) + ") was abandoned while running; terminated");
    CHECK(runtime::reaper_wait_idle(milliseconds(5000)));
    CHECK(none_tagged(tag));
    // abandoned after exit: silent
    warnings.clear();
    auto exited = start_ok(req({"true"}, OutputMode::Interactive, tag));
    CHECK(wait_until([&] { return !exited->running(); }, milliseconds(2000)));
    const auto h2 = table.create(script, exited);
    exited.reset();
    table.drop(h2);
    CHECK(runtime::reaper_wait_idle(milliseconds(3000)));
    CHECK(warnings.empty());
    // explicit release of a running process: terminated, no warning
    auto rel = start_ok(req({"sleep", "30"}, OutputMode::Interactive, tag));
    const auto h3 = table.create(script, rel);
    CHECK(table.release(h3, script));
    CHECK(runtime::reaper_wait_idle(milliseconds(5000)));
    CHECK(warnings.empty() && rel->finished() && rel->result().cancelled);
    rel.reset();
    // still running at context end: one warning per group (not per stage);
    // several groups are waited for once (not one grace period each)
    const ContextId ctx = table.open_context(ContextKind::Script, kDetached);
    std::vector<ProcessGroupPtr> groups;
    for (int i = 0; i < 3; ++i) {
        auto p = req({"sh", "-c", "trap '' TERM; sleep 30"}, OutputMode::Interactive, tag);
        add_stage(p, {"sleep", "30"}, tag);
        p.kill_after = milliseconds(500);
        groups.push_back(start_ok(p));
        table.create(ctx, groups.back());
    }
    std::this_thread::sleep_for(milliseconds(100));
    t0 = Clock::now();
    {
        ContextCleanup end("script", false);
        table.close_context(ctx);
        CHECK(ms_since(t0) < 300);  // the hooks only start the sequence
        CHECK(end.wait() == 0);
    }
    const double ms = ms_since(t0);
    CHECK(ms >= 450 && ms < 1500);  // one grace period for all three groups
    CHECK(warnings.size() == 3);
    if (!warnings.empty())
        CHECK(warnings[0] == "warning: process 'sh' (pid " + std::to_string(groups[2]->pid()) + ") was still running when script ended; terminated");
    for (auto& g : groups) CHECK(g->finished() && g->result().cancelled && g->result().stages[0].signal == SIGKILL);
    CHECK(none_tagged(tag, milliseconds(500)));
    // context ended on an error: same cleanup, no warning
    warnings.clear();
    const ContextId bad = table.open_context(ContextKind::PageRender, kDetached);
    auto err = start_ok(req({"sleep", "30"}, OutputMode::Interactive, tag));
    table.create(bad, err);
    {
        ContextCleanup end("page 'about'", true);
        table.close_context(bad);
    }
    CHECK(warnings.empty() && err->finished());
    CHECK(none_tagged(tag, milliseconds(500)));
    // exited before the context ended: reaped, no warning
    const ContextId q = table.open_context(ContextKind::Script, kDetached);
    auto quick = start_ok(req({"true"}, OutputMode::Interactive, tag));
    table.create(q, quick);
    CHECK(wait_until([&] { return !quick->running(); }, milliseconds(2000)));
    {
        ContextCleanup end("script", false);
        table.close_context(q);
    }
    CHECK(warnings.empty());
    // detached: never touched by managed cleanup or shutdown
    const std::string dtag = new_tag();
    const ContextId d = table.open_context(ContextKind::Script, kDetached);
    auto det = start_ok(req({"sleep", "30"}, OutputMode::Detached, dtag));
    CHECK(det->detached() && det->pid() > 0 && det->result().state == GroupState::Detached);
    const auto hd = table.create(d, det);
    CHECK(table.detach(hd, d));
    {
        ContextCleanup end("script", false);
        table.close_context(d);
    }
    table.shutdown();
    CHECK(tagged(dtag).size() == 1);
    CHECK(!process_cleanup(det, ProcessRelease::ContextEnd).live_stage_terminated);
    CHECK(tagged(dtag).size() == 1);
    // detached: own session, not in Nift's group
    runtime::ProcInfo info;
    CHECK(runtime::proc_read(det->pid(), info) && info.session != static_cast<long>(::getsid(0)) && info.pgrp != ::getpgrp());
    // cancel is a request while Nift lives
    CHECK(det->running());
    CHECK(det->cancel());
    runtime::reaper_adopt(det);
    CHECK(none_tagged(dtag));
    runtime::set_warning_sink(nullptr);
    CHECK(runtime::reaper_wait_idle(milliseconds(5000)));
}

void detached_rules() {
    const std::string tag = new_tag();
    HostStatus st;
    HostError e;
    auto b = req({"sleep", "30"}, OutputMode::Detached, tag);
    b.in_build = true;
    CHECK(!ProcessGroup::start(b, st, e) && st == HostStatus::Fatal && e.code == "process.detach_in_build");
    auto d = req({"sleep", "30"}, OutputMode::Detached, tag);
    d.timeout = milliseconds(10);
    CHECK(!ProcessGroup::start(d, st, e) && e.message == "run: deadline is not allowed for a detached process");
    auto m = req({"definitely-missing-cp16"}, OutputMode::Detached, tag);
    auto g = start_ok(m);
    CHECK(g && !g->result().launched && g->result().exit_code == 127 && g->result().error == "executable not found: definitely-missing-cp16");
    CHECK(none_tagged(tag, milliseconds(300)));
    CHECK(zombie_children() == 0);
}

void low_fd_limit() {
    const std::string tag = new_tag();
    struct rlimit old {};
    ::getrlimit(RLIMIT_NOFILE, &old);
    const int base = fd_count();
    struct rlimit low = old;
    low.rlim_cur = static_cast<rlim_t>(base + 3);  // room for one pipe, not for a capture pair + report pipe
    ::setrlimit(RLIMIT_NOFILE, &low);
    auto r = run_ok(req({"true"}, OutputMode::PipeCapture, tag));
    ::setrlimit(RLIMIT_NOFILE, &old);
    CHECK(!r.launched && r.state == GroupState::LaunchFailed);
    CHECK(r.error == "cannot create pipe: Too many open files");
    CHECK(fd_count() == base);
    // a limit that fits: 300 launches, no descriptor leak
    low.rlim_cur = static_cast<rlim_t>(base + 12);
    ::setrlimit(RLIMIT_NOFILE, &low);
    bool all = true;
    for (int i = 0; i < 300; ++i) {
        auto ok = run_ok(req({"true"}, kBlockingModes[i % 2], tag));
        all = all && ok.launched && ok.exit_code == 0;
    }
    ::setrlimit(RLIMIT_NOFILE, &old);
    CHECK(all);
    CHECK(fd_count() == base);
    CHECK(zombie_children() == 0);
}

struct SpySink : TraceSink {
    std::vector<std::string> lines;
    void write_line(const std::string& l) override { lines.push_back(l); }
    std::string context() override { return "script"; }
};

void trace_spawn() {
    const std::string tag = new_tag();
    for (int version : {1, 2}) {
        SpySink sink;
        TraceChannel channel(version, &sink);
        HostCallEnv env{&channel, nullptr, nullptr};
        auto p = req({"sh", "-c", "echo a b"}, OutputMode::PipeCapture, tag);
        p.stages[0].cwd = "/tmp/../tmp";
        add_stage(p, {"missing-cp16-stage"}, tag);
        HostStatus st;
        HostError e;
        auto g = start_traced(env, p, st, e);
        CHECK(g && st == HostStatus::Ok);
        g->wait();
        CHECK(sink.lines.size() == 2);
        if (version == 1 && sink.lines.size() == 2) {
            CHECK(sink.lines[0] == "{\"ctx\":\"script\",\"op\":\"spawn\",\"arg\":\"sh -c echo a b\",\"res\":\"/tmp/../tmp\"}\n");
            CHECK(sink.lines[1] == "{\"ctx\":\"script\",\"op\":\"spawn\",\"arg\":\"missing-cp16-stage\",\"res\":\"\"}\n");
        }
        if (version == 2 && sink.lines.size() == 2) {
            CHECK(sink.lines[0] == "{\"v\":2,\"ctx\":\"script\",\"op\":\"spawn\",\"arg\":\"sh\",\"res\":\"launched\",\"argv\":[\"sh\",\"-c\",\"echo a b\"],\"cwd\":\"/tmp\"}\n");
            CHECK(sink.lines[1].find("\"res\":\"launch_failed\"") != std::string::npos);
        }
        // refused before launch: v2 error line only
        sink.lines.clear();
        auto b = req({"sleep", "1"}, OutputMode::Detached, tag);
        b.in_build = true;
        CHECK(!start_traced(env, b, st, e) && e.code == "process.detach_in_build");
        if (version == 1) CHECK(sink.lines.empty());
        else CHECK(sink.lines.size() == 1 && sink.lines[0].find("\"err\":\"process.detach_in_build\"") != std::string::npos);
    }
}

// ---- subprocess modes (C2, PDEATHSIG) ----
int c2_child(const std::string& mtag, const std::string& dtag) {
    runtime::SignalCleanupOptions o;
    o.kill_after = milliseconds(300);
    o.post_kill_wait = milliseconds(2000);
    runtime::install_signal_cleanup(o);
    HostStatus st;
    HostError e;
    auto det = ProcessGroup::start(req({"sleep", "30"}, OutputMode::Detached, dtag), st, e);
    auto ign = req({"sh", "-c", "trap '' TERM; echo ready; while :; do sleep 0.05; done"}, OutputMode::FileCapture, mtag);
    ProcessGroup::run(ign, st, e);  // blocks until Nift's SIGTERM cleanup kills it
    return 3;                       // never reached: Nift dies of SIGTERM
}

int pdeath_child(const std::string& tag) {
    HostStatus st;
    HostError e;
    auto g = ProcessGroup::start(req({"sleep", "30"}, OutputMode::Interactive, tag), st, e);
    for (;;) ::pause();
}

pid_t spawn_self(std::vector<std::string> args) {
    const pid_t pid = ::fork();
    if (pid == 0) {
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(kSelf));
        for (auto& a : args) argv.push_back(a.data());
        argv.push_back(nullptr);
        const int null = ::open("/dev/null", O_WRONLY);
        ::dup2(null, 1);
        ::execv("/proc/self/exe", argv.data());
        ::_exit(127);
    }
    return pid;
}

void sigterm_to_nift_c2() {
    const std::string mtag = new_tag(), dtag = new_tag();
    const pid_t child = spawn_self({"--c2-child", mtag, dtag});
    CHECK(wait_until([&] { return !tagged(mtag).empty() && !tagged(dtag).empty(); }, milliseconds(5000)));
    const auto t0 = Clock::now();
    ::kill(child, SIGTERM);
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    const double ms = ms_since(t0);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);  // signal-derived exit status
    CHECK(ms < 300 + 2000 + 1500);
    CHECK(none_tagged(mtag, milliseconds(1000)));  // managed child (ignoring TERM) was killed
    CHECK(tagged(dtag).size() == 1);              // detached untouched
    kill_tagged(dtag);
    for (int i = 0; i < 50 && zombie_children() > 0; ++i) { int s; ::waitpid(-1, &s, WNOHANG); std::this_thread::sleep_for(milliseconds(10)); }
}

void sigkill_pdeathsig_linux() {
    const std::string tag = new_tag();
    const pid_t child = spawn_self({"--pdeath-child", tag});
    CHECK(wait_until([&] { return !tagged(tag).empty(); }, milliseconds(5000)));
    ::kill(child, SIGKILL);
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    CHECK(none_tagged(tag, milliseconds(2000)));  // direct stages die with Nift (Linux PDEATHSIG)
    for (int i = 0; i < 50 && zombie_children() > 0; ++i) { int s; ::waitpid(-1, &s, WNOHANG); std::this_thread::sleep_for(milliseconds(10)); }
}

void escapees_subreaper() {
    const std::string tag = new_tag(), dtag = new_tag();
    if (!runtime::enable_subreaper()) { std::printf("escapees_subreaper: SKIPPED (no PR_SET_CHILD_SUBREAPER)\n"); return; }
    CHECK(runtime::subreaper_enabled());
    // a pipe holder in another session does not hang a pipe-mode call
    const auto t0 = Clock::now();
    auto r = run_ok(req({"sh", "-c", "setsid sleep 30 & echo x"}, OutputMode::PipeCapture, tag));
    CHECK(r.out == "x\n" && ms_since(t0) < 3000);
    // an escapee of a FileCapture run (setsid, its parent exits) is reparented to Nift
    auto g = start_ok(req({"sh", "-c", "setsid sleep 30 </dev/null >/dev/null 2>&1 &"}, OutputMode::FileCapture, tag));
    g->wait();
    CHECK(wait_until([&] { return tagged(tag).size() == 2; }, milliseconds(2000)));
    for (long pid : tagged(tag)) {
        runtime::ProcInfo info;
        CHECK(runtime::proc_read(pid, info) && info.ppid == ::getpid());
    }
    // group cleanup cannot reach it (it left the group) ...
    {
        ContextCleanup end("script", false);
        process_cleanup(g, ProcessRelease::ContextEnd);
    }
    CHECK(tagged(tag).size() == 2);
    // ... the root-context orphan sweep does; a detached process is spared
    auto det = start_ok(req({"sleep", "30"}, OutputMode::Detached, dtag));
    CHECK(wait_until([&] { return tagged(dtag).size() == 1; }, milliseconds(2000)));
    const std::size_t swept = runtime::sweep_orphans(milliseconds(300), milliseconds(2000));
    CHECK(swept == 2);
    CHECK(none_tagged(tag));
    CHECK(tagged(dtag).size() == 1);
    det->cancel(CancelScope::Pipeline, CancelReason::Explicit, milliseconds(300));
    runtime::reaper_adopt(det);
    CHECK(none_tagged(dtag));
    CHECK(runtime::reaper_wait_idle(milliseconds(5000)));  // the detached pid was reaped (ours under the subreaper)
    CHECK(zombie_children() == 0);
}

} // namespace

int main(int argc, char** argv) {
    kSelf = argv[0];
    if (argc == 4 && std::strcmp(argv[1], "--c2-child") == 0) return c2_child(argv[2], argv[3]);
    if (argc == 3 && std::strcmp(argv[1], "--pdeath-child") == 0) return pdeath_child(argv[2]);
    const std::string only = argc > 1 ? argv[1] : "";
    struct T { const char* name; void (*fn)(); };
    const T tests[] = {
        {"launch_vs_exit_each_mode", launch_vs_exit_each_mode},
        {"capture_basic", capture_basic},
        {"own_blocking_reaped", own_blocking_reaped},
        {"drain_both_large", drain_both_large},
        {"drain_stdin_before_read", drain_stdin_before_read},
        {"interactive_lines", interactive_lines},
        {"deadlines", deadlines},
        {"default_grace_2000ms", default_grace_2000ms},
        {"cancellation", cancellation},
        {"exit_cancel_race", exit_cancel_race},
        {"capture_limit_loud", capture_limit_loud},
        {"leftovers_c1_and_holders", leftovers_c1_and_holders},
        {"adapter_lifecycle", adapter_lifecycle},
        {"detached_rules", detached_rules},
        {"low_fd_limit", low_fd_limit},
        {"trace_spawn", trace_spawn},
        {"sigterm_to_nift_c2", sigterm_to_nift_c2},
        {"sigkill_pdeathsig_linux", sigkill_pdeathsig_linux},
        {"escapees_subreaper", escapees_subreaper},  // last: enables the subreaper for the rest of the process
    };
    for (const auto& t : tests) {
        if (!only.empty() && only != t.name) continue;
        const long before = rt_test::failures();
        const auto t0 = Clock::now();
        t.fn();
        std::printf("%-28s %s (%.0f ms)\n", t.name, rt_test::failures() == before ? "ok" : "FAILED", ms_since(t0));
        std::fflush(stdout);
    }
    return rt_test::finish("host-process-unit");
}
