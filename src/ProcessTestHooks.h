#pragma once
#include <cstddef>
#include <string>
// Linked only by fault-injection test binaries; never controlled by user env.
#ifdef NIFT_PROCESS_TEST_HOOKS
bool nift_process_test_fail(const char* operation, std::size_t stage);
void nift_process_test_child(long long pid);
void nift_process_test_temp(const std::string& path);
#else
inline void nift_process_test_temp(const std::string&) {}
inline void nift_process_test_child(long long) {}
inline bool nift_process_test_fail(const char*, std::size_t) { return false; }
#endif
