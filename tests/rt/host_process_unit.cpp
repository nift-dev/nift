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
#include "rt/handles/table.h"
#include "rt/host/process_handles.h"
#include "rt/host/process.h"
#include "rt/host/process_call.h"
#include "rt/host/process_cleanup.h"
#include "rt/host/process_runtime.h"
#include "rt/host/run_request.h"
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
#include <set>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace nift::rt::host;
namespace handles = nift::rt::handles;
using nift::rt::HandleKind;
using nift::rt::HandleStatus;
using nift::rt::Value;
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

// Reads /proc/<pid>/environ. A process inside execve() briefly has an empty
// environ (the new address space is installed before its environment area is
// set up): such a read is retried until it is stable, so a test process is
// never missed (a missed process would turn an orphan check into a false pass;
// stress evidence in cp16-host-ops-impl.md §7).
// Diagnostics of the last tagged() scan: every pid readdir returned, and why
// a pid was skipped or how its environ read went (printed on failure).
struct ScanNote { long pid; std::string what; };
std::set<long> g_scan_seen;
std::vector<ScanNote> g_scan_notes;

std::string read_environ(long pid, std::string* note) {
    const std::string path = "/proc/" + std::to_string(pid) + "/environ";
    std::string env;
    int empties = 0, open_failures = 0, last_errno = 0;
    for (int attempt = 0; attempt < 20; ++attempt) {  // <= ~10 ms: the exec window is far shorter
        env.clear();
        const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            char buf[4096];
            ssize_t n;
            while ((n = ::read(fd, buf, sizeof buf)) > 0) env.append(buf, static_cast<std::size_t>(n));
            if (n < 0) last_errno = errno;
            ::close(fd);
            if (!env.empty()) break;
            ++empties;
        } else {
            last_errno = errno;
            ++open_failures;
        }
        // Empty or unopenable while the process still exists (inside execve():
        // the new address space is installed before its environment area is
        // set up, and opening can race the switch too): look again.
        runtime::ProcInfo info;
        if (!runtime::proc_read(pid, info) || info.state == 'Z' || info.state == 'X') break;
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    if (note && (empties || open_failures))
        *note = "environ retries: empty " + std::to_string(empties) + ", open failed " + std::to_string(open_failures) +
                ", errno " + std::to_string(last_errno) + ", final " + std::to_string(env.size()) + " bytes";
    return env;
}

std::vector<long> tagged(const std::string& tag) {
    std::vector<long> out;
    const std::string needle = "NIFT_TEST_PROC_TAG=" + tag;
    g_scan_seen.clear();
    g_scan_notes.clear();
    DIR* dir = ::opendir("/proc");
    if (!dir) return out;
    while (dirent* e = ::readdir(dir)) {
        if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
        const long pid = std::strtol(e->d_name, nullptr, 10);
        g_scan_seen.insert(pid);
        runtime::ProcInfo info;
        bool have = false;
        for (int attempt = 0; attempt < 3 && !have; ++attempt) {  // stat can race the exec switch as well
            have = runtime::proc_read(pid, info);
            if (!have && ::access(("/proc/" + std::to_string(pid)).c_str(), F_OK) != 0) break;  // really gone
        }
        if (!have) { g_scan_notes.push_back({pid, "stat unreadable"}); continue; }
        if (info.state == 'Z') continue;
        if (pid == 2 || info.ppid == 2) continue;  // kernel threads have no environment
        std::string note;
        const std::string env = read_environ(pid, &note);
        if (!note.empty()) g_scan_notes.push_back({pid, note});
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

// "No zombies while Nift lives": immediate without the subreaper; with it,
// adopted orphans are reaped asynchronously by the host loop (SIGCHLD), so
// the guarantee is "reaped within a bound" -- 1 s here.
bool no_zombies() { return wait_until([] { return zombie_children() == 0; }, milliseconds(1000)); }

int fd_count_raw();
// Descriptors of this process minus those the host loop owns (pidfd watches
// for processes of earlier tests come and go asynchronously); re-measured
// until stable, so a real leak (persistent) is still caught.
int fd_count() {
    int last = -1;
    for (int i = 0; i < 50; ++i) {
        const int now = fd_count_raw() - static_cast<int>(runtime::loop_fd_count());
        if (now == last) return now;
        last = now;
        std::this_thread::sleep_for(milliseconds(5));
    }
    return last;
}

int fd_count_raw() {
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

// A CP17 handle table with the Process kind registered (one per test).
struct Table {
    handles::HandleTable t;
    Table() { CHECK(register_process_kind(t) == HandleStatus::Ok); }
    handles::ContextId open(handles::ContextKind kind = handles::ContextKind::Script, const std::string& label = "script",
                            handles::ContextId parent = handles::kDetached) {
        handles::ContextId id = 0;
        CHECK(t.open_context(kind, parent, id, label) == HandleStatus::Ok);
        return id;
    }
};

// A context ending with `group` (already finished, with background members
// left) as its run() residue: the C1 path through the real table.
handles::CloseReport end_with_residue(Table& tb, const ProcessGroupPtr& group, bool on_error, bool& registered) {
    const auto ctx = tb.open();
    CHECK(register_run_residue(tb.t, ctx, group, registered) == HandleStatus::Ok);
    return tb.t.close_context(ctx, on_error);
}

// A context ending with `group` held through a script-visible handle.
handles::CloseReport end_with_handle(Table& tb, const ProcessGroupPtr& group, bool on_error, const std::string& label = "script") {
    const auto ctx = tb.open(handles::ContextKind::Script, label);
    Value h;
    CHECK(create_process_handle(tb.t, ctx, group, h) == HandleStatus::Ok);
    return tb.t.close_context(ctx, on_error);
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
    CHECK(no_zombies());
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
    CHECK(no_zombies());
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
    g->cancel();  // idempotent: no second sequence (false once the host loop reaped every stage)
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
    CHECK(no_zombies());
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
    CHECK(no_zombies());
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

void dump_events(Clock::time_point since) {
    auto& h = runtime::test_hooks();
    std::lock_guard<std::mutex> lock(h.log_mutex);
    for (const auto& e : h.log) {
        if (e.at < since) continue;
        std::printf("  +%8.3f ms %-15s group=%p a=%ld b=%ld c=%ld %s\n",
                    std::chrono::duration<double, std::milli>(e.at - since).count(), e.what, e.group, e.a, e.b, e.c, e.detail);
    }
}

void leftovers_c1_and_holders() {
    const auto t_start = Clock::now() - milliseconds(3000);  // include late events of earlier tests
    const std::string tag = new_tag();
    // FileCapture (legacy): run() returns after the stage; the background
    // descendant stays alive until the context ends, then is terminated silently.
    auto g = start_ok(req({"sh", "-c", "sleep 30 & echo started"}, OutputMode::FileCapture, tag));
    const auto t0 = Clock::now();
    const auto& r = g->wait();
    CHECK(r.out == "started\n" && ms_since(t0) < 2000);
    {
        const auto left = tagged(tag);
        if (left.size() != 1) {
            std::printf("left count %zu; sh: state %s exit %d signal %d; pgid %ld\n", left.size(), stage_state_name(r.stages[0].state),
                        r.exit_code, r.stages[0].signal, g->pgid());
            for (long p : left) { runtime::ProcInfo i; runtime::proc_read(p, i); std::printf("left %ld state %c ppid %ld pgrp %ld\n", p, i.state, i.ppid, i.pgrp); }
            for (const auto& n : g_scan_notes) std::printf("  scan note: pid %ld: %s\n", n.pid, n.what.c_str());
            dump_events(t_start);
            for (long m : runtime::proc_group_members(g->pgid(), -1)) {
                runtime::ProcInfo i;
                runtime::proc_read(m, i);
                auto slurp = [](const std::string& path) {
                    std::string out; char buf[4096]; ssize_t n;
                    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
                    if (fd < 0) return std::string("<open failed>");
                    while ((n = ::read(fd, buf, sizeof buf)) > 0) out.append(buf, static_cast<std::size_t>(n));
                    ::close(fd);
                    return out;
                };
                std::string cmd = slurp("/proc/" + std::to_string(m) + "/cmdline");
                for (auto& ch : cmd) if (ch == '\0') ch = ' ';
                std::printf("  member %ld state %c ppid %ld cmdline [%s] environ %zu bytes\n", m, i.state, i.ppid, cmd.c_str(),
                            slurp("/proc/" + std::to_string(m) + "/environ").size());
                std::printf("  member %ld %s by the failing scan\n", m, g_scan_seen.count(m) ? "was listed" : "was NOT listed");
            }
            std::this_thread::sleep_for(milliseconds(50));
            std::printf("  rescan after 50 ms: %zu tagged\n", tagged(tag).size());
        }
        CHECK(left.size() == 1);  // the sleep survives the call (§11.3 row 1)
    }
    std::vector<std::string> warnings;
    runtime::set_warning_sink([&](const std::string& w) { warnings.push_back(w); });
    Table tb;
    bool registered = false;
    const auto report = end_with_residue(tb, g, false, registered);
    CHECK(registered);  // the call left a background member: a residue resource in the context
    CHECK(report.status == HandleStatus::Ok && report.failures.empty() && report.cleaned == 1);
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

void handle_lifecycle() {
    const std::string tag = new_tag();
    std::vector<std::string> warnings;
    runtime::set_warning_sink([&](const std::string& w) { warnings.push_back(w); });
    Table tb;
    auto& t = tb.t;
    const auto script = tb.open();
    // abandoned (last counted reference dropped) while running: async termination + one warning
    auto running = start_ok(req({"sh", "-c", "trap '' TERM; sleep 30"}, OutputMode::Interactive, tag));
    const long pid = running->pid();
    Value h;
    CHECK(create_process_handle(t, script, running, h) == HandleStatus::Ok);
    running.reset();
    auto t0 = Clock::now();
    CHECK(t.drop(h) == HandleStatus::Ok);
    CHECK(ms_since(t0) < 200);  // the hook never blocks
    CHECK(warnings.size() == 1 && warnings[0] == "warning: process 'sh' (pid " + std::to_string(pid) + ") was abandoned while running; terminated");
    CHECK(runtime::reaper_wait_idle(milliseconds(5000)));
    CHECK(none_tagged(tag));
    handles::LifeState state;
    handles::TerminationCause cause;
    CHECK(t.lifecycle(h, HandleKind::Process, state, cause) != HandleStatus::Ok);  // closed: runtime.handle_closed for natives
    // natural exit while the handle stays open: the table records Completed (cause Exited)
    auto exited = start_ok(req({"true"}, OutputMode::Interactive, tag));
    Value he;
    CHECK(create_process_handle(t, script, exited, he) == HandleStatus::Ok);
    CHECK(wait_until([&] {
        return t.lifecycle(he, HandleKind::Process, state, cause) == HandleStatus::Ok && state == handles::LifeState::Completed;
    }, milliseconds(2000)));
    CHECK(cause == handles::TerminationCause::Exited);
    warnings.clear();
    CHECK(t.drop(he) == HandleStatus::Ok);  // abandoning an exited process: silent
    CHECK(warnings.empty());
    // deadline on an idle handle: the host loop starts termination, the table records it
    auto dr = req({"sleep", "30"}, OutputMode::Interactive, tag);
    dr.timeout = milliseconds(150);
    auto timed = start_ok(dr);
    Value hd;
    CHECK(create_process_handle(t, script, timed, hd) == HandleStatus::Ok);
    CHECK(wait_until([&] {
        return t.lifecycle(hd, HandleKind::Process, state, cause) == HandleStatus::Ok && state == handles::LifeState::Completed;
    }, milliseconds(3000)));
    CHECK(cause == handles::TerminationCause::Deadline && timed->result().timed_out);
    // explicit close of a running process: terminated, no warning
    auto rel = start_ok(req({"sleep", "30"}, OutputMode::Interactive, tag));
    Value hr;
    CHECK(create_process_handle(t, script, rel, hr) == HandleStatus::Ok);
    CHECK(t.close(hr, HandleKind::Process, script) == HandleStatus::Ok);
    CHECK(runtime::reaper_wait_idle(milliseconds(5000)));
    CHECK(warnings.empty() && rel->finished() && rel->result().cancelled);
    // cancel through the binding: the seam reports Cancel
    auto can = start_ok(req({"sleep", "30"}, OutputMode::Interactive, tag));
    Value hc;
    CHECK(create_process_handle(t, script, can, hc) == HandleStatus::Ok);
    bool anything = false;
    CHECK(cancel_process(t, hc, script, anything) == HandleStatus::Ok && anything);
    can->wait();
    CHECK(wait_until([&] {
        return t.lifecycle(hc, HandleKind::Process, state, cause) == HandleStatus::Ok && state == handles::LifeState::Completed;
    }, milliseconds(2000)));
    CHECK(cause == handles::TerminationCause::Cancel);
    CHECK(t.close_context(script).failures.empty());
    // still running at context end: one warning per group (not per stage); the
    // close returns once every group settled -- one grace period for all three
    const auto ctx = tb.open(handles::ContextKind::Script, "script");
    std::vector<ProcessGroupPtr> groups;
    for (int i = 0; i < 3; ++i) {
        auto p = req({"sh", "-c", "trap '' TERM; sleep 30"}, OutputMode::Interactive, tag);
        add_stage(p, {"sleep", "30"}, tag);
        p.kill_after = milliseconds(500);
        groups.push_back(start_ok(p));
        Value hg;
        CHECK(create_process_handle(t, ctx, groups.back(), hg) == HandleStatus::Ok);
    }
    std::this_thread::sleep_for(milliseconds(100));
    warnings.clear();
    t0 = Clock::now();
    const auto report = t.close_context(ctx);
    const double ms = ms_since(t0);
    CHECK(report.failures.empty() && report.cleaned == 3 && report.label == "script");
    CHECK(ms >= 450 && ms < 1500);
    CHECK(warnings.size() == 3);
    if (!warnings.empty())
        CHECK(warnings[0] == "warning: process 'sh' (pid " + std::to_string(groups[2]->pid()) + ") was still running when script ended; terminated");
    for (auto& g : groups) CHECK(g->finished() && g->result().cancelled && g->result().stages[0].signal == SIGKILL);
    CHECK(none_tagged(tag, milliseconds(500)));
    // context ended on an error: same cleanup, no warning
    warnings.clear();
    auto err = start_ok(req({"sleep", "30"}, OutputMode::Interactive, tag));
    CHECK(end_with_handle(tb, err, true, "page 'about'").failures.empty());
    CHECK(warnings.empty() && err->finished());
    // exited before the context ended: reaped, no warning
    auto quick = start_ok(req({"true"}, OutputMode::Interactive, tag));
    CHECK(wait_until([&] { return !quick->running(); }, milliseconds(2000)));
    CHECK(end_with_handle(tb, quick, false).failures.empty());
    CHECK(warnings.empty());
    // detached: counted handle detached at once; never touched by context end or shutdown
    const std::string dtag = new_tag();
    const auto d = tb.open();
    auto det = start_ok(req({"sleep", "30"}, OutputMode::Detached, dtag));
    CHECK(det->detached() && det->pid() > 0 && det->result().state == GroupState::Detached);
    Value hdet;
    CHECK(create_detached_handle(t, d, det, hdet) == HandleStatus::Ok);
    handles::ContextId owner = 1;
    CHECK(t.owner_of(hdet, HandleKind::Process, owner) == HandleStatus::Ok && owner == handles::kDetached);
    CHECK(t.close_context(d).failures.empty());
    CHECK(tagged(dtag).size() == 1);
    runtime::ProcInfo info;
    CHECK(runtime::proc_read(det->pid(), info) && info.session != static_cast<long>(::getsid(0)) && info.pgrp != ::getpgrp());
    // cancel of a detached handle: a request only (detached_request), nothing supervised
    CHECK(cancel_process(t, hdet, handles::kAnyAccessor, anything) == HandleStatus::Ok && anything);
    CHECK(none_tagged(dtag));
    // a detached launch in a build: refused before launching ...
    const auto build = tb.open(handles::ContextKind::Build, "build");
    bool refused = false;
    CHECK(detach_refused_here(t, build, refused) == HandleStatus::Ok && refused);
    CHECK(detach_refused_here(t, tb.open(), refused) == HandleStatus::Ok && !refused);
    // ... and, should one get that far, cancelled at once when the detach is refused
    auto late = start_ok(req({"sleep", "30"}, OutputMode::Detached, dtag));
    Value hl;
    CHECK(create_detached_handle(t, build, late, hl) != HandleStatus::Ok);
    CHECK(none_tagged(dtag));
    t.shutdown();
    runtime::set_warning_sink(nullptr);
    CHECK(runtime::reaper_wait_idle(milliseconds(5000)));
    CHECK(none_tagged(tag));
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
    CHECK(no_zombies());
}

// RLIMIT_NOFILE bounds descriptor *numbers*: the smallest limit that leaves
// exactly `free_slots` unused numbers below it.
rlim_t limit_with_free_slots(int free_slots) {
    std::set<int> open_fds;
    DIR* dir = ::opendir("/proc/self/fd");
    while (dirent* e = dir ? ::readdir(dir) : nullptr) if (e->d_name[0] != '.') open_fds.insert(std::atoi(e->d_name));
    if (dir) { open_fds.erase(::dirfd(dir)); ::closedir(dir); }
    int limit = 0, free_below = 0;
    while (free_below < free_slots) { if (!open_fds.count(limit)) ++free_below; ++limit; }
    return static_cast<rlim_t>(limit);
}

void low_fd_limit() {
    const std::string tag = new_tag();
    struct rlimit old {};
    ::getrlimit(RLIMIT_NOFILE, &old);
    const int base = fd_count();
    struct rlimit low = old;
    low.rlim_cur = limit_with_free_slots(3);  // room for one pipe, not for a capture pair + report pipe
    ::setrlimit(RLIMIT_NOFILE, &low);
    auto r = run_ok(req({"true"}, OutputMode::PipeCapture, tag));
    ::setrlimit(RLIMIT_NOFILE, &old);
    CHECK(!r.launched && r.state == GroupState::LaunchFailed);
    CHECK(r.error == "cannot create pipe: Too many open files");
    CHECK(fd_count() == base);
    // a limit that fits: 300 launches, no descriptor leak
    low.rlim_cur = limit_with_free_slots(12);
    ::setrlimit(RLIMIT_NOFILE, &low);
    bool all = true;
    for (int i = 0; i < 300; ++i) {
        auto ok = run_ok(req({"true"}, kBlockingModes[i % 2], tag));
        all = all && ok.launched && ok.exit_code == 0;
    }
    ::setrlimit(RLIMIT_NOFILE, &old);
    CHECK(all);
    CHECK(fd_count() == base);
    CHECK(no_zombies());
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
    const std::string ptag = new_tag(), tag = new_tag(), dtag = new_tag();
    if (!runtime::enable_subreaper()) { std::printf("escapees_subreaper: SKIPPED (no PR_SET_CHILD_SUBREAPER)\n"); return; }
    CHECK(runtime::subreaper_enabled());
    // a pipe holder in another session does not hang a pipe-mode call (it may
    // also be terminated as a leftover if it had not left the group yet)
    const auto t0 = Clock::now();
    auto r = run_ok(req({"sh", "-c", "setsid sleep 30 & echo x"}, OutputMode::PipeCapture, ptag));
    CHECK(r.out == "x\n" && ms_since(t0) < 3000);
    // an escapee of a FileCapture run (setsid, its parent exits) is reparented to Nift
    auto g = start_ok(req({"sh", "-c", "setsid sleep 30 </dev/null >/dev/null 2>&1 &"}, OutputMode::FileCapture, tag));
    g->wait();
    CHECK(wait_until([&] {
        auto pids = tagged(tag);
        if (pids.size() != 1) return false;
        runtime::ProcInfo info;
        return runtime::proc_read(pids[0], info) && info.ppid == ::getpid() && info.session == pids[0];
    }, milliseconds(2000)));
    // group cleanup cannot reach it (it left the group) ...
    {
        Table tb;
        bool registered = true;
        end_with_residue(tb, g, false, registered);
        CHECK(!registered);  // it left the group: nothing of the call's group is left to clean up
    }
    CHECK(tagged(tag).size() == 1);
    // ... the root-context orphan sweep does; a detached process is spared
    auto det = start_ok(req({"sleep", "30"}, OutputMode::Detached, dtag));
    CHECK(wait_until([&] { return tagged(dtag).size() == 1; }, milliseconds(2000)));
    const std::size_t swept = runtime::sweep_orphans(milliseconds(300), milliseconds(2000));
    CHECK(swept >= 1 && swept <= 2);
    CHECK(none_tagged(tag));
    CHECK(none_tagged(ptag));
    CHECK(tagged(dtag).size() == 1);
    det->cancel(CancelScope::Pipeline, CancelReason::Explicit, milliseconds(300));
    runtime::reaper_adopt(det);
    CHECK(none_tagged(dtag));
    CHECK(runtime::reaper_wait_idle(milliseconds(5000)));  // the detached pid was reaped (ours under the subreaper)
    CHECK(no_zombies());
}

// Regression (stress finding 2): under the subreaper, an adopted orphan that
// exits is reaped at once (SIGCHLD wakes the host loop), while a child of
// other code in Nift's own session and group is never taken from its owner.
void subreaper_reaps_orphans() {
    if (!runtime::enable_subreaper()) { std::printf("subreaper_reaps_orphans: SKIPPED\n"); return; }
    const std::string tag = new_tag();
    auto g = start_ok(req({"sh", "-c", "sleep 0.3 &"}, OutputMode::FileCapture, tag));
    g->wait();
    long orphan = -1;
    CHECK(wait_until([&] {
        auto pids = tagged(tag);
        if (pids.size() != 1) return false;
        runtime::ProcInfo info;
        if (!runtime::proc_read(pids[0], info) || info.ppid != ::getpid()) return false;
        orphan = pids[0];
        return true;
    }, milliseconds(1000)));  // adopted by Nift
    // no Host call from here on: only the host loop can reap it
    CHECK(none_tagged(tag, milliseconds(2000)));
    CHECK(wait_until([&] {
        runtime::ProcInfo info;
        return !runtime::proc_read(orphan, info);  // gone from /proc = reaped (a zombie would still be listed)
    }, milliseconds(1000)));
    CHECK(no_zombies());
    // a foreign child (same session, Nift's own group): left for its owner
    const pid_t foreign = ::fork();
    if (foreign == 0) ::_exit(7);
    std::this_thread::sleep_for(milliseconds(300));  // SIGCHLD has woken the loop by now
    int status = 0;
    CHECK(::waitpid(foreign, &status, 0) == foreign && WIFEXITED(status) && WEXITSTATUS(status) == 7);
}

// ---- D-034 ----
ProcessResult run_opts(OptionObject opts, std::vector<std::string> argv, const std::string& tag, std::uint64_t limit = 1u << 30) {
    RunOptions o;
    HostError e;
    CHECK(parse_run_options(opts, "run", o, e) == HostStatus::Ok);
    RunRequestContext ctx;
    ctx.read_limit = limit;
    SpawnRequest r;
    HostCallEnv env;
    CHECK(build_spawn_request(env, o, argv, ctx, r, e) == HostStatus::Ok);
    r.stages[0].env.push_back({"NIFT_TEST_PROC_TAG", tag});
    if (!o.kill_after) r.kill_after = milliseconds(300);  // keep the suite fast; the 2,000 ms default has its own test
    return run_ok(r);
}

OptionValue text(std::string t) { return OptionValue::object({{"text", OptionValue::of_string(std::move(t))}}); }

void pipe_mode_suite() {
    const std::string tag = new_tag();
    const OptionValue pipe = OptionValue::of_string("pipe");
    // large stdout and large stderr at the same time
    const char* both = "python3 -c \"import os\na=b'a'*65536; b=b'b'*65536\nfor i in range(256):\n  os.write(1,a); os.write(2,b)\n\"";
    auto r = run_opts({{"mode", pipe}}, {"sh", "-c", both}, tag);
    CHECK(r.state == GroupState::Exited && r.out.size() == 16u << 20 && r.err.size() == 16u << 20);
    // a descendant holding the pipe: managed cleanup, prompt return
    auto t0 = Clock::now();
    r = run_opts({{"mode", pipe}}, {"sh", "-c", "(sleep 30 &); echo x"}, tag);
    CHECK(r.out == "x\n" && r.state == GroupState::Exited && ms_since(t0) < 2000);
    CHECK(none_tagged(tag));
    // distinct outcomes: launch failure, nonzero exit, timeout, limit
    r = run_opts({{"mode", pipe}}, {"missing-cp16"}, tag);
    CHECK(r.state == GroupState::LaunchFailed && !r.launched && r.exit_code == 127);
    r = run_opts({{"mode", pipe}}, {"sh", "-c", "exit 4"}, tag);
    CHECK(r.state == GroupState::Exited && r.launched && r.exit_code == 4);
    r = run_opts({{"mode", pipe}, {"timeout_ms", OptionValue::of_int(200)}}, {"sleep", "30"}, tag);
    CHECK(r.state == GroupState::TimedOut && r.timed_out && !r.cancelled);
    r = run_opts({{"mode", pipe}}, {"sh", "-c", "head -c 3000000 /dev/zero; sleep 30"}, tag, 1u << 20);
    CHECK(r.state == GroupState::LimitExceeded && r.capture_limit_exceeded == "stdout" && !r.timed_out && !r.cancelled);
    // capture keeps file capture: seekable stdout
    r = run_opts({{"mode", OptionValue::of_string("capture")}}, {"sh", "-c", "test -f /dev/stdout && echo file"}, tag);
    CHECK(r.out == "file\n");
    r = run_opts({{"mode", pipe}}, {"sh", "-c", "test -p /dev/stdout && echo pipe"}, tag);
    CHECK(r.out == "pipe\n");
    // cancellation from another thread while the owner is blocked in wait()
    auto req0 = req({"sh", "-c", "echo started; sleep 30"}, OutputMode::PipeCapture, tag);
    auto g = start_ok(req0);
    std::thread canceller([&] { std::this_thread::sleep_for(milliseconds(200)); g->cancel(); });
    t0 = Clock::now();
    const auto& rc = g->wait();
    canceller.join();
    CHECK(rc.state == GroupState::Cancelled && rc.cancelled && rc.out == "started\n" && ms_since(t0) < 2000);
    // cleanup at context end (pipe mode handle still running)
    std::vector<std::string> warnings;
    runtime::set_warning_sink([&](const std::string& w) { warnings.push_back(w); });
    g = start_ok(req({"sleep", "30"}, OutputMode::PipeCapture, tag));
    {
        Table tb;
        CHECK(end_with_handle(tb, g, false).failures.empty());
    }
    CHECK(warnings.size() == 1 && g->finished() && g->result().state == GroupState::Cancelled);
    runtime::set_warning_sink(nullptr);
    CHECK(none_tagged(tag));
}

void stdin_suite() {
    const std::string tag = new_tag();
    const OptionValue pipe = OptionValue::of_string("pipe");
    // empty input
    auto r = run_opts({{"mode", pipe}, {"stdin", text("")}}, {"wc", "-c"}, tag);
    CHECK(std::strtol(r.out.c_str(), nullptr, 10) == 0 && r.stdin_complete && r.stdin_total == 0 && r.stdin_error.empty());
    // never a shell: shell syntax arrives literally
    r = run_opts({{"mode", pipe}, {"stdin", text("$(echo hi) `id` ; | *\n")}}, {"cat"}, tag);
    CHECK(r.out == "$(echo hi) `id` ; | *\n");
    // large input fed while large output is drained (cat echoes 8 MiB back): no deadlock
    std::string big(8u << 20, 'q');
    for (std::size_t i = 0; i < big.size(); i += 4096) big[i] = '\n';
    for (const char* mode : {"pipe", "capture"}) {
        r = run_opts({{"mode", OptionValue::of_string(mode)}, {"stdin", text(big)}, {"timeout_ms", OptionValue::of_int(60000)}}, {"cat"}, tag);
        CHECK(!r.timed_out && r.out == big && r.stdin_complete && r.stdin_written == big.size());
    }
    // early exit: the process result is kept; delivery is incomplete, not an error
    r = run_opts({{"mode", pipe}, {"stdin", text(big)}}, {"head", "-c", "10"}, tag);
    CHECK(r.state == GroupState::Exited && r.exit_code == 0 && r.out.size() == 10);
    CHECK(!r.stdin_complete && r.stdin_written < big.size() && r.stdin_error.empty());
    r = run_opts({{"mode", pipe}, {"stdin", text(big)}}, {"sh", "-c", "exit 3"}, tag);
    CHECK(r.exit_code == 3 && !r.stdin_complete && r.stdin_error.empty());
    // stdin "null" and inherit (default) unchanged
    r = run_opts({{"mode", pipe}, {"stdin", OptionValue::of_string("null")}}, {"wc", "-c"}, tag);
    CHECK(std::strtol(r.out.c_str(), nullptr, 10) == 0);
    // deadline while feeding a child that never reads
    auto t0 = Clock::now();
    r = run_opts({{"mode", pipe}, {"stdin", text(big)}, {"timeout_ms", OptionValue::of_int(200)}}, {"sleep", "30"}, tag);
    CHECK(r.state == GroupState::TimedOut && !r.stdin_complete && r.stdin_error.empty() && ms_since(t0) < 2000);
    // cancellation while feeding
    auto fr = req({"sleep", "30"}, OutputMode::PipeCapture, tag);
    fr.stdin_mode = StdinMode::Text;
    fr.stdin_text = big;
    auto g = start_ok(fr);
    std::this_thread::sleep_for(milliseconds(100));
    CHECK(g->cancel());
    const auto& rc = g->wait();
    CHECK(rc.state == GroupState::Cancelled && !rc.stdin_complete && rc.stdin_error.empty());
    // {"file": path}
    char path[] = "/tmp/cp16-stdin-file-XXXXXX";
    const int fd = ::mkstemp(path);
    CHECK(::write(fd, "from file\n", 10) == 10);
    ::close(fd);
    r = run_opts({{"mode", pipe}, {"stdin", OptionValue::object({{"file", OptionValue::of_string(path)}})}}, {"cat"}, tag);
    CHECK(r.out == "from file\n" && r.stdin_complete);
    std::remove(path);
    CHECK(none_tagged(tag));
    CHECK(no_zombies());
}

void deadline_suite() {
    const std::string tag = new_tag();
    // idle handle whose child ignores SIGTERM: enforced with nobody using the handle
    auto r0 = req({"sh", "-c", "trap '' TERM; while :; do sleep 0.05; done"}, OutputMode::Interactive, tag);
    r0.timeout = milliseconds(200);
    r0.kill_after = milliseconds(300);
    auto t0 = Clock::now();
    auto idle = start_ok(r0);
    CHECK(wait_until([&] { return idle->finished(); }, milliseconds(3000)));  // no call that pumps it
    const double ms = ms_since(t0);
    if (!(ms >= 450 && ms < 2000)) std::printf("idle deadline: %.0f ms\n", ms);
    CHECK(ms >= 450 && ms < 2000);
    CHECK(idle->result().state == GroupState::TimedOut && idle->result().stages[0].signal == SIGKILL);
    CHECK(none_tagged(tag, milliseconds(200)));
    CHECK(no_zombies());
    // several concurrent deadlines on idle handles
    std::vector<ProcessGroupPtr> gs;
    for (int ms_i : {100, 250, 1200, 1300}) {
        auto r = req({"sleep", "30"}, OutputMode::Interactive, tag);
        r.timeout = milliseconds(ms_i);
        gs.push_back(start_ok(r));
    }
    std::this_thread::sleep_for(milliseconds(700));
    CHECK(gs[0]->finished() && gs[1]->finished() && !gs[2]->finished() && !gs[3]->finished());
    CHECK(wait_until([&] { return gs[2]->finished() && gs[3]->finished(); }, milliseconds(3000)));
    for (auto& g : gs) CHECK(g->result().state == GroupState::TimedOut && g->result().stages[0].signal == SIGTERM);
    // expiry while the owner is blocked reading
    auto br = req({"sleep", "30"}, OutputMode::Interactive, tag);
    br.timeout = milliseconds(200);
    auto g = start_ok(br);
    t0 = Clock::now();
    CHECK(!g->read_line());
    CHECK(ms_since(t0) < 2000 && g->wait().timed_out);
    // the expiry is recorded even if the child then exits by itself during the grace period
    auto ex = req({"sh", "-c", "trap 'exit 0' TERM; while :; do sleep 0.05; done"}, OutputMode::Interactive, tag);
    ex.timeout = milliseconds(150);
    g = start_ok(ex);
    const auto& rr = g->wait();
    CHECK(rr.state == GroupState::TimedOut && rr.timed_out && rr.exit_code == 0 && rr.stages[0].state == StageState::Exited);
    // the host loop has nothing left armed once every group settled
    CHECK(wait_until([] { return runtime::loop_pending() == 0; }, milliseconds(1000)));
    CHECK(none_tagged(tag));
}

void deadline_races() {
    const std::string tag = new_tag();
    std::vector<std::string> warnings;
    runtime::set_warning_sink([&](const std::string& w) { warnings.push_back(w); });
    int ok = 0;
    for (int i = 0; i < 30; ++i) {
        // deadline, own exit, explicit cancel and context cleanup all at about the same time
        auto r = req({"sh", "-c", i % 2 ? "sleep 0.03" : "sleep 30"}, OutputMode::Interactive, tag);
        r.timeout = milliseconds(30);
        r.kill_after = milliseconds(100);
        auto g = start_ok(r);
        std::this_thread::sleep_for(std::chrono::microseconds(25000 + (i % 7) * 1000));
        std::thread a([&] { g->cancel(); });
        {
            Table tb;
            const auto report = end_with_handle(tb, g, false);
            if (!report.failures.empty()) CHECK(false);
        }
        a.join();
        if (g->finished() && g->settled()) ++ok;
    }
    CHECK(ok == 30);
    CHECK(wait_until([] { return zombie_children() == 0; }, milliseconds(1000)));
    CHECK(none_tagged(tag));
    runtime::set_warning_sink(nullptr);
}

void shutdown_refuses_launches() {
    runtime::request_shutdown_for_test(SIGTERM);
    HostStatus st;
    HostError e;
    CHECK(!ProcessGroup::start(req({"true"}, OutputMode::PipeCapture, new_tag()), st, e) && e.code == "host.shutting_down");
    CHECK(e.message == "run: Nift is shutting down (signal received); no new process is started");
    runtime::clear_shutdown_for_test();
}

void cleanup_failures_reported() {
    // A process that cannot be terminated (SIGKILL suppressed by the test
    // hook): the routine warning follows the context's outcome; the genuine
    // failure is decided and reported by the CP17 table (CloseReport), and it
    // forces a nonzero exit (D-035), in both cases.
    const std::string tag = new_tag();
    std::vector<std::string> warnings;
    runtime::set_warning_sink([&](const std::string& w) { warnings.push_back(w); });
    runtime::test_hooks().suppress_sigkill = true;
    for (bool failed : {true, false}) {
        warnings.clear();
        auto r = req({"sh", "-c", "trap '' TERM; while :; do sleep 0.05; done"}, OutputMode::Interactive, tag);
        r.kill_after = milliseconds(150);
        r.post_kill_wait = milliseconds(200);
        auto g = start_ok(r);
        std::this_thread::sleep_for(milliseconds(150));  // let sh install its trap first
        Table tb;
        const auto report = end_with_handle(tb, g, failed, failed ? "page 'about'" : "script");
        CHECK(warnings.size() == (failed ? 0u : 1u));
        CHECK(report.on_error == failed);
        CHECK(report.failures.size() == 1);
        if (!report.failures.empty()) {
            const auto& f = report.failures[0];
            CHECK(f.state == handles::LifeState::Lost && f.code == "process.lost");
            CHECK(f.message == "process 'sh' (pid " + std::to_string(g->pid()) + ") did not exit after SIGKILL; left to the reaper");
            CHECK(f.context_label == (failed ? "page 'about'" : "script"));
        }
        CHECK(report.final_exit_status(0) == 1);    // D-035: a cleanup failure forces a nonzero exit
        CHECK(report.final_exit_status(143) == 143);  // an earlier error / signal status stays primary
        CHECK(g->result().state == GroupState::Lost);
        kill_tagged(tag);
    }
    // members alive after SIGKILL (leftovers of a finished run): Failed, alive_after_kill
    {
        auto lr = req({"sh", "-c", "(trap '' TERM; while :; do sleep 0.05; done) & sleep 0.15; echo x"}, OutputMode::FileCapture, tag);
        lr.kill_after = milliseconds(150);
        lr.post_kill_wait = milliseconds(200);
        auto g = start_ok(lr);
        g->wait();
        Table tb;
        bool registered = false;
        const auto report = end_with_residue(tb, g, false, registered);
        if (!(registered && report.failures.size() == 1)) {
            std::printf("residue: registered %d failures %zu cleaned %zu\n", registered, report.failures.size(), report.cleaned);
            for (const auto& f : report.failures) std::printf("  failure state %s code %s msg %s\n", handles::life_state_name(f.state), f.code.c_str(), f.message.c_str());
            dump_events(Clock::now() - milliseconds(3000));
        }
        CHECK(registered && report.failures.size() == 1);
        if (!report.failures.empty())
            CHECK(report.failures[0].state == handles::LifeState::Failed && report.failures[0].code == "process.alive_after_kill");
        kill_tagged(tag);
    }
    // an error-ended context whose processes terminate normally: silent, no failure
    runtime::test_hooks().suppress_sigkill = false;
    warnings.clear();
    auto g = start_ok(req({"sleep", "30"}, OutputMode::Interactive, tag));
    {
        Table tb;
        const auto report = end_with_handle(tb, g, true, "page 'about'");
        CHECK(report.failures.empty() && report.final_exit_status(0) == 0);
    }
    CHECK(warnings.empty() && g->result().state == GroupState::Cancelled);
    runtime::set_warning_sink(nullptr);
    CHECK(runtime::reaper_wait_idle(milliseconds(3000)));
    CHECK(none_tagged(tag));
}

} // namespace

int main(int argc, char** argv) {
    kSelf = argv[0];
    runtime::test_hooks().record = true;  // diagnostics: event trace (printed on failure)

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
        {"handle_lifecycle", handle_lifecycle},
        {"detached_rules", detached_rules},
        {"low_fd_limit", low_fd_limit},
        {"trace_spawn", trace_spawn},
        {"pipe_mode_suite", pipe_mode_suite},
        {"stdin_suite", stdin_suite},
        {"deadline_suite", deadline_suite},
        {"deadline_races", deadline_races},
        {"shutdown_refuses_launches", shutdown_refuses_launches},
        {"cleanup_failures_reported", cleanup_failures_reported},
        {"sigterm_to_nift_c2", sigterm_to_nift_c2},
        {"sigkill_pdeathsig_linux", sigkill_pdeathsig_linux},
        {"escapees_subreaper", escapees_subreaper},  // enables the subreaper for the rest of the process
        {"subreaper_reaps_orphans", subreaper_reaps_orphans},
    };
    // Stress: CP16_REPEAT=N runs the selection N times (the signal log is reset per round).
    const char* repeat_env = std::getenv("CP16_REPEAT");
    const int rounds = repeat_env ? std::max(1, std::atoi(repeat_env)) : 1;
    for (int round = 0; round < rounds; ++round) {
    bool first = true;
    for (const auto& t : tests) {
        if (!only.empty() && only != t.name) continue;
        if (rounds > 1 && first) {
            auto& h = runtime::test_hooks();
            std::lock_guard<std::mutex> lock(h.log_mutex);
            h.log.clear();
            std::printf("--- round %d\n", round + 1);
        }
        first = false;
        const long before = rt_test::failures();
        const auto t0 = Clock::now();
        t.fn();
        std::printf("%-28s %s (%.0f ms)\n", t.name, rt_test::failures() == before ? "ok" : "FAILED", ms_since(t0));
        std::fflush(stdout);
    }
    }
    return rt_test::finish("host-process-unit");
}
