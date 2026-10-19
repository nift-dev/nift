#pragma once
// Shared fixtures for the CP17 handle-table tests.
#include "rt/handles/table.h"

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace handle_test {
using namespace nift::rt;
using namespace nift::rt::handles;

struct Res {
    int serial;
    explicit Res(int s) : serial(s) {}
};
struct Other { int x = 0; };

// Records every hook call (handle slot, reason) in order.
struct Log {
    std::vector<std::string> events;
    std::map<int, int> cleaned;  // serial -> times cleaned
};

inline KindSpec logging_kind(const char* name, Log& log) {
    KindSpec spec;
    spec.name = name;
    spec.cleanup = [&log](const CleanupEvent& e) {
        const int serial = static_cast<Res*>(e.resource.get())->serial;
        log.events.push_back(std::to_string(serial) + ":" + release_reason_name(e.reason));
        ++log.cleaned[serial];
        if (serial >= 9000 && serial < 9100) return CleanupOutcome::failure("process.kill_failed", "kill refused");
        if (serial >= 9100 && serial < 9200) throw std::runtime_error("hook threw");
        if (serial >= 9200) throw 42;
        return CleanupOutcome::success();
    };
    return spec;
}

inline std::shared_ptr<void> res(int serial) { return std::make_shared<Res>(serial); }

inline int serial_of(HandleTable& t, const Value& h, HandleKind k, ContextId accessor) {
    std::shared_ptr<Res> r;
    return t.resolve_as<Res>(h, k, accessor, r) == HandleStatus::Ok ? r->serial : -1;
}

} // namespace handle_test
