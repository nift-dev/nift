#pragma once
#include <string>
#include <vector>
#include <map>
#include <filesystem>

struct ProcessSpec {
    std::string program;
    std::vector<std::string> args;
    std::filesystem::path cwd;
    std::map<std::string,std::string> env;
    std::string stdin_path, stdout_path, stderr_path;
    bool append_stdout=false, append_stderr=false, merge_stderr=false;
    // Interactive foreground execution: the child inherits the shell's
    // terminal descriptors directly (no capture pipes) and becomes the
    // controlling-terminal foreground process group, so terminal-aware
    // programs (fastfetch, top, less, editors) behave as if launched from
    // Bash. Used only for simple foreground commands in the Nift shell; run()/
    // cmd() structured capture never sets it.
    bool foreground_terminal=false;
};
// launched: every stage started executing. On POSIX an exec failure in the
// child (missing or non-executable program) gives launched=false, error
// "executable not found: <name>" / "permission denied: <name>" and the shell
// exit status 127 / 126; exec_failed marks that case (feature-site-dx).
// One entry per pipeline stage, in order (feature-site-dx review R6):
// launched=false with `error` when the stage could not be started (setup or
// exec failure, or a later stage never started after an earlier failure);
// launched=true with that stage's own exit status otherwise.
struct ProcessStageResult { bool launched=false; int exit_code=-1; std::string error; };
// exit_code keeps the shell convention: the LAST stage's status (127/126 for
// an exec failure of that stage). For a single command, launched=false and
// `error` report an exec/setup failure. For a pipeline the top-level
// launched/error keep their pre-feature meaning (stages were started /
// parent-side setup failure); `stages` carries each stage's launch result.
struct ProcessResult { int exit_code=-1; std::string out, err; bool launched=false; std::string error; bool exec_failed=false; std::vector<ProcessStageResult> stages; };
ProcessResult nift_run_process(const ProcessSpec& spec, bool capture=true, bool stream=false);
ProcessResult nift_run_pipeline(const std::vector<ProcessSpec>& specs, bool capture=true, bool stream=false);
bool nift_find_executable(const std::string& name, std::string& path);

enum class ShellJobState { Running, Stopped, Done };
struct ShellJobInfo {
    int id = 0;
    long long process_group = -1;
    std::vector<long long> process_ids;
    std::string command;
    ShellJobState state = ShellJobState::Running;
    int exit_code = -1;
};

// Interactive-shell job ownership. Structured run()/cmd().run() continues to
// use nift_run_process/pipeline and is intentionally separate from this table.
class ShellJobTable {
public:
    ShellJobTable();
    ~ShellJobTable();
    ShellJobTable(const ShellJobTable&) = delete;
    ShellJobTable& operator=(const ShellJobTable&) = delete;

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
    Impl* impl_ = nullptr;
};
