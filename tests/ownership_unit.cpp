// CP2 ownership unit test: cross-platform verification of the lock + marker
// protocol (ProjectOwnership). Runs on Linux/macOS/Windows (the fork/kill
// process-death case is POSIX-only; Windows CI evidence for that path is
// pending, see the CP2 report).
#include "ProjectOwnership.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(label, cond)                                                       \
    do {                                                                         \
        const bool ok = (cond);                                                  \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);                 \
        if (!ok) ++failures;                                                     \
    } while (0)

#ifndef _WIN32
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

int main() {
    // Portable unique suffix (no getpid/unistd dependency; MSYS2 UCRT exposes
    // getpid only incidentally): use the steady clock.
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / ("nift-ownership-unit-" + std::to_string(suffix));
    fs::create_directories(dir);
    const fs::path marker = dir / ".unfinished";

    // 1. Clean acquire -> marker created; finish removes it.
    {
        ProjectOwnership o(marker);
        const auto s = o.acquire();
        CHECK("clean acquire returns Clean", s == ProjectOwnership::State::Clean);
        CHECK("clean acquire creates the marker", fs::exists(marker));
        o.finish();
        CHECK("finish removes the marker", !fs::exists(marker));
    }

    // 2. Stale marker (no live owner) -> Stale; repair path may proceed.
    {
        { std::FILE* f = std::fopen(marker.string().c_str(), "w"); std::fclose(f); }
        ProjectOwnership o(marker);
        const auto s = o.acquire();
        CHECK("stale acquire returns Stale", s == ProjectOwnership::State::Stale);
        o.finish();
        CHECK("finish removes the stale marker", !fs::exists(marker));
    }

    // 3. Live lock: a second holder (separate open file description) conflicts.
    {
        ProjectOwnership a(marker);
        CHECK("first owner acquires", a.acquire() == ProjectOwnership::State::Clean);
        ProjectOwnership b(marker);
        CHECK("second owner is refused (Live)", b.acquire() == ProjectOwnership::State::Live);
        CHECK("live_owner_exists reports true while held",
              ProjectOwnership::live_owner_exists(marker));
        a.finish();
        CHECK("live_owner_exists reports false after finish",
              !ProjectOwnership::live_owner_exists(marker));
    }

    // 4. live_owner_exists with no marker at all.
    CHECK("live_owner_exists is false with no marker", !ProjectOwnership::live_owner_exists(marker));

    // 5. .nift/.lock is persistent, contains the explanatory sentence, and is
    //    never removed by finish(). An idle legacy .ownership-gate migrates.
    {
        const fs::path lock = dir / ".lock";
        const fs::path legacy = dir / ".ownership-gate";
        const char* lock_text =
            "Nift project lock. This persistent file is normal and does not indicate an active or failed build.\n";
        {
            ProjectOwnership o(marker);
            CHECK("acquire creates .nift/.lock", o.acquire() == ProjectOwnership::State::Clean && fs::exists(lock));
            std::FILE* f = std::fopen(lock.string().c_str(), "r");
            CHECK(".lock exists after acquire", f != nullptr);
            std::string contents;
            if (f) {
                char buf[256];
                std::size_t n = 0;
                while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) contents.append(buf, n);
                std::fclose(f);
            }
            CHECK(".lock contains the exact explanatory sentence", contents == std::string(lock_text));
            o.finish();
        }
        CHECK(".lock persists after finish()", fs::exists(lock));

        // Legacy migration: an idle .ownership-gate is removed only after the
        // new .lock is established; a build epoch still works.
        std::FILE* lg = std::fopen(legacy.string().c_str(), "w");
        std::fprintf(lg, "legacy\n");
        std::fclose(lg);
        {
            ProjectOwnership o(marker);
            CHECK("acquire succeeds while migrating an idle legacy gate",
                  o.acquire() == ProjectOwnership::State::Clean);
            CHECK("legacy .ownership-gate removed after migration", !fs::exists(legacy));
            CHECK("new .lock established", fs::exists(lock));
            o.finish();
        }

        // An empty .lock from an interrupted creation is populated while the
        // lock is held (the file content is repaired on the next acquisition).
        std::FILE* ef = std::fopen(lock.string().c_str(), "w");
        std::fclose(ef); // empty file, existing identity
        {
            ProjectOwnership o(marker);
            CHECK("acquire succeeds on an existing empty .lock",
                  o.acquire() == ProjectOwnership::State::Clean);
            o.finish();
        }
        std::FILE* rf = std::fopen(lock.string().c_str(), "r");
        std::string refilled;
        if (rf) {
            char buf[256];
            std::size_t n = 0;
            while ((n = std::fread(buf, 1, sizeof(buf), rf)) > 0) refilled.append(buf, n);
            std::fclose(rf);
        }
        CHECK("empty .lock is populated with the explanation on next acquire",
              refilled == std::string(lock_text));
    }

#ifndef _WIN32
    // 6. Process death releases the lock; the marker survives.
    {
        const pid_t child = ::fork();
        if (child == 0) {
            ProjectOwnership o(marker);
            if (o.acquire() != ProjectOwnership::State::Clean) ::_exit(2);
            ::pause(); // hold the lock + marker until killed
            ::_exit(0);
        }
        ::usleep(300000); // let the child acquire
        ProjectOwnership parent(marker);
        CHECK("parent is refused (Live) while child holds the lock",
              parent.acquire() == ProjectOwnership::State::Live);
        ::kill(child, SIGKILL);
        int status = 0;
        ::waitpid(child, &status, 0);
        CHECK("marker survives child SIGKILL", fs::exists(marker));
        CHECK("live_owner_exists false after child death",
              !ProjectOwnership::live_owner_exists(marker));
        ProjectOwnership repair(marker);
        CHECK("repair acquires Stale after child death",
              repair.acquire() == ProjectOwnership::State::Stale);
        repair.finish();
        CHECK("marker removed after repair", !fs::exists(marker));
    }
#else
    std::printf("  [SKIP] process-death path (POSIX fork/kill) not run on Windows\n");
#endif

    std::error_code ignored;
    fs::remove_all(dir, ignored);

    if (failures) {
        std::printf("\nFAILED: %d ownership unit checks\n", failures);
        return 1;
    }
    std::printf("\nALL OWNERSHIP UNIT TESTS PASSED\n");
    return 0;
}
