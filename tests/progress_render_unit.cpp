#include "BuildProgress.h"
#include "Console.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <ostream>
#include <streambuf>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define popen _popen
#define pclose _pclose
#endif

// Portable, deterministic regression coverage for the BuildProgress renderer.
// No PTY is required: interactivity is controlled through BuildProgress's
// explicit Visibility seam and output is captured through an injected sink, so
// results never depend on whether the test binary itself was launched from a
// terminal.

namespace {

int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void check_eq(const std::string& actual, const std::string& expected, const char* message) {
    if (actual != expected) {
        std::cerr << "FAIL: " << message << "\n  expected: [" << expected << "]\n  actual:   [" << actual << "]\n";
        ++failures;
    }
}

class ScopedEnv {
public:
    explicit ScopedEnv(const char* name) : name_(name) {
#if defined(_WIN32)
        char* value = nullptr;
        size_t length = 0;
        if (_dupenv_s(&value, &length, name) == 0 && value != nullptr) {
            had_ = true;
            previous_ = value;
            free(value);
        }
#else
        const char* value = std::getenv(name);
        if (value != nullptr) {
            had_ = true;
            previous_ = value;
        }
#endif
    }

    ~ScopedEnv() { restore(); }

    void set(const char* value) {
#if defined(_WIN32)
        _putenv_s(name_, value);
#else
        setenv(name_, value, 1);
#endif
    }

private:
    void restore() {
#if defined(_WIN32)
        _putenv_s(name_, had_ ? previous_.c_str() : "");
#else
        if (had_) setenv(name_, previous_.c_str(), 1);
        else unsetenv(name_);
#endif
    }

    const char* name_;
    bool had_ = false;
    std::string previous_;
};

// A streambuf that captures renderer output and signals a condition variable on
// every write, so the tests synchronize on real bytes rather than sleeps.
class NotificationBuffer : public std::streambuf {
public:
    NotificationBuffer() = default;

    std::string data_unlocked() const { return buffer_; }
    std::size_t writes_unlocked() const { return writes_; }

protected:
    int_type overflow(int_type c) override {
        if (c != traits_type::eof()) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                buffer_.push_back(static_cast<char>(c));
                ++writes_;
            }
            cv_.notify_all();
        }
        return c;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            buffer_.append(s, static_cast<std::size_t>(n));
            ++writes_;
        }
        cv_.notify_all();
        return n;
    }

public:
    std::mutex mutex_;
    std::condition_variable cv_;

private:
    std::string buffer_;
    std::size_t writes_ = 0;
};

bool wait_for(NotificationBuffer& sink, std::function<bool(const std::string&)> predicate,
              int timeout_ms = 3000) {
    std::unique_lock<std::mutex> lock(sink.mutex_);
    return sink.cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                             [&] { return predicate(sink.data_unlocked()); });
}

// Minimal terminal screen model: handles \r, \n, \033[2K, \033[K and SGR
// stripping, enough to reconstruct the visible current/last line.
class Screen {
public:
    void feed(const std::string& bytes) {
        for (char c : bytes) feed(c);
    }

    void feed(char c) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (state_ == 0) {
            if (c == '\r') cursor_ = 0;
            else if (c == '\n') { last_line_ = line_; line_.clear(); cursor_ = 0; }
            else if (uc == 0x1b) { state_ = 1; params_.clear(); }
            else put(c);
        } else if (state_ == 1) {
            if (c == '[') { state_ = 2; params_.clear(); }
            else state_ = 0;
        } else {
            if ((c >= '0' && c <= '9') || c == ';' || c == '?') params_.push_back(c);
            else {
                if (c == 'K') {
                    if (params_ == "2") { line_.clear(); cursor_ = 0; }
                    else if (params_.empty() && cursor_ < line_.size()) line_.erase(cursor_);
                }
                state_ = 0;
            }
        }
    }

    const std::string& current() const { return line_; }
    const std::string& last() const { return last_line_; }

private:
    void put(char c) {
        if (cursor_ < line_.size()) line_[cursor_] = c;
        else line_.push_back(c);
        ++cursor_;
    }

    std::string line_;
    std::string last_line_;
    std::size_t cursor_ = 0;
    int state_ = 0;
    std::string params_;
};

void feed_screen(Screen& screen, NotificationBuffer& sink) {
    std::lock_guard<std::mutex> lock(sink.mutex_);
    screen.feed(sink.data_unlocked());
}

BuildProgress::Options test_options(std::ostream& sink, std::size_t width) {
    BuildProgress::Options options;
    options.display_delay = std::chrono::milliseconds(5);
    options.interval = std::chrono::milliseconds(5);
    options.sink = &sink;
    options.visibility = BuildProgress::Visibility::Enabled;
    options.colour = false;
    options.width = width;
    return options;
}

std::string strip_erase(const std::string& frame) {
    constexpr const char* erase = "\r\033[2K";
    constexpr std::size_t erase_len = 5;
    return frame.compare(0, erase_len, erase) == 0 ? frame.substr(erase_len) : frame;
}

// Path to this test executable, captured from argv[0] so the Detect subprocess
// test can re-exec itself.
std::string program_path_;

std::string shell_quote(const std::string& value) {
#if defined(_WIN32)
    return "\"" + value + "\"";
#else
    return "'" + value + "'";
#endif
}

// Runs the production Visibility::Detect path with genuinely redirected stdout:
// the child's stdout is a pipe (popen), so the renderer must not start and no
// progress bytes (\r or \033) may appear. Deterministic regardless of whether
// this test executable itself was launched from a terminal.
void test_detect_noninteractive_child() {
    const std::string command = shell_quote(program_path_) + " --progress-detect-child";
    FILE* pipe = popen(command.c_str(), "r");
    check(pipe != nullptr, "popen starts the detect child");
    std::string captured;
    if (pipe != nullptr) {
        char buffer[256];
        std::size_t n = 0;
        while ((n = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0)
            captured.append(buffer, n);
        pclose(pipe);
    }
    check(captured.empty(), "detect child with redirected stdout emits no progress bytes");
    check(captured.find('\r') == std::string::npos, "redirected output contains no carriage returns");
    check(captured.find('\033') == std::string::npos, "redirected output contains no escape sequences");
}

void test_frame_tiers() {
    const std::string wide = strip_erase(BuildProgress::compose_frame(15003, 16253, 0, 80, false));
    check_eq(wide, "  building 15003/16253  92%  [" + BuildProgress::compose_bar(0, 12, false) + "]",
             "wide tier keeps count, percent and a 12-cell bar");
    check(wide.find("building") != std::string::npos, "wide tier shows the building label");

    const std::string medium = strip_erase(BuildProgress::compose_frame(15003, 16253, 0, 40, false));
    check_eq(medium, "  building 15003/16253  92%", "medium tier drops the bar but keeps the label");

    const std::string narrow = strip_erase(BuildProgress::compose_frame(15003, 16253, 0, 20, false));
    check_eq(narrow, "15003/16253  92%", "narrow tier drops the label");

    const std::string very_narrow = strip_erase(BuildProgress::compose_frame(15003, 16253, 0, 4, false));
    check_eq(very_narrow, "92%", "very narrow tier keeps only the percentage");

    const std::string too_narrow = strip_erase(BuildProgress::compose_frame(15003, 16253, 0, 2, false));
    check(too_narrow.empty(), "no frame is drawn when even the percentage does not fit");

    // No tier may exceed its terminal width (no wrapping).
    for (std::size_t width : {80u, 40u, 20u, 4u}) {
        const std::string frame = strip_erase(BuildProgress::compose_frame(15003, 16253, 3, width, false));
        check(console::display_width(frame) <= width, "rendered frame never exceeds terminal width");
    }

    const std::string coloured = BuildProgress::compose_frame(15003, 16253, 3, 80, true);
    check(coloured.find("\033[1;36m") != std::string::npos, "colour frames emit the bright peak SGR");
    check(coloured.find("\033[36m") != std::string::npos, "colour frames emit the shoulder SGR");

    const std::string plain = BuildProgress::compose_frame(15003, 16253, 3, 80, false);
    check(strip_erase(plain).find('\033') == std::string::npos, "no-colour frames contain no ANSI beyond the erase");
    check(plain.find('\n') == std::string::npos, "frames never contain a newline");
}

void test_bar_plain() {
    const std::string bar = BuildProgress::compose_bar(0, 12, false);
    check(console::display_width(bar) == 12, "plain bar is exactly the requested width in columns");
    check(bar.find('\033') == std::string::npos, "plain bar has no ANSI sequences");
    check(bar.find("▓") != std::string::npos, "plain bar shows a moving highlight");
    check(bar.find("·") != std::string::npos, "plain bar shows inactive cells");

    // A bar with colour must not exceed the same column count after SGR strip.
    const std::string coloured = BuildProgress::compose_bar(3, 12, true);
    check(coloured.find("\033[1;36m") != std::string::npos, "coloured bar contains the bright peak SGR");
}

void test_no_color_detection() {
    ScopedEnv env("NO_COLOR");
    env.set("1");
    check(console::no_color_requested(), "NO_COLOR with a non-empty value disables colour");
    env.set("");
    check(!console::no_color_requested(), "empty NO_COLOR leaves colour enabled (convention: non-empty disables)");
    env.set("0");
    check(console::no_color_requested(), "NO_COLOR value does not matter, only presence of a non-empty value");
}

void test_disabled_emits_nothing() {
    NotificationBuffer buffer;
    std::ostream sink(&buffer);
    std::atomic<std::size_t> completed{0};
    {
        BuildProgress::Options options = test_options(sink, 80);
        options.visibility = BuildProgress::Visibility::Disabled;
        BuildProgress progress(1000, completed, options);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        progress.finish();
        check(!progress.running(), "disabled renderer never starts a thread");
        check(buffer.data_unlocked().empty(), "disabled renderer emits no bytes at all");
    }
}

void test_zero_total_emits_nothing() {
    NotificationBuffer buffer;
    std::ostream sink(&buffer);
    std::atomic<std::size_t> completed{0};
    {
        BuildProgress::Options options = test_options(sink, 80);
        BuildProgress progress(0, completed, options);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        progress.finish();
        check(!progress.running(), "zero-total renderer never starts a thread");
        check(buffer.data_unlocked().empty(), "zero-total renderer emits no bytes at all");
    }
}

void test_deterministic_fixed_ordering() {
    NotificationBuffer buffer;
    std::ostream sink(&buffer);
    std::atomic<std::size_t> completed{0};
    {
        BuildProgress progress(1000, completed, test_options(sink, 80));
        check(wait_for(buffer, [](const std::string& d) { return d.find("building") != std::string::npos; }),
              "renderer writes at least one frame before shutdown");

        progress.finish();

        // Deterministic proof of shutdown: the thread is joined, the erase was
        // written as part of finish(), and no further bytes (frames or erases)
        // arrive afterwards.
        check(!progress.running(), "renderer thread is joined after finish()");
        const std::size_t writes_after_finish = buffer.writes_unlocked();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        check(buffer.writes_unlocked() == writes_after_finish,
              "no renderer output occurs after finish() returns");

        Screen screen;
        feed_screen(screen, buffer);
        check(screen.current().empty(), "final transient line is fully erased before the summary");

        // Summary is emitted only after the erase, under the same output lock.
        {
            std::lock_guard<std::mutex> lock(console::output_mutex);
            sink << "📦 3 pages built successfully\n" << std::flush;
        }
        feed_screen(screen, buffer);
        check_eq(screen.last(), "📦 3 pages built successfully",
                 "summary lands on a clean line with no stale progress prefix");
    }
}

void test_deterministic_buggy_ordering_detected() {
    // Recreates the pre-fix ordering: summary written while the renderer is
    // still live and before any erase. The screen model must expose the stale
    // prefix, proving this regression test would catch the original bug.
    NotificationBuffer buffer;
    std::ostream sink(&buffer);
    std::atomic<std::size_t> completed{0};
    {
        BuildProgress progress(1000, completed, test_options(sink, 80));
        check(wait_for(buffer, [](const std::string& d) { return d.find("building") != std::string::npos; }),
              "renderer writes at least one frame before shutdown");

        {
            std::lock_guard<std::mutex> lock(console::output_mutex);
            sink << "📦 3 pages built successfully\n" << std::flush;
        }
        Screen screen;
        feed_screen(screen, buffer);
        check(screen.last().find("building") != std::string::npos,
              "summary written before shutdown leaves the stale progress prefix");
        check(screen.last().find("built successfully") != std::string::npos,
              "stale-prefix line still contains the summary");

        progress.finish();
    }
}

void test_wide_to_medium_no_stale_suffix() {
    NotificationBuffer buffer;
    std::ostream sink(&buffer);
    std::atomic<std::size_t> completed{0};
    {
        BuildProgress progress(1000, completed, test_options(sink, 80));
        check(wait_for(buffer, [](const std::string& d) { return d.find('[') != std::string::npos; }),
              "renderer draws a wide frame with the bar first");

        progress.set_width_override(30);
        check(wait_for(buffer, [](const std::string& d) {
                  const std::string marker = "\r\033[2K";
                  const auto pos = d.rfind(marker);
                  if (pos == std::string::npos) return false;
                  const std::string tail = d.substr(pos + marker.size());
                  return tail.find('[') == std::string::npos && tail.find("  building") == 0;
              }),
              "renderer redraws at the medium tier after the width override");

        Screen screen;
        feed_screen(screen, buffer);
        check_eq(screen.current(), "  building 0/1000  0%",
                 "medium frame fully replaces the wide frame (no stale bar suffix)");
        check(screen.current().find('[') == std::string::npos, "no bar residue survives the transition");

        progress.finish();
        feed_screen(screen, buffer);
        check(screen.current().empty(), "finish erases the final line entirely");
    }
}

void test_finish_idempotent_with_destructor() {
    NotificationBuffer buffer;
    std::ostream sink(&buffer);
    std::atomic<std::size_t> completed{0};
    std::size_t size_after_explicit = 0;
    {
        BuildProgress progress(1000, completed, test_options(sink, 80));
        check(wait_for(buffer, [](const std::string& d) { return !d.empty(); }),
              "renderer produces output before explicit finish");
        progress.finish();
        size_after_explicit = buffer.data_unlocked().size();
        check(!progress.running(), "explicit finish joins the renderer");
    }
    // Destructor re-enters finish(); call_once makes the second call a no-op,
    // so no double erase is appended and nothing can throw.
    check(buffer.data_unlocked().size() == size_after_explicit,
          "destructor cleanup is a no-op after explicit finish (no double erase)");
}

void test_stress_repeat_no_timing_race() {
    constexpr int iterations = 40;
    for (int i = 0; i < iterations; ++i) {
        NotificationBuffer buffer;
        std::ostream sink(&buffer);
        std::atomic<std::size_t> completed{0};
        BuildProgress::Options options = test_options(sink, 80);
        options.display_delay = std::chrono::milliseconds(1);
        options.interval = std::chrono::milliseconds(2);
        {
            BuildProgress progress(1000, completed, options);
            completed.store(static_cast<std::size_t>(i * 37 % 1000));
            const bool saw_frame = wait_for(buffer, [](const std::string& d) { return !d.empty(); }, 1000);
            check(saw_frame, "renderer produced a frame under stress");
            progress.finish();
            check(!progress.running(), "stress finish always joins the renderer");
            const std::size_t writes_after_finish = buffer.writes_unlocked();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            check(buffer.writes_unlocked() == writes_after_finish, "stress finish is followed by no further output");
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--progress-detect-child") {
        // Child mode of the redirected-output Detect test: real stdout is the
        // pipe popen set up, so Visibility::Detect must not start a renderer.
        // Emits nothing on stdout.
        std::atomic<std::size_t> completed{0};
        BuildProgress::Options options;
        options.display_delay = std::chrono::milliseconds(1);
        options.interval = std::chrono::milliseconds(1);
        {
            BuildProgress progress(1000, completed, options);
            completed.store(500);
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            progress.finish();
        }
        return 0;
    }

    program_path_ = argv[0];
    test_frame_tiers();
    test_bar_plain();
    test_no_color_detection();
    test_disabled_emits_nothing();
    test_zero_total_emits_nothing();
    test_detect_noninteractive_child();
    test_deterministic_fixed_ordering();
    test_deterministic_buggy_ordering_detected();
    test_wide_to_medium_no_stale_suffix();
    test_finish_idempotent_with_destructor();
    test_stress_repeat_no_timing_race();

    if (failures) {
        std::cerr << failures << " progress-render check(s) failed\n";
        return 1;
    }
    std::cout << "progress renderer smoke passed\n";
    return 0;
}