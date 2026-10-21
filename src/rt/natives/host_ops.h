#pragma once
// CP16 VM host built-ins (slice 1): handle-free operations through HostCall.
#include "rt/natives/builtin.h"

namespace nift::rt::natives {

Status host_open(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_open_bytes(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_cat(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_sha256_file(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_exists(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_is_file(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_is_dir(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_stat(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_ls(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_pwd(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_cd(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_touch(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_mkdir(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_remove(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_cp(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_mv(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_which(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);

Status host_getenv(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_env(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_setenv(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_unsetenv(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_os(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_arch(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_platform(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_hardware_concurrency(const Context&, const Value* args, std::uint32_t argc, Value& out,
                                 std::string& error);

Status host_epoch(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_sleep(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status host_secure_random_bytes(const Context&, const Value* args, std::uint32_t argc, Value& out,
                                std::string& error);

} // namespace nift::rt::natives
