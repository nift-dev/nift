// L6 `native::` (D-075 #2, D-095) through the embedding API (nift::Engine runs the legacy evaluator): the qualified
// seven work; the unqualified seven keep their legacy meaning; a host function named like one of them does not
// capture `native::<name>`; a name outside the seven is an error. Run in a fresh temporary directory (argv[1]).
#include "nift/nift.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

static int failures = 0;
#define CHECK(cond)                                                                                     \
    do {                                                                                                \
        if (!(cond)) {                                                                                  \
            std::printf("FAIL native-qualifier embed: %s (line %d)\n", #cond, __LINE__);                \
            ++failures;                                                                                 \
        }                                                                                               \
    } while (0)

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    if (argc != 2) return 2;
    fs::current_path(argv[1]);
    { std::ofstream("a.txt") << "A"; }
    nift::Engine engine;
    bool host_called = false;
    CHECK(engine.register_function("cp", [&](const std::vector<nift::Value>&) {
        host_called = true;
        return nift::Value();
    }));
    auto qualified = engine.execute("native::touch(\"e1\")\nnative::cp(\"a.txt\", \"b.txt\")\nreturn 1\n");
    CHECK(qualified.ok());
    CHECK(fs::exists("e1") && fs::exists("b.txt"));
    CHECK(!host_called);  // the host function named cp does not capture native::cp
    auto unqualified = engine.execute("touch(\"e2\")\nreturn 1\n");
    CHECK(unqualified.ok() && fs::exists("e2"));  // legacy keeps the unqualified seven
    auto nested = engine.execute("native::cat(native::ls(\".\")[0])\nreturn 1\n");
    CHECK(nested.ok());
    auto unknown = engine.execute("native::print(\"x\")\nreturn 1\n");
    CHECK(!unknown.ok());
    CHECK(unknown.error().message.find("native::print is not a native function") != std::string::npos);
    CHECK(unknown.stdout_output().find('x') == std::string::npos);
    auto spaced = engine.execute("native :: touch(\"e3\")\nreturn 1\n");
    CHECK(!spaced.ok() && !fs::exists("e3"));  // rejected, nothing ran (no prevalidation here: legacy's own text)
    if (failures == 0) std::printf("PASS native-qualifier embed: 10 checks of the embedding path\n");
    return failures == 0 ? 0 : 1;
}
