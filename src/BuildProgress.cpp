#include "BuildProgress.h"
#include "Console.h"

#include <cmath>
#include <iostream>
#include <utility>

namespace {

// Fixed-width activity bar: a soft green gradient pulse that breathes while it
// travels. All glyphs are single-column so display width stays deterministic.
constexpr std::size_t bar_cells = 12;
constexpr double two_pi = 6.283185307179586476925286766559;
// Travel (left-right) and breathing (width/brightness) run on distinct periods
// in ticks at the 100 ms animation interval: 3.0 s and 2.0 s, so the combined
// pattern repeats every lcm(30, 20) = 60 ticks (6 s) and maximum brightness is
// never pinned to one particular end.
constexpr std::size_t travel_period_ticks = 30;
constexpr std::size_t breathe_period_ticks = 20;
// U+00B7 (middle dot) and the shade block series U+2591..U+2593, U+2588,
// written as UTF-8 bytes so the source stays an ordinary narrow execution
// charset. Inactive cells use the dot; the pulse climbs through the shades.
constexpr const char* glyph_dot = "\xc2\xb7";
constexpr const char* glyph_shade1 = "\xe2\x96\x91";
constexpr const char* glyph_shade2 = "\xe2\x96\x92";
constexpr const char* glyph_shade3 = "\xe2\x96\x93";
constexpr const char* glyph_block = "\xe2\x96\x88";

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
    const double travel = two_pi * static_cast<double>(phase % travel_period_ticks) /
                          static_cast<double>(travel_period_ticks);
    const double breathe = two_pi * static_cast<double>(phase % breathe_period_ticks) /
                           static_cast<double>(breathe_period_ticks);
    // The pulse centre travels with sine easing (zero derivative at both ends,
    // so direction reverses without a jump). Breathing modulates the pulse
    // width and peak brightness on a distinct, faster period, so the light
    // visibly expands/contracts while it moves rather than merely sliding.
    const double margin = std::min(3.0, static_cast<double>(width) * 0.25);
    const double span = static_cast<double>(width) - 2.0 * margin;
    const double centre = margin + span * (0.5 + 0.5 * std::sin(travel));
    const double br = 0.5 + 0.5 * std::sin(breathe);
    const double peak = 0.55 + 0.45 * br;   // dim exhale .. bright inhale
    const double radius = 1.7 + 0.9 * br;   // narrow .. wide pulse

    std::string bar;
    bar.reserve(width * (colour ? 16u : 3u));
    for (std::size_t i = 0; i < width; ++i) {
        const double d = std::fabs(static_cast<double>(i) + 0.5 - centre);
        const double g = peak * std::exp(-(d * d) / (radius * radius));
        int level = 0;
        if (g >= 0.84) level = 4;
        else if (g >= 0.62) level = 3;
        else if (g >= 0.38) level = 2;
        else if (g >= 0.18) level = 1;

        if (level == 0) {
            bar += glyph_dot;
        } else if (colour) {
            switch (level) {
                case 1: bar += "\033[2;32m"; bar += glyph_shade1; bar += "\033[0m"; break;
                case 2: bar += "\033[32m";   bar += glyph_shade2; bar += "\033[0m"; break;
                case 3: bar += "\033[1;32m"; bar += glyph_shade3; bar += "\033[0m"; break;
                default: bar += "\033[1;32m"; bar += glyph_block; bar += "\033[0m"; break;
            }
        } else {
            switch (level) {
                case 1: bar += glyph_shade1; break;
                case 2: bar += glyph_shade2; break;
                case 3: bar += glyph_shade3; break;
                default: bar += glyph_block; break;
            }
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
