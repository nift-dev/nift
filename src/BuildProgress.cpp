#include "BuildProgress.h"
#include "Console.h"

#include <iostream>
#include <utility>

namespace {

// Minimal Aeye-style Braille spinner. The ten glyphs are all single-column, so
// the animated prefix keeps a fixed position and width while the frames change.
// The cycle advances one frame per animation tick.
constexpr const char* spinner_frames[] = {
    "\xe2\xa0\x8b", // ⠋
    "\xe2\xa0\x99", // ⠙
    "\xe2\xa0\xb9", // ⠹
    "\xe2\xa0\xb8", // ⠸
    "\xe2\xa0\xbc", // ⠼
    "\xe2\xa0\xb4", // ⠴
    "\xe2\xa0\xa6", // ⠦
    "\xe2\xa0\xa7", // ⠧
    "\xe2\xa0\x87", // ⠇
    "\xe2\xa0\x8f", // ⠏
};
constexpr std::size_t spinner_frame_count =
    sizeof(spinner_frames) / sizeof(spinner_frames[0]);

} // namespace

BuildProgress::BuildProgress(std::size_t total, std::atomic<std::size_t>& completed)
    : BuildProgress(total, completed, Options{}) {}

BuildProgress::BuildProgress(std::size_t total, std::atomic<std::size_t>& completed, Options options)
    : total_(total), completed_(completed), options_(std::move(options)) {
    if (options_.width.has_value()) {
        width_override_value_.store(*options_.width, std::memory_order_relaxed);
        width_override_.store(true, std::memory_order_relaxed);
    }
    if (total_ == 0 || !enabled()) return;

    thread_ = std::thread([this] {
        {
            std::unique_lock<std::mutex> lock(wait_mutex_);
            if (wake_.wait_for(lock, options_.display_delay, [this] { return stop_.load(); })) return;
        }

        while (!stop_.load() && completed_.load() < total_) {
            if (!render()) return;
            displayed_.store(true, std::memory_order_relaxed);

            std::unique_lock<std::mutex> lock(wait_mutex_);
            wake_.wait_for(lock, options_.interval, [this] { return stop_.load(); });
        }
    });
}

BuildProgress::~BuildProgress() noexcept {
    finish();
}

bool BuildProgress::enabled() const {
    switch (options_.visibility) {
        case Visibility::Enabled:
            return true;
        case Visibility::Disabled:
            return false;
        case Visibility::Detect:
            return console::stdout_is_interactive() && console::stdout_ansi_supported();
    }
    return false;
}

bool BuildProgress::colour_enabled() const {
    if (options_.colour.has_value()) return *options_.colour;
    return console::stdout_colour_enabled();
}

std::ostream& BuildProgress::sink() const {
    return options_.sink ? *options_.sink : std::cout;
}

bool BuildProgress::render() {
    std::string frame_text;
    try {
        std::size_t width = console::terminal_width();
        if (width_override_.load(std::memory_order_relaxed))
            width = width_override_value_.load(std::memory_order_relaxed);
        const std::size_t phase = phase_++;
        frame_text = compose_frame(completed_.load(), total_, phase, width, colour_enabled());
        std::lock_guard<std::mutex> lock(console::output_mutex);
        sink() << frame_text << std::flush;
    } catch (...) {
        stop_.store(true, std::memory_order_relaxed);
        wake_.notify_all();
        return false;
    }
    return true;
}

void BuildProgress::finish() noexcept {
    std::call_once(finished_, [this] {
        stop_.store(true, std::memory_order_relaxed);
        wake_.notify_all();
        // Wait until the renderer has completely stopped. After the join no
        // further frame can be written, so the erase below is guaranteed to be
        // the next thing on the terminal line.
        if (thread_.joinable()) thread_.join();
        if (displayed_.load(std::memory_order_relaxed)) {
            try {
                std::lock_guard<std::mutex> lock(console::output_mutex);
                sink() << "\r\033[2K" << std::flush;
            } catch (...) {
                // Best-effort cleanup during shutdown must never throw.
            }
        }
    });
}

std::string BuildProgress::compose_spinner(std::size_t phase, bool colour) {
    const std::string glyph = spinner_frames[phase % spinner_frame_count];
    return colour ? "\033[32m" + glyph + "\033[0m" : glyph;
}

std::string BuildProgress::compose_frame(std::size_t done, std::size_t total, std::size_t phase,
                                         std::size_t term_width, bool colour) {
    const std::size_t percent = total ? (100 * done) / total : 0;
    const std::string count = std::to_string(done) + "/" + std::to_string(total);
    const std::string pct = std::to_string(percent) + "%";
    const std::string spinner = compose_spinner(phase, colour);

    // Layout tiers, measured in visible columns and recomputed for every frame.
    // The spinner is always one column wide, so the animated prefix keeps a
    // fixed position and width; only the trailing text is dropped as the
    // terminal narrows. A frame that fits its tier can never wrap.
    const std::string full_line = "  " + spinner + " building " + count + "  " + pct;
    const std::string medium_line = "  " + spinner + " " + count + "  " + pct;
    const std::string narrow_line = "  " + spinner + " " + pct;
    const std::size_t full_needed = console::display_width(full_line);
    const std::size_t medium_needed = console::display_width(medium_line);
    const std::size_t narrow_needed = console::display_width(narrow_line);
    const std::size_t very_needed = console::display_width(pct);

    std::string text;
    if (term_width >= full_needed) {
        text = full_line;
    } else if (term_width >= medium_needed) {
        text = medium_line;
    } else if (term_width >= narrow_needed) {
        text = narrow_line;
    } else if (term_width >= very_needed) {
        text = pct;
    }

    // Every frame begins with a full-line clear so a shorter frame never leaves
    // the previous (possibly longer) frame's suffix visible.
    return "\r\033[2K" + text;
}
