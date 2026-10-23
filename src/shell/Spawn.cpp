#include "shell/Spawn.h"

#include "rt/host/host_error.h"
#include "rt/host/process.h"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace nift::shell {

namespace {

using nift::rt::host::HostError;
using nift::rt::host::HostStatus;
using nift::rt::host::OutputMode;
using nift::rt::host::ProcessGroup;
using nift::rt::host::SpawnRequest;
using nift::rt::host::SpawnStage;

[[maybe_unused]] SpawnStage convert_stage(const ProcessSpec& spec) {
    SpawnStage stage;
    stage.program = spec.program;
    stage.args = spec.args;
    stage.cwd = spec.cwd.string();
    for (const auto& entry : spec.env) stage.env.push_back(entry);
    stage.stdin_path = spec.stdin_path;
    stage.stdout_path = spec.stdout_path;
#if defined(NIFT_CP20_MUTANT) && NIFT_CP20_MUTANT == 2
    stage.stdout_append = false;
#else
    stage.stdout_append = spec.append_stdout;
#endif
#if defined(NIFT_CP20_MUTANT) && NIFT_CP20_MUTANT == 12
    stage.stderr_path.clear();
#else
    stage.stderr_path = spec.stderr_path;
#endif
    stage.stderr_append = spec.append_stderr;
#if defined(NIFT_CP20_MUTANT) && NIFT_CP20_MUTANT == 3
    stage.stderr_to_stdout = false;
#else
    stage.stderr_to_stdout = spec.merge_stderr;
#endif
#if defined(NIFT_CP20_MUTANT) && NIFT_CP20_MUTANT == 4
    for (const auto& entry : spec.env) {
#ifdef _WIN32
        _putenv_s(entry.first.c_str(), entry.second.c_str());
#else
        ::setenv(entry.first.c_str(), entry.second.c_str(), 1);
#endif
    }
#endif
    return stage;
}

[[maybe_unused]] ProcessResult convert_result(const nift::rt::host::ProcessResult& host) {
    ProcessResult out;
    out.exit_code = host.exit_code;
    out.out = host.out;
    out.err = host.err;
    out.launched = host.launched;
    out.error = host.error;
    out.stages.reserve(host.stages.size());
    for (const auto& stage : host.stages) {
        ProcessStageResult item;
        item.launched = stage.launched;
        item.exit_code = stage.exit_code;
        item.error = stage.error;
        out.stages.push_back(std::move(item));
    }
#if defined(NIFT_CP20_MUTANT) && NIFT_CP20_MUTANT == 6
    for (const auto& stage : out.stages) {
        if (stage.launched && stage.exit_code != 0) { out.exit_code = stage.exit_code; break; }
    }
#endif
    return out;
}

[[maybe_unused]] ProcessResult failure(const HostError& error, HostStatus status) {
    ProcessResult out;
    out.error = error.message;
    out.exit_code = status == HostStatus::Limit ? 1 : -1;
    return out;
}

[[maybe_unused]] ProcessResult run_host(const std::vector<ProcessSpec>& specs, const std::string& command) {
    ProcessResult out;
    if (specs.empty()) {
        out.error = "empty pipeline";
        return out;
    }
    SpawnRequest request;
    request.builtin = command.empty() ? "shell" : command;
    request.mode = OutputMode::Inherit;
    request.exec_report_optional = true;  // CP07 low-fd shell contract
    // Legacy shell semantics: only a simple foreground command gets its own
    // terminal/process group. Pipeline stages stay in Nift's group, so a
    // SIGINT to that group still reaches every stage (CP07/CP20 contract).
    request.own_group = specs.size() == 1 && specs[0].foreground_terminal;
#if defined(NIFT_CP20_MUTANT) && NIFT_CP20_MUTANT == 1
    if (specs.size() > 1) request.mode = OutputMode::FileCapture;
#endif
    request.stages.reserve(specs.size());
    for (const auto& spec : specs) request.stages.push_back(convert_stage(spec));
    HostStatus status = HostStatus::Ok;
    HostError error;
    auto group = ProcessGroup::start_for_wait(std::move(request), status, error);
    if (status != HostStatus::Ok || !group) return failure(error, status);
    out = convert_result(group->wait());
#if defined(NIFT_CP20_MUTANT) && NIFT_CP20_MUTANT == 1
    if (specs.size() > 1) {
        if (!out.out.empty()) std::cout << out.out;
        if (!out.err.empty()) std::cerr << out.err;
        std::cout.flush();
        std::cerr.flush();
    }
#endif
    return out;
}

} // namespace

bool host_shell_supported() {
#ifdef _WIN32
    return false;
#else
    return true;
#endif
}

ProcessResult run_pipeline(const std::vector<ProcessSpec>& specs, const std::string& command, bool direct) {
#ifdef _WIN32
    (void)command;
    return nift_run_pipeline(specs, !direct, !direct);
#else
    (void)direct;
    return run_host(specs, command);
#endif
}

struct JobTable::Impl {
    struct Entry {
        int id = 0;
        std::shared_ptr<ProcessGroup> group;
        ShellJobInfo info;
    };
    int next_id = 1;
    std::vector<Entry> entries;

    Entry* find(int id) {
        for (Entry& entry : entries) if (entry.id == id) return &entry;
        return nullptr;
    }

    void refresh(Entry& entry) {
        if (!entry.group || entry.info.state == ShellJobState::Done) return;
        if (entry.group->finished()) {
            entry.info.state = ShellJobState::Done;
            entry.info.exit_code = entry.group->result().exit_code;
            entry.group.reset();  // release Host loop fds; job info remains for jobs/wait
#if defined(NIFT_CP20_MUTANT) && NIFT_CP20_MUTANT == 9
        } else {
            entry.info.state = ShellJobState::Running;
#else
        } else {
            entry.info.state = entry.group->has_stopped_members() ? ShellJobState::Stopped
                                                                   : ShellJobState::Running;
#endif
        }
    }
};

JobTable::JobTable() : impl_(std::make_unique<Impl>()) {}

JobTable::~JobTable() {
#ifndef _WIN32
    for (auto& entry : impl_->entries) {
        if (entry.group && !entry.group->finished()) {
            entry.group->terminate(nift::rt::host::CancelReason::Cleanup);
        }
    }
#endif
}

bool JobTable::supported() const { return host_shell_supported(); }

ProcessResult JobTable::launch(const std::vector<ProcessSpec>& specs, const std::string& command, bool foreground) {
#if defined(NIFT_CP20_MUTANT) && NIFT_CP20_MUTANT == 11
    // Deliberate negative control: deferred report fds are never closed.
    static std::vector<int> leaked_fds;
    int leak[2] = {-1, -1};
    if (::pipe(leak) == 0) { leaked_fds.push_back(leak[0]); leaked_fds.push_back(leak[1]); }
#endif
    if (!supported()) {
        if (!foreground) {
            ProcessResult out;
            out.error = "interactive job control is not supported on Windows";
            out.exit_code = 2;
            return out;
        }
        return run_pipeline(specs, command, true);
    }
#ifndef _WIN32
    if (specs.empty()) { ProcessResult out; out.error = "empty pipeline"; return out; }
    SpawnRequest request;
    request.builtin = command.empty() ? "shell" : command;
    request.mode = OutputMode::Inherit;
#if defined(NIFT_CP20_MUTANT) && NIFT_CP20_MUTANT == 1
    if (specs.size() > 1) request.mode = OutputMode::FileCapture;
#endif
    request.exec_report_optional = true;
    request.own_group = true;
    request.terminal = foreground ? nift::rt::host::TerminalPolicy::Auto
                                  : nift::rt::host::TerminalPolicy::Never;
    for (const auto& spec : specs) request.stages.push_back(convert_stage(spec));
    HostStatus status = HostStatus::Ok;
    HostError error;
#if defined(NIFT_CP20_MUTANT) && NIFT_CP20_MUTANT == 7
    auto group = ProcessGroup::start_for_wait(std::move(request), status, error);
#else
    auto group = ProcessGroup::start(std::move(request), status, error);
#endif
    if (status != HostStatus::Ok) return failure(error, status);
    Impl::Entry entry;
    entry.id = impl_->next_id++;
    entry.group = std::move(group);
    entry.info.id = entry.id;
    entry.info.process_group = entry.group->pgid();
    entry.info.command = command;
    for (const auto& stage : entry.group->stages()) entry.info.process_ids.push_back(stage.pid);
    entry.info.state = entry.group->finished() ? ShellJobState::Done : ShellJobState::Running;
    if (entry.info.state == ShellJobState::Done) entry.info.exit_code = entry.group->result().exit_code;
    ProcessResult out;
    out.launched = true;
    out.exit_code = 0;
    if (foreground) {
        const auto& result = entry.group->wait();
        entry.info.state = ShellJobState::Done;
        entry.info.exit_code = result.exit_code;
        out = convert_result(result);
        entry.group.reset();
#if defined(NIFT_CP20_MUTANT) && NIFT_CP20_MUTANT == 1
        if (specs.size() > 1) {
            if (!out.out.empty()) std::cout << out.out;
            if (!out.err.empty()) std::cerr << out.err;
            std::cout.flush();
            std::cerr.flush();
        }
#endif
    }
    impl_->entries.push_back(std::move(entry));
    return out;
#else
    ProcessResult out;
    out.error = "interactive job control is not supported on Windows";
    out.exit_code = 2;
    return out;
#endif
}

void JobTable::reap() {
    for (auto& entry : impl_->entries) impl_->refresh(entry);
}

std::vector<ShellJobInfo> JobTable::jobs(bool include_done) {
    reap();
    std::vector<ShellJobInfo> out;
    for (const auto& entry : impl_->entries)
        if (include_done || entry.info.state != ShellJobState::Done) out.push_back(entry.info);
    return out;
}

ProcessResult JobTable::foreground(int job_id) {
    ProcessResult out;
    auto* entry = impl_->find(job_id);
    if (!entry) { out.error = "unknown job"; return out; }
    if (!entry->group) {
        if (entry->info.state == ShellJobState::Done) {
            out.launched = true;
            out.exit_code = entry->info.exit_code;
            return out;
        }
        out.error = "unknown job";
        return out;
    }
#if defined(NIFT_CP20_MUTANT) && NIFT_CP20_MUTANT == 8
    if (entry->group->has_stopped_members()) {
        out.launched = true;
        out.exit_code = 130;
        return out;
    }
#else
    if (entry->group->pgid() > 0) ::kill(-static_cast<pid_t>(entry->group->pgid()), SIGCONT);
#endif
    const auto& result = entry->group->wait();
    entry->info.state = ShellJobState::Done;
    entry->info.exit_code = result.exit_code;
    out = convert_result(result);
    return out;
}

bool JobTable::background(int job_id, std::string& error) {
    auto* entry = impl_->find(job_id);
    if (!entry) { error = "unknown job"; return false; }
    if (!entry->group) { error = "job is already complete"; return false; }
    if (entry->group->finished()) { error = "job is already complete"; return false; }
#ifndef _WIN32
#if !defined(NIFT_CP20_MUTANT) || NIFT_CP20_MUTANT != 8
    if (entry->group->pgid() > 0) ::kill(-static_cast<pid_t>(entry->group->pgid()), SIGCONT);
#endif
#endif
    entry->info.state = ShellJobState::Running;
    return true;
}

ProcessResult JobTable::wait(int job_id) {
    ProcessResult out;
    auto* entry = impl_->find(job_id);
    if (!entry) { out.error = "unknown job"; return out; }
    if (!entry->group) {
        out.launched = true;
        out.exit_code = entry->info.exit_code;
        return out;
    }
#if defined(NIFT_CP20_MUTANT) && (NIFT_CP20_MUTANT == 5 || NIFT_CP20_MUTANT == 7)
    out.launched = true;
    out.exit_code = 0;
    return out;
#elif defined(NIFT_CP20_MUTANT) && NIFT_CP20_MUTANT == 8
    if (entry->group->has_stopped_members()) {
        out.launched = true;
        out.exit_code = 130;
        return out;
    }
#endif
    const auto& result = entry->group->wait();
    entry->info.state = ShellJobState::Done;
    entry->info.exit_code = result.exit_code;
    out = convert_result(result);
    entry->group.reset();
    return out;
}

ProcessResult JobTable::wait_all() {
    ProcessResult out;
    out.launched = true;
    out.exit_code = 0;
    for (auto& entry : impl_->entries) {
        if (!entry.group || entry.info.state == ShellJobState::Done) continue;
        ProcessResult one = wait(entry.id);
        if (!one.error.empty()) return one;
        out.exit_code = one.exit_code;
    }
    return out;
}

bool JobTable::has_live_jobs() {
    reap();
    for (const auto& entry : impl_->entries)
        if (entry.info.state != ShellJobState::Done) return true;
    return false;
}

} // namespace nift::shell
