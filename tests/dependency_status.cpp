#include "FileSystem.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace fs=std::filesystem;
int main(int argc,char** argv) {
    assert(argc==2);const fs::path root=argv[1];fs::create_directories(root);
    const auto file=root/"input";filesystem::write_file(file,"old");
    const auto original=filesystem::dependency_status(file);
    assert(original.exists&&!original.error);
#ifdef _WIN32
    HANDLE handle=CreateFileW(file.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    assert(handle!=INVALID_HANDLE_VALUE);
    FILE_BASIC_INFO native{};
    assert(GetFileInformationByHandleEx(handle,FileBasicInfo,&native,sizeof(native)));
    CloseHandle(handle);
    const auto ticks=static_cast<std::uint64_t>(native.LastWriteTime.QuadPart);
    assert(original.mtime.seconds==static_cast<std::int64_t>(ticks/10000000ull)-11644473600ll);
    assert(original.mtime.nanoseconds==(ticks%10000000ull)*100ull);
#else
    struct stat native{};assert(::stat(file.c_str(),&native)==0);
#ifdef __APPLE__
    assert(original.mtime.seconds==native.st_mtimespec.tv_sec&&original.mtime.nanoseconds==native.st_mtimespec.tv_nsec);
#else
    assert(original.mtime.seconds==native.st_mtim.tv_sec&&original.mtime.nanoseconds==native.st_mtim.tv_nsec);
#endif
#endif
    assert(original.mtime>=original.mtime);
    auto later=original.mtime;++later.seconds;assert(later>=original.mtime);assert(!(original.mtime>=later));
    filesystem::DependencyTime subsecond{123, 456};
    auto adjacent=subsecond;++adjacent.nanoseconds;
    assert(adjacent>=subsecond && !(subsecond>=adjacent));
    // A subsequent comparison observes the mutation, never a retained cache.
    fs::last_write_time(file,fs::last_write_time(file)+std::chrono::seconds(2));
    const auto changed=filesystem::dependency_status(file);assert(changed.mtime>=later);
    fs::remove(file);const auto missing=filesystem::dependency_status(file);assert(!missing.exists&&missing.error);
    filesystem::write_file(file,"recreated");assert(filesystem::dependency_status(file).exists);
    const auto directory=filesystem::dependency_status(root);assert(directory.exists&&!directory.error);
    std::error_code error;fs::create_symlink("input",root/"alias",error);
    if(!error){const auto alias=filesystem::dependency_status(root/"alias");const auto current=filesystem::dependency_status(file);assert(alias.exists&&!alias.error&&alias.mtime>=current.mtime&&current.mtime>=alias.mtime);
        fs::create_symlink("absent",root/"broken");const auto broken=filesystem::dependency_status(root/"broken");assert(!broken.exists&&broken.error);
        fs::create_symlink("cycle",root/"cycle");const auto cycle=filesystem::dependency_status(root/"cycle");assert(!cycle.exists&&cycle.error);
    }
#ifndef _WIN32
    if(geteuid()!=0){fs::create_directory(root/"denied");filesystem::write_file(root/"denied/input","bytes");fs::permissions(root/"denied",fs::perms::none);const auto denied=filesystem::dependency_status(root/"denied/input");fs::permissions(root/"denied",fs::perms::owner_all);assert(!denied.exists&&denied.error==std::errc::permission_denied);
        fs::permissions(file,fs::perms::none);assert(filesystem::dependency_status(file).exists);assert(!filesystem::read_file_checked(file));fs::permissions(file,fs::perms::owner_all);
    }
#endif
    fs::remove_all(root);std::cout<<"dependency status contracts PASS\n";
}
