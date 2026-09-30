#include <nift/nift.h>

#include <cassert>
#include <chrono>
#include <string>

int main() {
    nift::Engine engine;

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
    return 0;
}
