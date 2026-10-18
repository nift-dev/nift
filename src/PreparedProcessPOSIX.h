#pragma once
#ifndef _WIN32
#include "Proc.h"
#include "ProcessEnvironment.h"
#include <cerrno>
#include <cstdlib>
#include <unistd.h>
extern char **environ;

// All storage and PATH expansion are prepared in the parent. After fork this
// object is read-only; exec() uses only execve and _exit, including ENOEXEC's
// historical /bin/sh fallback. Relative PATH entries resolve after child chdir.
struct PreparedProcessPOSIX {
    std::vector<std::string> arguments, environment, candidates;
    std::vector<char*> argv, envp, shell_argv;
    explicit PreparedProcessPOSIX(const ProcessSpec& spec) {
        arguments.push_back(spec.program);
        arguments.insert(arguments.end(),spec.args.begin(),spec.args.end());
        for(auto& a:arguments) argv.push_back(a.data());
        argv.push_back(nullptr);
        std::map<std::string,std::string> vars;
        {std::lock_guard<std::mutex> lock(nift_environment_mutex());
        for(char** p=environ; p && *p; ++p) {
            std::string entry=*p; auto eq=entry.find('=');
            if(eq!=std::string::npos)vars[entry.substr(0,eq)]=entry.substr(eq+1);
        }
        }
        for(const auto& kv:spec.env)vars[kv.first]=kv.second;
        for(const auto& kv:vars)environment.push_back(kv.first+"="+kv.second);
        for(auto& e:environment)envp.push_back(e.data());
        envp.push_back(nullptr);
        if(spec.program.find('/')!=std::string::npos)candidates.push_back(spec.program);
        else {
            auto it=vars.find("PATH");
            std::string path=it==vars.end()?"/bin:/usr/bin":it->second;
            size_t start=0;
            do {
                auto end=path.find(':',start);
                std::string dir=path.substr(start,end==std::string::npos?end:end-start);
                candidates.push_back(dir.empty()?spec.program:dir+"/"+spec.program);
                if(end==std::string::npos)break;
                start=end+1;
            } while(true);
        }
        shell_argv.push_back(const_cast<char*>("/bin/sh"));
        shell_argv.push_back(nullptr); // replaced with candidate in child
        for(size_t i=1;i<arguments.size();++i)shell_argv.push_back(arguments[i].data());
        shell_argv.push_back(nullptr);
    }
    // feature-site-dx: `report_fd` is the write end of a close-on-exec pipe.
    // A successful execve closes it (the parent reads EOF: launched); on
    // failure the child writes {kind, errno} with one async-signal-safe
    // write before _exit, so the parent can report launched:false and why.
    // Exit codes are unchanged (127 not found, 126 not executable).
    // Payload: {kind, errno, detail}. ExecInterpreter: ENOENT from execve of
    // a file that exists (its #! interpreter or ELF loader is missing);
    // detail is the candidate index. ExecSetup: chdir/redirection in the
    // child failed before exec; detail names the step (SetupStep).
    enum ExecFailure : int { ExecNotFound = 1, ExecDenied = 2, ExecOther = 3, ExecInterpreter = 4, ExecSetup = 5 };
    enum SetupStep : int { SetupChdir = 1, SetupStdin = 2, SetupStdout = 3, SetupStderr = 4, SetupRedirect = 5 };
    [[noreturn]] static void fail_exec(int report_fd, int kind, int error, int code, int detail = 0) {
        if(report_fd>=0){int payload[3]={kind,error,detail};ssize_t ignored=write(report_fd,payload,sizeof payload);(void)ignored;}
        _exit(code);
    }
    [[noreturn]] void exec(int report_fd=-1) {
        bool denied=false;int last=ENOENT;int interpreter=-1;
        for(std::size_t index=0;index<candidates.size();++index) {
            const auto& candidate=candidates[index];
            execve(candidate.c_str(),argv.data(),envp.data());
            int error=errno;
            if(error==ENOEXEC){shell_argv[1]=const_cast<char*>(candidate.c_str());execve("/bin/sh",shell_argv.data(),envp.data());fail_exec(report_fd,ExecOther,errno,126);}
            if(error==EACCES){denied=true;continue;}
            if(error==ENOENT&&interpreter<0&&access(candidate.c_str(),F_OK)==0)interpreter=static_cast<int>(index);
            if(error!=ENOENT && error!=ENOTDIR)fail_exec(report_fd,ExecOther,error,126);
            last=error;
        }
        if(denied)fail_exec(report_fd,ExecDenied,EACCES,126);
        if(interpreter>=0)fail_exec(report_fd,ExecInterpreter,ENOENT,127,interpreter);
        fail_exec(report_fd,ExecNotFound,last,127);
    }
};
#endif
