// Public-header consumer probe. Compiled with ONLY -Iinclude (see the
// test-public-header Makefile target), this proves the public Embedded Nift
// headers are self-contained: a consumer must not need -Isrc, must not see
// json::Document, Parser, RenderHost, ProjectInfo or other implementation
// machinery, and must not need to know Jsonic++ exists. It exercises every
// public type and member function so the whole surface compiles from a
// consumer's point of view.
#include <nift/nift.h>

#include <cstdio>
#include <type_traits>

// nift::Value move construction/assignment must be nothrow: the moved-from
// source becomes a valid Null Value without the move itself performing a
// potentially throwing allocation. The traits here prove nothrow directly;
// the allocation-free implementation is established by inspection.
static_assert(std::is_nothrow_move_constructible<nift::Value>::value,
              "nift::Value move construction must be nothrow");
static_assert(std::is_nothrow_move_assignable<nift::Value>::value,
              "nift::Value move assignment must be nothrow");

int main() {
    // Source variants.
    nift::Source path_source = nift::Source::path(std::filesystem::path("a.html"));
    nift::Source text_source = nift::Source::text(std::string("<p>x</p>"));
    nift::Source named_text = nift::Source::text(std::string("<p>y</p>"), std::string("tpl.html"));
    (void)path_source.is_path();
    (void)text_source.is_text();
    (void)named_text.path();
    (void)named_text.text();
    (void)named_text.logical_name();

    // Value surface.
    nift::Value null_value;
    nift::Value bool_value(true);
    nift::Value int_value(1);
    nift::Value double_value(2.5);
    nift::Value string_value(std::string("s"));
    nift::Value array = nift::Value::make_array();
    array.push_back(int_value);
    array[0] = string_value;
    nift::Value object = nift::Value::make_object();
    object["k"] = bool_value;
    nift::Value arr_member = nift::Value::make_array();
    arr_member.push_back(string_value);
    object["arr"] = arr_member;
    (void)null_value.is_null();
    (void)object.is_object();
    (void)array.is_array();
    (void)int_value.type();
    (void)int_value.number();
    (void)bool_value.boolean();
    (void)string_value.string();
    nift::Value copy = object;
    nift::Value moved = std::move(int_value);
    nift::Value assigned;
    assigned = object;
    (void)copy;
    (void)moved;
    (void)assigned;
    (void)null_value.is_null();

    // RenderError + RenderResult surface (populated through a render).
    nift::Engine engine;
    engine.set_root(std::filesystem::temp_directory_path());
    engine.set("title", std::string("T"));
    engine.set("count", 42);
    engine.set_environment_provider([](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    });
    engine.set_loader([](std::string_view) -> std::optional<std::string> { return std::nullopt; });

    nift::Context context;
    context.set_page_name(std::string("page"));
    context.set_current_output(std::filesystem::temp_directory_path() / "page.html");
    context.set_title(std::string("CT"));
    context.set("count", 7);
    context.set_json("user", R"({"name":"Nick"})");

    nift::RenderResult result = engine.render(
        nift::Source::text("<h1>hi</h1>"),
        nift::Source::text("<title>$[title]/$[count]/$[user.name]</title>@content"),
        context);
    nift::RenderResult partial = engine.render(nift::Source::text("<nav>N</nav>"), context);
    nift::RenderResult path_result =
        engine.render(nift::Source::path(std::filesystem::temp_directory_path() / "page.html"),
                      nift::Source::text("<html>@content</html>"), context);
    (void)result.ok();
    (void)result.output();
    (void)result.error().message;
    (void)result.error().source;
    (void)result.error().line;
    (void)result.error().column;
    (void)result.dependencies().size();
    (void)result.requirements().size();
    (void)partial.ok();
    (void)path_result.ok();

    // Engine overloads without context.
    nift::RenderResult r2 = engine.render(nift::Source::text("<p>p</p>"),
                                          nift::Source::text("<html>@content</html>"));
    nift::RenderResult r3 = engine.render(nift::Source::text("<p>p</p>"));
    (void)r2.ok();
    (void)r3.ok();

    // The documented structured-construction example must actually work: the
    // array member is constructed with make_array() and assigned before use.
    nift::Value user = nift::Value::make_object();
    user["name"] = nift::Value(std::string("Nick"));
    nift::Value projects = nift::Value::make_array();
    projects.push_back(nift::Value(std::string("nift")));
    projects.push_back(nift::Value(std::string("tscc")));
    user["projects"] = projects;
    nift::Engine doc_engine;
    doc_engine.set("user", user);
    nift::RenderResult doc_result = doc_engine.render(nift::Source::text("$[user.name]/$[user.projects[1]]"));
    if (!doc_result.ok() || doc_result.output() != "Nick/tscc") {
        std::fprintf(stderr, "public-header probe: documented value example did not render\n");
        return 1;
    }

    return 0;
}
