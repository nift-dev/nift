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
    // Bash. Used only for simple foreground commands in `nift sh`; run()/
    // cmd() structured capture never sets it.
    bool foreground_terminal=false;
};
struct ProcessResult { int exit_code=-1; std::string out, err; bool launched=false; std::string error; };
ProcessResult nift_run_process(const ProcessSpec& spec, bool capture=true, bool stream=false);
ProcessResult nift_run_pipeline(const std::vector<ProcessSpec>& specs, bool capture=true, bool stream=false);
bool nift_find_executable(const std::string& name, std::string& path);
