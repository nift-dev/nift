#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <thread>

class BuildProgress {
public:
    // Change this one value to control how long a build must run before
    // interactive progress is shown.
    static constexpr std::chrono::milliseconds display_delay{200};

    // Explicit control over whether the renderer is active. Production uses
    // Detect (query the real terminal). Tests use Enabled/Disabled so their
    // behaviour never depends on the unrelated real stdout of the test binary.
    enum class Visibility { Detect, Enabled, Disabled };

    struct Options {
        std::chrono::milliseconds display_delay{BuildProgress::display_delay};
        std::chrono::milliseconds interval{100};
        std::ostream* sink = nullptr;     // nullptr selects std::cout
        Visibility visibility = Visibility::Detect;
        std::optional<bool> colour;       // nullopt selects production detection
        std::optional<std::size_t> width; // nullopt selects console::terminal_width()
    };

    BuildProgress(std::size_t total, std::atomic<std::size_t>& completed);
    BuildProgress(std::size_t total, std::atomic<std::size_t>& completed, Options options);
    ~BuildProgress() noexcept;

    BuildProgress(const BuildProgress&) = delete;
    BuildProgress& operator=(const BuildProgress&) = delete;

    // Deterministic shutdown: request stop, wake, join, then erase the final
    // transient line and flush. Idempotent; the destructor calls it as an RAII
    // fallback. Never throws.
    void finish() noexcept;

    // True while the renderer thread is still alive (i.e. until finish() has
    // joined it). Used by tests to prove shutdown is complete.
    bool running() const { return thread_.joinable(); }

    // Test seam: override the terminal width the renderer observes for the
    // next and subsequent frames. Production leaves detection in place. The
    // override is stored atomically so it is safe to change while rendering.
    void set_width_override(std::size_t width) {
        width_override_value_.store(width, std::memory_order_relaxed);
        width_override_.store(true, std::memory_order_relaxed);
    }

    // Pure frame composition, exposed for deterministic unit coverage. The
    // returned string is exactly what render() writes, including the leading
    // full-line clear so every frame erases the previous, longer or shorter.
    static std::string compose_spinner(std::size_t phase, bool colour);
    static std::string compose_frame(std::size_t done, std::size_t total, std::size_t phase,
                                     std::size_t term_width, bool colour);

private:
    bool enabled() const;
    bool colour_enabled() const;
    std::ostream& sink() const;
    bool render();

    std::size_t total_;
    std::atomic<std::size_t>& completed_;
    Options options_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> displayed_{false};
    std::atomic<bool> width_override_{false};
    std::atomic<std::size_t> width_override_value_{80};
    std::size_t phase_{0};
    std::mutex wait_mutex_;
    std::condition_variable wake_;
    std::thread thread_;
    std::once_flag finished_;
};
