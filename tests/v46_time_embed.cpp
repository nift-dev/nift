#include <nift/nift.h>
#include "ValueInternal.h"

#include <cassert>
#include <chrono>
#include <string>

int main() {
    nift::Engine engine;
    bool host_called = false;
    assert(engine.register_function("timer_probe", [&](const std::vector<nift::Value>&) {
        host_called = true;
        return nift::Value(nullptr);
    }));

    auto epoch = engine.evaluate("epoch()");
    assert(epoch.ok() && epoch.value().is_number());
    assert(epoch.value().number() > 1000000000000.0);

    const auto start = std::chrono::steady_clock::now();
    auto slept = engine.evaluate("sleep(20)");
    const auto elapsed = std::chrono::steady_clock::now() - start;
    assert(slept.ok() && slept.value().is_null());
    assert(elapsed >= std::chrono::milliseconds(10));

    auto invalid = engine.evaluate("sleep(-1)");
    assert(!invalid.ok());
    assert(invalid.error().message.find("non-negative") != std::string::npos);

    auto timer = engine.execute("t := timer(); t.start(); sleep(1); t.stop(); return [type(t), type(t.elapsed()), t.running(), t.paused()]");
    assert(timer.ok() && timer.value().is_array());
    assert(timer.value().json() == "[\n\"timer\",\n\"int\",\nfalse,\nfalse\n]");

    auto exposed = engine.evaluate("timer()");
    assert(!exposed.ok());
    assert(exposed.error().message.find("timer value") != std::string::npos);
    auto returned = engine.execute("return {\"nested\": [timer()]}");
    assert(!returned.ok());
    assert(returned.error().message.find("timer") != std::string::npos);
    auto host_transfer = engine.evaluate("timer_probe(timer())");
    assert(!host_transfer.ok() && !host_called);
    assert(host_transfer.error().message.find("timer value") != std::string::npos);
    auto collection_host_transfer = engine.execute(
        "m := map(); m.set(\"timer\", timer()); timer_probe(m)");
    assert(!collection_host_transfer.ok() && !host_called);
    assert(collection_host_transfer.error().message.find("timer value") != std::string::npos);

    for (int i = 0; i < 128; ++i) {
        auto temporary = i % 2 == 0 ? engine.evaluate("timer()") : engine.evaluate("type(timer())");
        if (i % 2 == 0) assert(!temporary.ok() && temporary.error().message.find("timer value") != std::string::npos);
        else assert(temporary.ok() && temporary.value().string() == "timer");
    }
    assert(engine.evaluate("1 + 1").ok());

    auto persistent_return = engine.execute("persistent_timer := timer(); return persistent_timer");
    assert(!persistent_return.ok());
    auto persistent_elapsed = engine.evaluate("persistent_timer.elapsed()");
    assert(persistent_elapsed.ok() && persistent_elapsed.value().is_number());

    auto collection_alias = engine.execute(
        "timer_map := map(); timer_map.set(\"saved\", timer()); return timer_map");
    assert(!collection_alias.ok());
    auto collection_elapsed = engine.evaluate("timer_map.get(\"saved\").elapsed()");
    assert(collection_elapsed.ok() && collection_elapsed.value().is_number());

    auto captured_alias = engine.execute(
        "fn(make_timer_reader()) { captured_timer := timer(); return () => captured_timer }; "
        "timer_reader := make_timer_reader()");
    assert(captured_alias.ok());
    auto captured_elapsed = engine.evaluate("timer_reader().elapsed()");
    assert(captured_elapsed.ok() && captured_elapsed.value().is_number());

    nift::Value foreign_timer;
    nift::ValueAccess::runtime(foreign_timer) = nift::RuntimeValue::make_timer(999, 1);
    assert(!engine.set("foreign_timer", foreign_timer));
    nift::Context context;
    assert(!context.set("foreign_timer", foreign_timer));
    auto argument_transfer = engine.call("missing", {foreign_timer});
    assert(!argument_transfer.ok());
    assert(argument_transfer.error().message.find("reserved runtime value") != std::string::npos);

    assert(engine.register_function("return_foreign_timer", [foreign_timer](const std::vector<nift::Value>&) {
        return foreign_timer;
    }));
    auto host_return = engine.evaluate("return_foreign_timer()");
    assert(!host_return.ok());
    assert(host_return.error().message.find("timer") != std::string::npos);

    const std::string legacy_marker("\x1fnift:timer:1");
    assert(engine.register_function("return_legacy_timer_marker", [legacy_marker](const std::vector<nift::Value>&) {
        return nift::Value(legacy_marker);
    }));
    auto blocked_marker = engine.evaluate("return_legacy_timer_marker()");
    assert(!blocked_marker.ok());
    assert(blocked_marker.error().message.find("reserved runtime value") != std::string::npos);

    nift::Value forged_collection(std::string("\x1fnift:collection:1"));
    assert(!engine.set("forged_collection", forged_collection));
    assert(!context.set("forged_collection", forged_collection));
    nift::Engine other_engine;
    assert(!other_engine.set("forged_collection", forged_collection));
    nift::Value nested_forgery = nift::Value::make_array();
    nested_forgery.push_back(forged_collection);
    assert(!engine.set("nested_forgery", nested_forgery));
    assert(!engine.set_json("json_forgery", "[\"\\u001fnift:collection:1\"]"));
    assert(!context.set_json("json_forgery", "[\"\\u001fnift:collection:1\"]"));

    host_called = false;
    assert(engine.register_function("marker_probe", [&](const std::vector<nift::Value>&) {
        host_called = true;
        return nift::Value(nullptr);
    }));
    assert(!engine.call("marker_probe", {nested_forgery}).ok());
    assert(!host_called);

    auto cleanup_failure = engine.execute(
        "f := file(\"timer-cleanup.txt\"); f.open(\"w\"); f.write(\"dirty\"); print(timer())");
    assert(!cleanup_failure.ok());
    assert(cleanup_failure.error().message.find("not directly renderable") != std::string::npos);
    assert(engine.evaluate("f.open(\"w\")").ok());
    assert(engine.evaluate("f.revert()").ok());
    assert(engine.evaluate("f.close()").ok());

    auto define_evaluate_file = engine.execute(
        "fn(leak_evaluate_file()) { local := file(\"engine-evaluate-local.txt\"); "
        "local.open(\"w\"); return timer() }");
    assert(define_evaluate_file.ok());
    auto failed_evaluate_file = engine.evaluate("leak_evaluate_file()");
    assert(!failed_evaluate_file.ok());
    assert(failed_evaluate_file.error().message.find("timer") != std::string::npos);
    assert(engine.execute("return 1").ok());

    auto define_call_file = engine.execute(
        "fn(leak_call_file()) { local := file(\"engine-call-local.txt\"); "
        "local.open(\"w\"); return timer() }");
    assert(define_call_file.ok());
    auto failed_call_file = engine.call("leak_call_file");
    assert(!failed_call_file.ok());
    assert(failed_call_file.error().message.find("timer") != std::string::npos);
    assert(engine.execute("return 1").ok());
    return 0;
}
