// CP3: value bindings through Engine defaults and Context overlays, the
// structural built-in rule, and collision behaviour before the precedence
// contract is frozen. Exercises the same parser/evaluator the CLI uses.
#include "nift/nift.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "engine-bindings FAIL: %s (line %d)\n",     \
                         #cond, __LINE__);                                   \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

int main() {
    namespace fs = std::filesystem;

    // 1. Engine default binding renders through $[...].
    {
        nift::Engine engine;
        CHECK(engine.set("title", std::string("Site")));
        auto r = engine.render(nift::Source::text("<h1>hi</h1>"),
                               nift::Source::text("<title>$[title]</title>@content"));
        CHECK(r.ok());
        CHECK(r.output() == "<title>Site</title><h1>hi</h1>");
    }

    // 2. Context overlay wins over the Engine default for the same name.
    {
        nift::Engine engine;
        engine.set("title", std::string("Site"));
        nift::Context context;
        context.set("title", std::string("Request"));
        auto r = engine.render(nift::Source::text("<h1>hi</h1>"),
                               nift::Source::text("<title>$[title]</title>@content"), context);
        CHECK(r.ok());
        CHECK(r.output() == "<title>Request</title><h1>hi</h1>");
    }

    // 3. set_json + structured access.
    {
        nift::Engine engine;
        CHECK(engine.set_json("user", R"({"name":"Nick","role":"admin"})"));
        auto r = engine.render(nift::Source::text("<p>p</p>"),
                               nift::Source::text("$[user.name]:$[user.role]@content"));
        CHECK(r.ok());
        CHECK(r.output() == "Nick:admin<p>p</p>");
    }

    // 4. Context set_json.
    {
        nift::Engine engine;
        nift::Context context;
        CHECK(context.set_json("user", R"({"name":"Casey"})"));
        auto r = engine.render(nift::Source::text("<p>p</p>"),
                               nift::Source::text("$[user.name]@content"), context);
        CHECK(r.ok());
        CHECK(r.output() == "Casey<p>p</p>");
    }

    // 5. Structured nift::Value built from make_object/make_array.
    {
        nift::Engine engine;
        nift::Value user = nift::Value::make_object();
        user["name"] = nift::Value(std::string("Nick"));
        nift::Value projects = nift::Value::make_array();
        projects.push_back(nift::Value(std::string("nift")));
        projects.push_back(nift::Value(std::string("tscc")));
        user["projects"] = projects;
        CHECK(engine.set("user", user));
        auto r = engine.render(nift::Source::text("$[user.name]:$[user.projects[1]]"));
        CHECK(r.ok());
        CHECK(r.output() == "Nick:tscc");
    }

    // 6. @json refuses a name already supplied by the host.
    {
        const fs::path tmp = fs::temp_directory_path() / "nift-embed-bindings";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        {
            std::ofstream f(tmp / "data.json");
            f << R"({"value": 1})";
        }
        nift::Engine engine;
        engine.set_root(tmp);
        engine.set("user", std::string("x"));
        auto r = engine.render(nift::Source::text("<p>p</p>"),
                               nift::Source::text("@json(\"data.json\", \"user\")@content"));
        CHECK(!r.ok());
        CHECK(r.error().message.find("already bound") != std::string::npos);
        fs::remove_all(tmp);
    }

    // 7. Structural built-ins and invalid identifiers are rejected by set.
    {
        nift::Engine engine;
        CHECK(!engine.set("name", std::string("x")));
        CHECK(!engine.set("content-path", std::string("x")));
        CHECK(!engine.set("output-path", std::string("x")));
        CHECK(!engine.set("template-path", std::string("x")));
        CHECK(!engine.set("loop", nift::Value(1)));
        CHECK(!engine.set("not-an-identifier", std::string("x")));
        nift::Context context;
        CHECK(!context.set("name", std::string("x")));
        CHECK(!context.set("output-path", std::string("x")));
    }

    // 8. Hyphenated built-in metadata (build-date etc.) is not bindable: the
    //    value-expression grammar only accepts identifiers, so such names are
    //    rejected. The only identifier-shaped built-in metadata, title, is the
    //    overridable case (covered by tests 1-2).
    {
        nift::Engine engine;
        CHECK(!engine.set("build-date", std::string("2026-01-01")));
        CHECK(!engine.set("content-path", std::string("x")));
        CHECK(!engine.set("build-time", std::string("x")));
    }

    // 9. Scalar value types.
    {
        nift::Engine engine;
        engine.set("count", 42);
        engine.set("enabled", true);
        auto r = engine.render(nift::Source::text("$[count]/$[enabled]"));
        CHECK(r.ok());
        CHECK(r.output() == "42/true");
    }

    // 10. Title precedence matrix. Context::set_title and Context::set("title")
    //     write the same per-render slot and must both outrank an Engine
    //     default; the most recent Context write wins.
    {
        // Engine default + Context::set_title -> Context title wins.
        nift::Engine engine;
        engine.set("title", std::string("ENGINE"));
        nift::Context context;
        context.set_title(std::string("CONTEXT_TITLE"));
        auto r = engine.render(nift::Source::text("x"), nift::Source::text("$[title]@content"), context);
        CHECK(r.ok());
        CHECK(r.output() == "CONTEXT_TITLEx");
    }
    {
        // Engine default + Context::set("title") -> Context binding wins.
        nift::Engine engine;
        engine.set("title", std::string("ENGINE"));
        nift::Context context;
        context.set("title", std::string("CONTEXT_SET"));
        auto r = engine.render(nift::Source::text("x"), nift::Source::text("$[title]@content"), context);
        CHECK(r.ok());
        CHECK(r.output() == "CONTEXT_SETx");
    }
    {
        // Context::set_title then Context::set("title") -> later write wins.
        nift::Engine engine;
        nift::Context context;
        context.set_title(std::string("FIRST"));
        context.set("title", std::string("SECOND"));
        auto r = engine.render(nift::Source::text("x"), nift::Source::text("$[title]@content"), context);
        CHECK(r.ok());
        CHECK(r.output() == "SECONDx");
    }
    {
        // Context::set("title") then Context::set_title -> later write wins.
        nift::Engine engine;
        nift::Context context;
        context.set("title", std::string("FIRST"));
        context.set_title(std::string("SECOND"));
        auto r = engine.render(nift::Source::text("x"), nift::Source::text("$[title]@content"), context);
        CHECK(r.ok());
        CHECK(r.output() == "SECONDx");
    }
    {
        // No host title + Context::set_title -> Context title renders.
        nift::Engine engine;
        nift::Context context;
        context.set_title(std::string("ONLY_TITLE"));
        auto r = engine.render(nift::Source::text("x"), nift::Source::text("$[title]@content"), context);
        CHECK(r.ok());
        CHECK(r.output() == "ONLY_TITLEx");
    }
    {
        // No Context title + Engine default -> Engine default renders.
        nift::Engine engine;
        engine.set("title", std::string("ENGINE_ONLY"));
        auto r = engine.render(nift::Source::text("x"), nift::Source::text("$[title]@content"));
        CHECK(r.ok());
        CHECK(r.output() == "ENGINE_ONLYx");
    }

    // 11. Value copy semantics: copying a Value yields an independent
    //     equivalent value; mutation of either copy must not affect the other.
    {
        // Object copy then mutate member.
        nift::Value a = nift::Value::make_object();
        a["x"] = nift::Value(1);
        nift::Value b = a;
        b["x"] = nift::Value(2);
        nift::Context context;
        context.set("a", a);
        context.set("b", b);
        auto r = nift::Engine().render(nift::Source::text("$[a.x]/$[b.x]"), context);
        CHECK(r.ok());
        CHECK(r.output() == "1/2");
    }
    {
        // Array copy then mutate element.
        nift::Value arr = nift::Value::make_array();
        arr.push_back(nift::Value(10));
        arr.push_back(nift::Value(11));
        nift::Value copy = arr;
        copy[0] = nift::Value(20);
        nift::Context context;
        context.set("arr", arr);
        context.set("copy", copy);
        auto r = nift::Engine().render(nift::Source::text("$[arr[0]]/$[copy[0]]"), context);
        CHECK(r.ok());
        CHECK(r.output() == "10/20");
    }
    {
        // Copy assignment then mutate.
        nift::Value a = nift::Value::make_object();
        a["x"] = nift::Value(1);
        nift::Value b = nift::Value::make_object();
        b["y"] = nift::Value(9);
        b = a;
        b["x"] = nift::Value(2);
        nift::Context context;
        context.set("a", a);
        context.set("b", b);
        auto r = nift::Engine().render(nift::Source::text("$[a.x]/$[b.x]"), context);
        CHECK(r.ok());
        CHECK(r.output() == "1/2");
    }
    {
        // Original mutation after copy does not affect the copy.
        nift::Value a = nift::Value::make_object();
        a["x"] = nift::Value(1);
        nift::Value b = a;
        a["x"] = nift::Value(5);
        nift::Context context;
        context.set("a", a);
        context.set("b", b);
        auto r = nift::Engine().render(nift::Source::text("$[a.x]/$[b.x]"), context);
        CHECK(r.ok());
        CHECK(r.output() == "5/1");
    }
    {
        // Move construction: destination has the value, source is a valid Null.
        nift::Value a = nift::Value::make_object();
        a["x"] = nift::Value(7);
        nift::Value moved = std::move(a);
        CHECK(moved.is_object());
        CHECK(a.is_null());          // safe inspection of moved-from
        nift::Context context;
        context.set("moved", moved);
        auto r = nift::Engine().render(nift::Source::text("$[moved.x]"), context);
        CHECK(r.ok());
        CHECK(r.output() == "7");
    }
    {
        // Move construction: moved-from source can be copied and reused.
        nift::Value a = nift::Value::make_object();
        a["x"] = nift::Value(7);
        nift::Value moved = std::move(a);
        nift::Value copied_from_moved_from = a;   // copy of moved-from (Null) is valid
        CHECK(copied_from_moved_from.is_null());
        a = nift::Value::make_object();            // reassign and reuse
        a["y"] = nift::Value(9);
        nift::Context context;
        context.set("a", a);
        context.set("moved", moved);
        auto r = nift::Engine().render(nift::Source::text("$[a.y]/$[moved.x]"), context);
        CHECK(r.ok());
        CHECK(r.output() == "9/7");
    }
    {
        // Move assignment: destination takes value, source becomes valid Null.
        nift::Value a = nift::Value::make_object();
        a["x"] = nift::Value(1);
        nift::Value b = nift::Value::make_object();
        b["y"] = nift::Value(2);
        b = std::move(a);
        CHECK(b.is_object());
        CHECK(a.is_null());
        nift::Context context;
        context.set("b", b);
        auto r = nift::Engine().render(nift::Source::text("$[b.x]"), context);
        CHECK(r.ok());
        CHECK(r.output() == "1");
    }
    {
        // Move assignment: source can be assigned a new value and used.
        nift::Value a = nift::Value::make_object();
        a["x"] = nift::Value(1);
        nift::Value b = nift::Value::make_object();
        b = std::move(a);
        CHECK(b.is_object());
        a["z"] = nift::Value(3);     // moved-from source accepts mutation
        CHECK(a.is_object());
        nift::Context context;
        context.set("a", a);
        context.set("b", b);
        auto r = nift::Engine().render(nift::Source::text("$[a.z]/$[b.x]"), context);
        CHECK(r.ok());
        CHECK(r.output() == "3/1");
    }

    // 12. Null internal representation (impl_ == nullptr) paths: default and
    //     moved-from values behave as Null through reads and normal APIs.
    {
        nift::Value v;
        CHECK(v.is_null());
        CHECK(v.type() == nift::Value::Type::Null);
        CHECK(v.string().empty());
    }
    {
        // push_back of a Null value is safe and renders empty.
        nift::Value array = nift::Value::make_array();
        array.push_back(nift::Value(std::string("a")));
        array.push_back(nift::Value());
        nift::Context context;
        context.set("array", array);
        auto r = nift::Engine().render(nift::Source::text("$[array[0]]/$[array[1]]"), context);
        CHECK(r.ok());
        CHECK(r.output() == "a/null");
    }
    {
        // A moved-from (Null) value is safe through Context/Engine.
        nift::Value a = nift::Value::make_object();
        a["x"] = nift::Value(1);
        nift::Value moved = std::move(a);
        nift::Context context;
        context.set("moved", moved);
        context.set("empty", a);   // the moved-from source, bound as a value
        auto r = nift::Engine().render(nift::Source::text("$[moved.x]/$[empty]"), context);
        CHECK(r.ok());
        CHECK(r.output() == "1/null");
    }

    if (failures == 0) {
        std::printf("engine bindings test passed\n");
        return 0;
    }
    std::fprintf(stderr, "engine bindings test: %d failure(s)\n", failures);
    return 1;
}
