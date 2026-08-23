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

    if (failures == 0) {
        std::printf("engine bindings test passed\n");
        return 0;
    }
    std::fprintf(stderr, "engine bindings test: %d failure(s)\n", failures);
    return 1;
}
