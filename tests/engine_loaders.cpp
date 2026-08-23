// CP4: loader semantics. A custom engine loader supplies @input/@content/
// @json/@dep sources from memory (no filesystem), and an environment provider
// supplies @getenv. Default (filesystem) behaviour and the CLI are unchanged.
#include "nift/nift.h"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "engine-loaders FAIL: %s (line %d)\n",      \
                         #cond, __LINE__);                                   \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

// Loader keyed on the resolved path's final component; returns nullopt for
// anything it does not know.
static std::function<std::optional<std::string>(std::string_view)> memory_loader(
    const std::unordered_map<std::string, std::string>& sources) {
    return [sources](std::string_view path) -> std::optional<std::string> {
        const std::string key(path);
        const std::size_t slash = key.find_last_of("/\\");
        const std::string base = slash == std::string::npos ? key : key.substr(slash + 1);
        const auto it = sources.find(base);
        if (it == sources.end()) return std::nullopt;
        return it->second;
    };
}

int main() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "nift-engine-loaders";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    // 1. @input through a memory loader (no filesystem needed).
    {
        nift::Engine engine;
        engine.set_root(root);
        engine.set_loader(memory_loader({{"nav.html", "<nav>L</nav>"}}));
        auto r = engine.render(nift::Source::text("<main>m</main>"),
                               nift::Source::text("<body>@input(\"nav.html\")@content</body>"));
        CHECK(r.ok());
        CHECK(r.output() == "<body><nav>L</nav><main>m</main></body>");
    }

    // 2. @json through a memory loader.
    {
        nift::Engine engine;
        engine.set_root(root);
        engine.set_loader(memory_loader({{"data.json", R"({"value": 7})"}}));
        auto r = engine.render(nift::Source::text("@json(\"data.json\", \"data\")$[data.value]"));
        CHECK(r.ok());
        CHECK(r.output() == "7");
    }

    // 3. @dep through a memory loader (declared, exists) and its dependency
    //    appears in the result.
    {
        nift::Engine engine;
        engine.set_root(root);
        engine.set_loader(memory_loader({{"config.txt", "x"}}));
        auto r = engine.render(nift::Source::text("@dep(\"config.txt\")ok"));
        CHECK(r.ok());
        CHECK(r.output() == "ok");
        CHECK(r.dependencies().size() == 1);
    }

    // 4. @dep missing through the loader is a controlled error.
    {
        nift::Engine engine;
        engine.set_root(root);
        engine.set_loader(memory_loader({}));
        auto r = engine.render(nift::Source::text("@dep(\"missing.txt\")"));
        CHECK(!r.ok());
        CHECK(r.error().message.find("dependency") != std::string::npos ||
              r.error().message.find("does not exist") != std::string::npos);
    }

    // 5. Environment provider for @getenv.
    {
        nift::Engine engine;
        engine.set_environment_provider([](std::string_view name) -> std::optional<std::string> {
            if (name == "APP_MODE") return std::string("server");
            return std::nullopt;
        });
        auto r = engine.render(nift::Source::text("@getenv('APP_MODE')/@getenv('UNSET')"));
        CHECK(r.ok());
        CHECK(r.output() == "server/");
    }

    std::filesystem::remove_all(root);

    // 6. @input missing through the loader is a controlled error.
    {
        nift::Engine engine;
        engine.set_root(root);
        engine.set_loader(memory_loader({}));
        auto r = engine.render(nift::Source::text("<p>p</p>"),
                               nift::Source::text("@input(\"nav.html\")@content"));
        CHECK(!r.ok());
        CHECK(r.error().message.find("does not exist") != std::string::npos);
    }

    // 7. Custom loader does not break @content composition from text/page.
    {
        nift::Engine engine;
        engine.set_root(root);
        engine.set_loader(memory_loader({}));
        auto r = engine.render(nift::Source::text("<main>hello</main>"),
                               nift::Source::text("<html>@content</html>"));
        CHECK(r.ok());
        CHECK(r.output() == "<html><main>hello</main></html>");
    }

    if (failures == 0) {
        std::printf("engine loaders test passed\n");
        return 0;
    }
    std::fprintf(stderr, "engine loaders test: %d failure(s)\n", failures);
    return 1;
}
