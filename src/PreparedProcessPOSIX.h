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
    [[noreturn]] void exec() {
        bool denied=false;
        for(const auto& candidate:candidates) {
            execve(candidate.c_str(),argv.data(),envp.data());
            int error=errno;
            if(error==ENOEXEC){shell_argv[1]=const_cast<char*>(candidate.c_str());execve("/bin/sh",shell_argv.data(),envp.data());_exit(126);}
            if(error==EACCES){denied=true;continue;}
            if(error!=ENOENT && error!=ENOTDIR)_exit(126);
        }
        _exit(denied?126:127);
    }
};
#endif
