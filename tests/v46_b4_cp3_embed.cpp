#include <nift/nift.h>

#include <cassert>
#include <string>

int main() {
    nift::Engine engine;
    auto loaded = engine.execute(R"NIFT(
fn(direct_error()) { return error("direct") }
fn(array_error()) { return [error("array")] }
fn(object_error()) { return {"nested": error("object")} }
fn(collection_error()) { values := stack(); values.push(error("collection")); return values }
struct(holder) { value := error("struct") }
fn(struct_error()) { return holder() }
return 0
)NIFT");
    assert(loaded.ok());

    auto direct_eval = engine.evaluate("error(\"eval\")");
    assert(!direct_eval.ok());
    assert(direct_eval.error().message.find("Error value") != std::string::npos);
    auto nested_eval = engine.evaluate("[error(\"nested\")]");
    assert(!nested_eval.ok());

    for (const char* name : {"direct_error", "array_error", "object_error",
                             "collection_error", "struct_error"}) {
        auto result = engine.call(name, {});
        assert(!result.ok());
        assert(result.error().message.find("Error value") != std::string::npos);
    }
}
