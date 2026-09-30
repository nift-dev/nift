#include "Parser.h"
#include "ScriptHost.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <future>
#include <stdexcept>

int main() {
    using Clock = std::chrono::steady_clock;
    Clock::time_point now{};
    ScriptRenderHost host(std::filesystem::current_path());
    TrackedInfo tracked;
    Parser parser(host, tracked, {}, [&] { return now; });

    nift::RuntimeValue value;
    std::string error;
    auto eval = [&](const std::string& expression) {
        error.clear();
        assert(parser.eval_expression(expression, value, error));
    };

    eval("t := timer()");
    eval("type(t)"); assert(value.is_string() && value.string == "timer");
    eval("t.elapsed()"); assert(value.is_number() && value.num == 0);
    eval("t.running()"); assert(value.is_bool() && !value.boolean);
    eval("t.paused()"); assert(value.is_bool() && !value.boolean);
    eval("t.pause()"); eval("t.resume()"); eval("t.stop()");
    eval("t.elapsed()"); assert(value.num == 0);
    eval("t.running()"); assert(!value.boolean);

    eval("t.start()");
    now += std::chrono::milliseconds(17);
    eval("t.resume()");
    eval("t.elapsed()"); assert(value.num == 17);
    eval("t.pause()");
    eval("t.pause()");
    now += std::chrono::milliseconds(50);
    eval("t.elapsed()"); assert(value.num == 17);
    eval("t.paused()"); assert(value.boolean);
    eval("t.stop()");
    eval("t.resume()");
    eval("t.elapsed()"); assert(value.num == 17);
    eval("t.running()"); assert(!value.boolean);
    eval("t.start()");
    now += std::chrono::milliseconds(8);
    eval("t.stop()");
    now += std::chrono::milliseconds(50);
    eval("t.elapsed()"); assert(value.num == 8);

    eval("alias := t");
    eval("copy_value := copy(t)");
    eval("deep_value := deepcopy(t)");
    eval("alias == t && copy_value == t && deep_value == t"); assert(value.boolean);
    eval("other := timer()");
    eval("other != t"); assert(value.boolean);

    eval("t");
    const nift::RuntimeValue timer_value = value;
    assert(timer_value.is_timer() && timer_value.timer);
    assert(nift::runtime_equal(timer_value, timer_value));
    bool rejected_json = false;
    try { (void)timer_value.dump(); }
    catch (const std::runtime_error&) { rejected_json = true; }
    assert(rejected_json);

    Parser other_parser(host, tracked, {}, [&] { return now; });
    nift::RuntimeValue other_timer;
    assert(other_parser.eval_expression("timer()", other_timer, error));
    assert(other_timer.timer && other_timer.timer->owner != timer_value.timer->owner);
    assert(!nift::runtime_equal(other_timer, timer_value));

    const std::size_t timer_count = parser.timer_instance_count();
    for (int i = 0; i < 128; ++i) {
        const auto checkpoint = parser.begin_timer_operation();
        assert(parser.eval_expression(i % 2 == 0 ? "timer()" : "type(timer())", value, error));
        parser.finish_timer_operation(checkpoint);
        assert(parser.timer_instance_count() == timer_count);
    }

    const nift::RuntimeValue forged(std::string("\x1fnift:timer:1"));
    assert(forged.is_string() && !forged.is_timer());
    assert(!nift::runtime_contains_timer(forged));
    assert(forged.dump() == "\"\\u001fnift:timer:1\"");

    eval("left_mutex := mutex()");
    eval("right_mutex := mutex()");
    eval("left_mutex.lock()");
    eval("left_mutex.set(right_mutex)");
    eval("left_mutex.unlock()");
    eval("right_mutex.lock()");
    eval("right_mutex.set(left_mutex)");
    eval("right_mutex.unlock()");
    eval("left_mutex"); const nift::RuntimeValue left_mutex = value;
    eval("right_mutex"); const nift::RuntimeValue right_mutex = value;
    auto left_scan = std::async(std::launch::async, [&] { return parser.contains_timer_resource(left_mutex); });
    auto right_scan = std::async(std::launch::async, [&] { return parser.contains_timer_resource(right_mutex); });
    if (left_scan.wait_for(std::chrono::seconds(2)) != std::future_status::ready ||
        right_scan.wait_for(std::chrono::seconds(2)) != std::future_status::ready) std::_Exit(1);
    assert(!left_scan.get() && !right_scan.get());

    const auto cyclic_checkpoint = parser.begin_timer_operation();
    eval("type(timer())");
    auto cyclic_sweep = std::async(std::launch::async, [&] {
        parser.finish_timer_operation(cyclic_checkpoint);
    });
    if (cyclic_sweep.wait_for(std::chrono::seconds(2)) != std::future_status::ready) std::_Exit(1);
    cyclic_sweep.get();

    eval("t.start()");
    now += std::chrono::milliseconds(4);
    eval("t.start()");
    now += std::chrono::milliseconds(3);
    eval("t.elapsed()"); assert(value.num == 3);
    eval("t.reset()");
    eval("t.elapsed()"); assert(value.num == 0);
    eval("t.running()"); assert(!value.boolean);
    return 0;
}
