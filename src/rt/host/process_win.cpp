// CP16 Host layer: Windows process model. *** UNTESTED: not compiled or run
// in CP16 (no Windows toolchain on the development host; D-004 CI only). ***
//
// What is here: the job-object building blocks §11.4/§11.9 rely on (one job
// per managed pipeline with KILL_ON_JOB_CLOSE and no breakaway; cooperative
// CTRL_BREAK to the console process group, then TerminateJobObject; a bounded
// wait for the job to empty). What is not here yet: the ProcessGroup event
// loop on Windows (overlapped named pipes or drain threads), so every
// ProcessGroup entry point reports `process.unsupported` until the Windows
// slice is implemented and certified in CI.
#ifdef _WIN32
#include "rt/host/process.h"

#include <windows.h>

namespace nift::rt::host {

namespace win {

// One job per managed pipeline. Children are created suspended
// (CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP), assigned, then resumed, so no
// descendant can start outside the job.
HANDLE create_pipeline_job() {
    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
    if (!job) return nullptr;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    // KILL_ON_JOB_CLOSE: if Nift dies, the job handle closes and every member
    // is killed (§11.9 row "managed processes die when Nift is killed").
    // Breakaway is NOT allowed (no JOB_OBJECT_LIMIT_BREAKAWAY_OK).
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits)) {
        ::CloseHandle(job);
        return nullptr;
    }
    return job;
}

bool assign_and_resume(HANDLE job, const PROCESS_INFORMATION& pi) {
    if (!::AssignProcessToJobObject(job, pi.hProcess)) return false;
    return ::ResumeThread(pi.hThread) != static_cast<DWORD>(-1);
}

DWORD active_processes(HANDLE job) {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
    if (!::QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &info, sizeof info, nullptr)) return 0;
    return info.ActiveProcesses;
}

// §11.4 Pipeline scope: CTRL_BREAK to the console process group of the first
// stage (console programs only), grace, TerminateJobObject, bounded wait.
// Returns false when members remained after the bound (reported as Lost).
bool cancel_job(HANDLE job, DWORD first_stage_pid, DWORD grace_ms, DWORD bound_ms) {
    if (first_stage_pid) ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, first_stage_pid);
    const ULONGLONG grace_end = ::GetTickCount64() + grace_ms;
    while (active_processes(job) > 0 && ::GetTickCount64() < grace_end) ::Sleep(10);
    if (active_processes(job) > 0) ::TerminateJobObject(job, 1);
    const ULONGLONG bound_end = ::GetTickCount64() + bound_ms;
    while (active_processes(job) > 0 && ::GetTickCount64() < bound_end) ::Sleep(10);
    return active_processes(job) == 0;
}

// Detached: CREATE_BREAKAWAY_FROM_JOB | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
// if the environment's job forbids breakaway, the caller warns that the
// process will end with Nift's job (§11.9).
constexpr DWORD kDetachedFlags = CREATE_BREAKAWAY_FROM_JOB | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT;
constexpr DWORD kManagedFlags = CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT;

} // namespace win

struct ProcessGroup::State {
    SpawnRequest req;
    ProcessResult result;
};

std::vector<std::string> SpawnStage::argv() const {
    std::vector<std::string> out;
    out.push_back(program);
    out.insert(out.end(), args.begin(), args.end());
    return out;
}

ProcessGroup::ProcessGroup(std::unique_ptr<State> state) : s_(std::move(state)) {}
ProcessGroup::~ProcessGroup() = default;

const char* output_mode_name(OutputMode mode) {
    switch (mode) {
        case OutputMode::FileCapture: return "file-capture";
        case OutputMode::PipeCapture: return "pipe-capture";
        case OutputMode::Stream: return "stream";
        case OutputMode::Inherit: return "inherit";
        case OutputMode::Interactive: return "interactive";
        case OutputMode::Detached: return "detached";
    }
    return "?";
}

std::shared_ptr<ProcessGroup> ProcessGroup::start(SpawnRequest req, HostStatus& status, HostError& error) {
    status = validate(req, error);
    if (status != HostStatus::Ok) return nullptr;
    status = make_error(error, "process.unsupported", req.builtin + ": the Host process layer is not implemented on Windows yet");
    return nullptr;
}
ProcessResult ProcessGroup::run(SpawnRequest req, HostStatus& status, HostError& error) {
    start(std::move(req), status, error);
    return ProcessResult();
}
bool ProcessGroup::pump(std::optional<Clock::time_point>) { return true; }
const ProcessResult& ProcessGroup::wait() { return s_->result; }
bool ProcessGroup::cancel(CancelScope, CancelReason, std::optional<Millis>) { return false; }
HostStatus ProcessGroup::write(std::string_view, HostError& error) { return make_error(error, "process.unsupported", "write: unsupported"); }
HostStatus ProcessGroup::close_stdin(HostError& error) { return make_error(error, "process.unsupported", "close_stdin: unsupported"); }
std::optional<std::string> ProcessGroup::read_line() { return std::nullopt; }
std::optional<std::string> ProcessGroup::read(std::size_t) { return std::nullopt; }
bool ProcessGroup::finished() const { return true; }
bool ProcessGroup::running() { return false; }
bool ProcessGroup::detached() const { return false; }
GroupState ProcessGroup::state() const { return s_->result.state; }
long ProcessGroup::pid() const { return -1; }
long ProcessGroup::pgid() const { return -1; }
std::string ProcessGroup::argv0() const { return s_->req.stages.empty() ? std::string() : s_->req.stages.front().program; }
const SpawnRequest& ProcessGroup::request() const { return s_->req; }
const ProcessResult& ProcessGroup::result() const { return s_->result; }
std::vector<StageResult> ProcessGroup::stages() const { return s_->result.stages; }
ProcessGroup::TerminateOutcome ProcessGroup::begin_terminate(CancelReason) { return TerminateOutcome(); }
ProcessGroup::TerminateOutcome ProcessGroup::terminate(CancelReason) { return TerminateOutcome(); }
bool ProcessGroup::has_live_members() const { return false; }
std::string signal_name(int signo) { return "SIG" + std::to_string(signo); }

} // namespace nift::rt::host
#endif  // _WIN32
