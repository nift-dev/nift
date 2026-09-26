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
struct ProcessResult { int exit_code=-1; std::string out, err; bool launched=false; std::string error; };
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
