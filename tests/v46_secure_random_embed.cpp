#include <nift/nift.h>

#include <cassert>
#include <string>

int main() {
    nift::Engine engine;
    auto empty = engine.evaluate("secure_random_bytes(0)");
    assert(empty.ok() && empty.value().is_bytes() && empty.value().bytes().empty());
    auto bytes = engine.evaluate("secure_random_bytes(64)");
    assert(bytes.ok() && bytes.value().is_bytes() && bytes.value().bytes().size() == 64);
    auto invalid = engine.evaluate("secure_random_bytes(10000001)");
    assert(!invalid.ok());
    assert(invalid.error().message.find("exceeds") != std::string::npos);
    return 0;
}
