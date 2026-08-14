#pragma once

#include <atomic>
#include <iostream>
#include <mutex>
#include <string>

#if defined(_WIN32)
    #include <io.h>
    #define NIFT_ISATTY _isatty
    #define NIFT_FILENO _fileno
#else
    #include <unistd.h>
    #define NIFT_ISATTY isatty
    #define NIFT_FILENO fileno
#endif

namespace console {
inline std::mutex output_mutex;
inline std::atomic<bool> plain_output{false};

inline bool stdout_is_tty() {
    return !plain_output.load(std::memory_order_relaxed) && NIFT_ISATTY(NIFT_FILENO(stdout)) != 0;
}

inline bool stderr_is_tty() {
    return !plain_output.load(std::memory_order_relaxed) && NIFT_ISATTY(NIFT_FILENO(stderr)) != 0;
}

class ScopedPlainOutput {
public:
    ScopedPlainOutput()
        : previous_(plain_output.exchange(true, std::memory_order_relaxed)) {}

    ~ScopedPlainOutput() {
        plain_output.store(previous_, std::memory_order_relaxed);
    }

    ScopedPlainOutput(const ScopedPlainOutput&) = delete;
    ScopedPlainOutput& operator=(const ScopedPlainOutput&) = delete;

private:
    bool previous_;
};

inline std::string paint(const std::string& text, const char* code, bool enabled) {
    return enabled ? std::string("\033[") + code + "m" + text + "\033[0m" : text;
}

inline std::string path(const std::string& text, bool stderr_stream = false) {
    return paint(text, "36", stderr_stream ? stderr_is_tty() : stdout_is_tty());
}
inline std::string good(const std::string& text) { return paint(text, "32", stdout_is_tty()); }
inline std::string dim(const std::string& text) { return paint(text, "2", stdout_is_tty()); }
inline std::string heading(const std::string& text) { return paint(text, "1;35", stdout_is_tty()); }
inline std::string json_key(const std::string& text) { return paint(text, "1;34", stdout_is_tty()); }
inline std::string json_string(const std::string& text) { return paint(text, "32", stdout_is_tty()); }
inline std::string json_number(const std::string& text) { return paint(text, "36", stdout_is_tty()); }
inline std::string json_literal(const std::string& text) { return paint(text, "35", stdout_is_tty()); }
inline std::string error_label() { return paint("error:", "1;31", stderr_is_tty()); }
inline std::string warning_label() { return paint("warning:", "1;33", stderr_is_tty()); }

inline void error(const std::string& message) {
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cerr << error_label() << ' ' << message << '\n';
}
} // namespace console
