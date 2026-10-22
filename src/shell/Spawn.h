#pragma once
// CP20: route the interactive shell's external commands through the CP16 Host
// process layer. The shell keeps ProcessSpec as its parsed intermediate type;
// this module owns the conversion, foreground execution and background groups.

#include "Proc.h"

#include <memory>
#include <string>
#include <vector>

namespace nift::shell {

// True when the Host process layer can launch on this platform. On Windows
// the Host layer is still unsupported; the shell keeps the legacy launcher
// there until the Windows ProcessGroup slice is certified.
bool host_shell_supported();

// Foreground run, including pipelines and redirections. Returns the legacy
// ProcessResult shape the shell already consumes.
ProcessResult run_pipeline(const std::vector<ProcessSpec>& specs, const std::string& command);

class JobTable {
public:
    JobTable();
    ~JobTable();
    JobTable(const JobTable&) = delete;
    JobTable& operator=(const JobTable&) = delete;

    bool supported() const;
    ProcessResult launch(const std::vector<ProcessSpec>& specs, const std::string& command, bool foreground);
    void reap();
    std::vector<ShellJobInfo> jobs(bool include_done = true);
    ProcessResult foreground(int job_id);
    bool background(int job_id, std::string& error);
    ProcessResult wait(int job_id);
    ProcessResult wait_all();
    bool has_live_jobs();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nift::shell
