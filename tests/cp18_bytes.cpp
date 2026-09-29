#include "nift/nift.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, \
    "cp18-bytes FAIL: %s (line %d)\n", #condition, __LINE__); ++failures; } } while (0)

std::uint8_t expected_byte(std::size_t index) {
    return static_cast<std::uint8_t>((index * 131u + 17u) & 0xffu);
}

bool exact_pattern(const nift::Value::Bytes& bytes) {
    if (bytes.size() != 8u * 1024u * 1024u) return false;
    for (std::size_t i = 0; i < bytes.size(); ++i)
        if (bytes[i] != expected_byte(i)) return false;
    return true;
}
} // namespace

int main() {
    nift::Value::Bytes storage(8u * 1024u * 1024u);
    for (std::size_t i = 0; i < storage.size(); ++i) storage[i] = expected_byte(i);
    nift::Value source(std::move(storage));
    const std::uint8_t* const backing = source.bytes().data();
    CHECK(backing != nullptr);
    CHECK(exact_pattern(source.bytes()));

    // Deterministic copy-amplification guard: independent Value shells must not
    // duplicate the immutable large backing.
    std::vector<nift::Value> copies(256, source);
    nift::Value assigned;
    assigned = source;
    for (const auto& copy : copies) {
        CHECK(copy.bytes().data() == backing);
        CHECK(exact_pattern(copy.bytes()));
    }
    CHECK(assigned.bytes().data() == backing);

    nift::Value aggregate = nift::Value::make_object();
    aggregate["payload"] = source;
    aggregate["marker"] = nift::Value(1);
    nift::Value aggregate_copy = aggregate;
    aggregate_copy["marker"] = nift::Value(2);

    nift::Engine engine;
    CHECK(engine.set("source", source));
    CHECK(engine.set("aggregate", aggregate));
    CHECK(engine.set("aggregate_copy", aggregate_copy));
    source = nift::Value();
    assigned = nift::Value();
    copies.clear();
    aggregate = nift::Value();
    aggregate_copy = nift::Value();

    std::atomic<unsigned> callback_reads{0};
    std::atomic<unsigned> barrier_entries{0};
    std::atomic<unsigned> active_reads{0};
    std::atomic<unsigned> max_active_reads{0};
    std::atomic<unsigned> completed_reads{0};
    std::atomic<bool> barrier_aborted{false};
    CHECK(engine.register_function("shared_bytes", [&](const std::vector<nift::Value>& args) {
        if (args.empty()) throw std::runtime_error("expected at least one byte value");
        for (const auto& value : args) {
            if (!value.is_bytes() || value.bytes().data() != backing ||
                !exact_pattern(value.bytes())) {
                throw std::runtime_error("bytes backing or contents changed after " +
                    std::to_string(callback_reads.load(std::memory_order_relaxed)) +
                    " successful reads");
            }
        }
        callback_reads.fetch_add(static_cast<unsigned>(args.size()), std::memory_order_relaxed);
        return nift::Value(true);
    }));
    CHECK(engine.register_function("concurrent_bytes", [&](const std::vector<nift::Value>& args) {
        const bool valid_value = args.size() == 1 && args[0].is_bytes() &&
            args[0].bytes().data() == backing;
        const unsigned active = active_reads.fetch_add(1, std::memory_order_seq_cst) + 1;
        unsigned maximum = max_active_reads.load(std::memory_order_seq_cst);
        while (maximum < active && !max_active_reads.compare_exchange_weak(
                   maximum, active, std::memory_order_seq_cst)) {}
        barrier_entries.fetch_add(1, std::memory_order_seq_cst);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (barrier_entries.load(std::memory_order_seq_cst) < 2 &&
               !barrier_aborted.load(std::memory_order_seq_cst)) {
            if (std::chrono::steady_clock::now() >= deadline) barrier_aborted.store(true);
            else std::this_thread::yield();
        }
        const bool valid_contents = valid_value && exact_pattern(args[0].bytes());
        completed_reads.fetch_add(1, std::memory_order_seq_cst);
        while (completed_reads.load(std::memory_order_seq_cst) < 2 &&
               !barrier_aborted.load(std::memory_order_seq_cst)) {
            if (std::chrono::steady_clock::now() >= deadline) barrier_aborted.store(true);
            else std::this_thread::yield();
        }
        active_reads.fetch_sub(1, std::memory_order_seq_cst);
        if (barrier_aborted.load(std::memory_order_seq_cst))
            throw std::runtime_error("concurrent bytes barrier timed out");
        if (!valid_contents)
            throw std::runtime_error("concurrent bytes backing or contents changed");
        return nift::Value(true);
    }));

    const char* script = R"NIFT(
fn(identity(x)) { return x }
fn(legacy_wrap(x)) { marker := 0; marker; return {"payload":[x]} }
fn(make_closure(x)) { local := x; result := () => local; return result }
fn(thread_read(x)) {
    i := 0
    while(i < 8) { shared_bytes(x); i += 1 }
    return x
}
fn(thread_nested(x)) { shared_bytes(x.payload[0]); return x }
fn(concurrent_read(x)) { concurrent_bytes(x); return x }
@fn[async](async_read(x)) { shared_bytes(x); return x }
shared_bytes(source)
shared_bytes(aggregate.payload, aggregate_copy.payload)
assigned := source
shared_bytes(assigned)
copied := copy(source)
deep := deepcopy({"payload":[source], "marker":1})
shared_bytes(copied, deep.payload[0])
deep["marker"] = 2
deep["payload"][0] = bytes([1])
shared_bytes(source)
direct := identity(source)
legacy := legacy_wrap(source)
shared_bytes(direct, legacy.payload[0])
closure := make_closure(source)
shared_bytes(closure())
nested_closure := () => { return {"payload":[source]} }
shared_bytes(nested_closure().payload[0])
m := mutex({"payload":source, "marker":1})
m.lock()
mv := m.get()
shared_bytes(mv.payload)
mv["marker"] = 2
mv["payload"] = bytes([2])
again := m.get()
shared_bytes(again.payload)
m.unlock()
m2 := mutex(source)
m2.lock()
shared_bytes(m2.get())
m2.unlock()
t1 := thread(thread_read, source)
t2 := thread(thread_nested, {"payload":[source]})
tr1 := t1.join()
tr2 := t2.join()
shared_bytes(tr1, tr2.payload[0])
ct1 := thread(concurrent_read, source)
ct2 := thread(concurrent_read, source)
shared_bytes(ct1.join(), ct2.join())
future1 := async_read(source)
async_closure := async (x) => { shared_bytes(x.payload[0]); return x }
future2 := async_closure({"payload":[source]})
ar1 := await future1
ar2 := await future2
shared_bytes(ar1, ar2.payload[0])
return [aggregate.marker, aggregate_copy.marker, again.marker, source == tr1, source == ar1]
)NIFT";

    auto result = engine.execute(script);
    CHECK(result.ok());
    if (!result.ok()) std::fprintf(stderr, "cp18 script error: %s\n", result.error().message.c_str());
    else CHECK(result.value().json() == "[\n1,\n2,\n1,\ntrue,\ntrue\n]");
    CHECK(callback_reads.load(std::memory_order_relaxed) == 31u);
    CHECK(barrier_entries.load(std::memory_order_seq_cst) == 2u);
    CHECK(completed_reads.load(std::memory_order_seq_cst) == 2u);
    CHECK(max_active_reads.load(std::memory_order_seq_cst) >= 2u);
    CHECK(!barrier_aborted.load(std::memory_order_seq_cst));

    nift::Engine rejection_engine;
    auto direct = rejection_engine.execute(
        "fn(identity(x)) { return x }\nreturn identity(bytes([0,255]))\n");
    CHECK(direct.ok());
    bool json_rejected = false;
    try { (void)direct.value().json(); }
    catch (const std::runtime_error& error) {
        json_rejected = std::string(error.what()) == "bytes values are not JSON serializable";
    }
    CHECK(json_rejected);
    auto print = nift::Engine().execute(
        "fn(identity(x)) { return x }\nprint(identity(bytes([0,255])))\n");
    CHECK(!print.ok());
    CHECK(print.error().message.find("cannot be rendered as text") != std::string::npos);
    auto nested = nift::Engine().execute(
        "fn(legacy_wrap(x)) { return {\"payload\":[x]} }\n"
        "return legacy_wrap(bytes([0,255]))\n");
    CHECK(nested.ok());
    json_rejected = false;
    try { (void)nested.value().json(); }
    catch (const std::runtime_error& error) {
        json_rejected = std::string(error.what()) == "bytes values are not JSON serializable";
    }
    CHECK(json_rejected);

    if (failures == 0) { std::printf("CP18 bytes transfer: PASS\n"); return 0; }
    std::fprintf(stderr, "CP18 bytes transfer: %d failure(s)\n", failures);
    return 1;
}
