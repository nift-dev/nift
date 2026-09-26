#include "Proc.h"
#include <algorithm>
#include <cerrno>
#include <map>
#include <memory>
#include <set>
#include <utility>
#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif

struct ShellJobTable::Impl {
    int next_id = 1;
    struct Entry {
        ShellJobInfo info;
#ifndef _WIN32
        pid_t pgid = -1;
        std::vector<pid_t> pids;
        std::set<pid_t> remaining;
        pid_t last_pid = -1;
#endif
    };
    std::map<int, Entry> entries;
};

ShellJobTable::ShellJobTable() : impl_(new Impl) {}
ShellJobTable::~ShellJobTable() { delete impl_; }
bool ShellJobTable::supported() const {
#ifdef _WIN32
    return false;
#else
    return true;
#endif
}

#ifndef _WIN32
namespace {
int status_code(int st) {
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return 1;
}
void child_reset_signals() {
    struct sigaction d{}; d.sa_handler=SIG_DFL; sigemptyset(&d.sa_mask);
    for(int s : {SIGINT,SIGQUIT,SIGTSTP,SIGTTIN,SIGTTOU,SIGCHLD}) sigaction(s,&d,nullptr);
}
bool redirect_fd(const std::string& path, int target, bool append, bool read) {
    if(path.empty()) return true;
    int flags = read ? O_RDONLY : (O_WRONLY|O_CREAT|(append?O_APPEND:O_TRUNC));
    int fd = open(path.c_str(), flags, 0666); if(fd<0) return false;
    bool ok = dup2(fd,target)>=0; close(fd); return ok;
}
}
#endif

ProcessResult ShellJobTable::launch(const std::vector<ProcessSpec>& specs, const std::string& command, bool foreground) {
    ProcessResult result;
#ifdef _WIN32
    (void)specs; (void)command; (void)foreground;
    result.error="interactive job control is not supported on Windows"; result.exit_code=2; return result;
#else
    if(specs.empty()){result.error="empty pipeline";return result;}
    Impl::Entry entry; entry.info.id=impl_->next_id++; entry.info.command=command;
    int prev=-1; pid_t pgid=-1;
    for(size_t i=0;i<specs.size();++i){
        int pp[2]={-1,-1}; if(i+1<specs.size()&&pipe(pp)!=0){result.error="pipe failed";break;}
        pid_t pid=fork();
        if(pid<0){result.error="fork failed"; if(pp[0]>=0)close(pp[0]); if(pp[1]>=0)close(pp[1]); break;}
        if(pid==0){
            const pid_t group = pgid<0 ? 0 : pgid; setpgid(0,group); child_reset_signals();
            if(!specs[i].cwd.empty()&&chdir(specs[i].cwd.c_str())!=0)_exit(126);
            for(const auto&kv:specs[i].env)setenv(kv.first.c_str(),kv.second.c_str(),1);
            if(prev>=0)dup2(prev,STDIN_FILENO); else if(!redirect_fd(specs[i].stdin_path,STDIN_FILENO,false,true))_exit(126);
            if(i+1<specs.size())dup2(pp[1],STDOUT_FILENO); else if(!redirect_fd(specs[i].stdout_path,STDOUT_FILENO,specs[i].append_stdout,false))_exit(126);
            if(specs[i].merge_stderr)dup2(STDOUT_FILENO,STDERR_FILENO); else if(!redirect_fd(specs[i].stderr_path,STDERR_FILENO,specs[i].append_stderr,false))_exit(126);
            if (prev >= 0) close(prev);
            if (pp[0] >= 0) close(pp[0]);
            if (pp[1] >= 0) close(pp[1]);
            std::vector<std::string> avs{specs[i].program}; avs.insert(avs.end(),specs[i].args.begin(),specs[i].args.end());
            std::vector<char*> av; for(auto& a:avs)av.push_back(a.data()); av.push_back(nullptr);
            execvp(specs[i].program.c_str(),av.data()); _exit(errno==ENOENT?127:126);
        }
        if (pgid < 0) pgid = pid;
        setpgid(pid, pgid);
        if (prev >= 0) close(prev);
        if (pp[1] >= 0) close(pp[1]);
        prev = pp[0];
        entry.pids.push_back(pid); entry.remaining.insert(pid); entry.info.process_ids.push_back((long long)pid); entry.last_pid=pid; result.launched=true;
    }
    if(prev>=0)close(prev);
    if(!result.error.empty()) { if(pgid>0)kill(-pgid,SIGTERM); return result; }
    entry.pgid=pgid; entry.info.process_group=(long long)pgid; entry.info.state=ShellJobState::Running;
    const int id=entry.info.id; impl_->entries.emplace(id,std::move(entry));
    if(foreground) return this->foreground(id);
    result.exit_code=0; return result;
#endif
}

void ShellJobTable::reap() {
#ifndef _WIN32
    for(auto& kv:impl_->entries){ auto& e=kv.second; if(e.info.state==ShellJobState::Done)continue; bool saw_stop=false;
        for(auto it=e.remaining.begin();it!=e.remaining.end();){ int st=0; pid_t p=waitpid(*it,&st,WNOHANG|WUNTRACED|WCONTINUED); if(p==0){++it;continue;} if(p<0){if(errno==ECHILD){it=e.remaining.erase(it);continue;}++it;continue;}
            if(WIFSTOPPED(st)){saw_stop=true;e.info.state=ShellJobState::Stopped;++it;continue;} if(WIFCONTINUED(st)){e.info.state=ShellJobState::Running;++it;continue;}
            if (p == e.last_pid) e.info.exit_code = status_code(st);
            it = e.remaining.erase(it);
        }
        if(e.remaining.empty())e.info.state=ShellJobState::Done; else if(!saw_stop&&e.info.state!=ShellJobState::Stopped)e.info.state=ShellJobState::Running;
    }
#endif
}
std::vector<ShellJobInfo> ShellJobTable::jobs(bool include_done){ reap(); std::vector<ShellJobInfo> out; for(auto&kv:impl_->entries)if(include_done||kv.second.info.state!=ShellJobState::Done)out.push_back(kv.second.info);return out; }

ProcessResult ShellJobTable::foreground(int job_id){ ProcessResult r;
#ifdef _WIN32
    (void)job_id;r.error="interactive job control is not supported on Windows";r.exit_code=2;return r;
#else
    auto it=impl_->entries.find(job_id);if(it==impl_->entries.end()){r.error="unknown job";return r;}auto&e=it->second;if(e.info.state==ShellJobState::Done){r.launched=true;r.exit_code=e.info.exit_code;return r;}
    const bool tty=isatty(STDIN_FILENO); struct sigaction ign{},ot{},oi{},oq{},oz{}; ign.sa_handler=SIG_IGN;sigemptyset(&ign.sa_mask);
    if(tty){sigaction(SIGTTOU,&ign,&ot);sigaction(SIGTTIN,&ign,&oi);sigaction(SIGINT,&ign,&oq);sigaction(SIGTSTP,&ign,&oz);tcsetpgrp(STDIN_FILENO,e.pgid);} if(e.info.state==ShellJobState::Stopped)kill(-e.pgid,SIGCONT); e.info.state=ShellJobState::Running;
    while(!e.remaining.empty()){int st=0;pid_t p=waitpid(-e.pgid,&st,WUNTRACED);if(p<0){if(errno==EINTR)continue;break;}if(WIFSTOPPED(st)){e.info.state=ShellJobState::Stopped;break;}if(WIFEXITED(st)||WIFSIGNALED(st)){if(p==e.last_pid)e.info.exit_code=status_code(st);e.remaining.erase(p);}}
    if(e.remaining.empty())e.info.state=ShellJobState::Done;
    if(tty){tcsetpgrp(STDIN_FILENO,getpgrp());sigaction(SIGTTOU,&ot,nullptr);sigaction(SIGTTIN,&oi,nullptr);sigaction(SIGINT,&oq,nullptr);sigaction(SIGTSTP,&oz,nullptr);}r.launched=true;r.exit_code=e.info.state==ShellJobState::Stopped?128+SIGTSTP:e.info.exit_code;return r;
#endif
}
bool ShellJobTable::background(int job_id,std::string&error){
#ifdef _WIN32
    (void)job_id;error="interactive job control is not supported on Windows";return false;
#else
    reap();auto it=impl_->entries.find(job_id);if(it==impl_->entries.end()){error="unknown job";return false;}if(it->second.info.state==ShellJobState::Done){error="job is already complete";return false;}if(kill(-it->second.pgid,SIGCONT)!=0){error="failed to continue job";return false;}it->second.info.state=ShellJobState::Running;return true;
#endif
}
ProcessResult ShellJobTable::wait(int job_id){ ProcessResult r;
#ifdef _WIN32
    (void)job_id;r.error="interactive job control is not supported on Windows";r.exit_code=2;return r;
#else
    auto it=impl_->entries.find(job_id);if(it==impl_->entries.end()){r.error="unknown job";return r;}auto&e=it->second;while(!e.remaining.empty()){int st=0;pid_t p=waitpid(-e.pgid,&st,WUNTRACED);if(p<0){if(errno==EINTR)continue;break;}if(WIFSTOPPED(st)){e.info.state=ShellJobState::Stopped;r.exit_code=128+WSTOPSIG(st);r.launched=true;return r;}if(p==e.last_pid)e.info.exit_code=status_code(st);e.remaining.erase(p);}e.info.state=ShellJobState::Done;r.launched=true;r.exit_code=e.info.exit_code;return r;
#endif
}
ProcessResult ShellJobTable::wait_all(){ProcessResult r;r.launched=true;r.exit_code=0;for(auto&kv:impl_->entries){if(kv.second.info.state==ShellJobState::Done)continue;auto one=wait(kv.first);if(!one.error.empty())return one;r.exit_code=one.exit_code;}return r;}
bool ShellJobTable::has_live_jobs(){reap();for(auto&kv:impl_->entries)if(kv.second.info.state!=ShellJobState::Done)return true;return false;}
