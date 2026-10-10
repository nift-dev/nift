#pragma once
#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <mutex>
#include <unistd.h>
// Serialize Nift-owned descriptor setup through fork. This closes the portable
// pipe/mkstemp + fcntl race on platforms without atomic CLOEXEC creation.
inline std::mutex& nift_process_launch_mutex() { static std::mutex value; return value; }
inline bool nift_process_cloexec(int fd) { return fcntl(fd,F_SETFD,FD_CLOEXEC)>=0; }
inline int nift_process_owned_fd(int fd) {
    if(fd<0)return fd;
    if(fd<3){int high=fcntl(fd,F_DUPFD,3);int error=errno;close(fd);fd=high;errno=error;}
    if(fd>=0 && !nift_process_cloexec(fd)){int error=errno;close(fd);errno=error;return -1;}
    return fd;
}
inline bool nift_process_pipe(int ends[2]) {
    if(pipe(ends)!=0)return false;
    ends[0]=nift_process_owned_fd(ends[0]);ends[1]=nift_process_owned_fd(ends[1]);
    if(ends[0]>=0 && ends[1]>=0)return true;
    int error=errno;close(ends[0]);close(ends[1]);ends[0]=ends[1]=-1;errno=error;return false;
}
// Child-side routing uses only descriptor syscalls, including the same-fd case.
inline bool nift_process_dup2(int source,int target,bool fail=false) {
    if(fail)return false;
    if(source==target)return fcntl(target,F_SETFD,0)>=0;
    int result;do { result=dup2(source,target); } while(result<0 && errno==EINTR);
    return result>=0;
}
#endif
