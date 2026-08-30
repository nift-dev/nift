#include "BuildProgress.h"
#include "Console.h"

#include <cmath>
#include <iostream>
#include <utility>

namespace {

// Fixed-width activity bar: a smooth highlight that breathes back and forth.
// The two glyphs are both single-column so display width stays deterministic.
constexpr std::size_t bar_cells = 12;
constexpr double two_pi = 6.283185307179586476925286766559;
// Full back-and-forth cycle in ticks (2.8 s at the 100 ms animation interval).
constexpr std::size_t animation_period_ticks = 28;
// U+00B7 (middle dot) and U+2593 (dark shade), written as UTF-8 bytes so the
// source stays an ordinary narrow execution charset.
constexpr const char* dot_glyph = "\xc2\xb7";
constexpr const char* block_glyph = "\xe2\x96\x93";

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

std::string BuildProgress::compose_bar(std::size_t phase, std::size_t width, bool colour) {
    if (width == 0) return {};
    const double t = two_pi * static_cast<double>(phase % animation_period_ticks) /
                     static_cast<double>(animation_period_ticks);
    // The centre travels between 1.5 and width - 1.5 so the 3-cell highlight
    // never clips at either edge and reverses smoothly (the sine derivative is
    // zero at the extremes, so the direction change does not jump).
    const double centre = 1.5 + (static_cast<double>(width) - 3.0) * (0.5 + 0.5 * std::sin(t));
    std::string bar;
    bar.reserve(width * (colour ? 11u : 1u));
    for (std::size_t i = 0; i < width; ++i) {
        const double distance = std::fabs(static_cast<double>(i) + 0.5 - centre);
        if (colour) {
            if (distance <= 0.5) bar += "\033[1;36m" "\xe2\x96\x93" "\033[0m";      // bright peak
            else if (distance <= 1.0) bar += "\033[36m" "\xe2\x96\x93" "\033[0m";   // shoulders
            else bar += dot_glyph;
        } else {
            if (distance <= 1.0) bar += block_glyph;
            else bar += dot_glyph;
        }
    }
    return bar;
}

std::string BuildProgress::compose_frame(std::size_t done, std::size_t total, std::size_t phase,
                                         std::size_t term_width, bool colour) {
    const std::size_t percent = total ? (100 * done) / total : 0;
    const std::string count = std::to_string(done) + "/" + std::to_string(total);
    const std::string pct = std::to_string(percent) + "%";

    // Layout tiers, measured in visible columns and recomputed for every frame.
    // A frame that fits its tier can never wrap.
    const std::string wide_prefix = "  building " + count + "  " + pct + "  [";
    const std::size_t wide_needed = console::display_width(wide_prefix) + bar_cells + 1;
    const std::string medium_line = "  building " + count + "  " + pct;
    const std::size_t medium_needed = console::display_width(medium_line);
    const std::string narrow_line = count + "  " + pct;
    const std::size_t narrow_needed = console::display_width(narrow_line);
    const std::size_t very_needed = console::display_width(pct);

    std::string text;
    if (term_width >= wide_needed) {
        text = wide_prefix + compose_bar(phase, bar_cells, colour) + "]";
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
