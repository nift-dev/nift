// CP2 ownership unit test: cross-platform verification of the lock + marker
// protocol (ProjectOwnership). Runs on Linux/macOS/Windows (the fork/kill
// process-death case is POSIX-only; Windows CI evidence for that path is
// pending, see the CP2 report).
#include "ProjectOwnership.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

static void set_test_env(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    ::setenv(name, value, 1);
#endif
}

static void clear_test_env(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    ::unsetenv(name);
#endif
}

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

        // Existing non-empty .lock retains its identity and contents.
        const fs::path identity_lock = dir / ".identity.lock";
        {
            std::FILE* f = std::fopen(identity_lock.string().c_str(), "w");
            std::fprintf(f, "custom persistent content\n");
            std::fclose(f);
        }
        // Point a second marker at the same directory so .lock is the identity
        // file: assert the pre-existing content is untouched by an acquire.
        const fs::path marker2 = dir / ".identity-marker";
        const fs::path existing_lock = dir / ".lock";
        {
            std::FILE* f = std::fopen(existing_lock.string().c_str(), "w");
            std::fprintf(f, "custom existing lock content\n");
            std::fclose(f);
            const fs::path marker3 = dir / ".identity-marker2";
            (void)marker3;
            ProjectOwnership o(marker2);
            CHECK("acquire succeeds with a pre-existing non-empty .lock",
                  o.acquire() == ProjectOwnership::State::Clean);
            std::FILE* rf = std::fopen(existing_lock.string().c_str(), "r");
            std::string before;
            if (rf) {
                char buf[256];
                std::size_t n = 0;
                while ((n = std::fread(buf, 1, sizeof(buf), rf)) > 0) before.append(buf, n);
                std::fclose(rf);
            }
            CHECK("pre-existing non-empty .lock contents are untouched",
                  before == "custom existing lock content\n");
            o.finish();
        }
        std::error_code remove_ec;
        std::filesystem::remove(identity_lock, remove_ec);

        // Injected explanation-write failure: acquire refuses before creating
        // or classifying .unfinished, and no truncated lock is left.
        {
            std::FILE* f = std::fopen(lock.string().c_str(), "w");
            std::fclose(f); // empty file
            set_test_env("NIFT_TEST_LOCK_WRITE_FAIL", "err");
            ProjectOwnership o(marker);
            CHECK("explanation-write failure refuses (Failed)",
                  o.acquire() == ProjectOwnership::State::Failed);
            clear_test_env("NIFT_TEST_LOCK_WRITE_FAIL");
            CHECK("no .unfinished after explanation-write failure", !fs::exists(marker));
            CHECK(".lock still exists after explanation-write failure", fs::exists(lock));
        }

        // Injected partial-write failure: acquire refuses, .unfinished is never
        // created, and the truncated non-empty .lock is retained untouched.
        {
            std::FILE* f = std::fopen(lock.string().c_str(), "w");
            std::fclose(f); // empty file
            set_test_env("NIFT_TEST_LOCK_WRITE_FAIL", "partial");
            ProjectOwnership o(marker);
            CHECK("partial-write failure refuses (Failed)",
                  o.acquire() == ProjectOwnership::State::Failed);
            clear_test_env("NIFT_TEST_LOCK_WRITE_FAIL");
            CHECK("no .unfinished after partial-write failure", !fs::exists(marker));
            std::FILE* pf = std::fopen(lock.string().c_str(), "r");
            std::string partial;
            if (pf) {
                char buf[256];
                std::size_t n = 0;
                while ((n = std::fread(buf, 1, sizeof(buf), pf)) > 0) partial.append(buf, n);
                std::fclose(pf);
            }
            CHECK("truncated non-empty .lock is retained after partial-write failure",
                  !partial.empty() && partial.size() < std::strlen(lock_text));
        }

        // Injected legacy-removal failure: acquire refuses, .unfinished is
        // never created, and both files are retained for diagnosis/retry.
        {
            std::FILE* lg = std::fopen(legacy.string().c_str(), "w");
            std::fprintf(lg, "legacy\n");
            std::fclose(lg);
            std::FILE* f = std::fopen(lock.string().c_str(), "w");
            std::fclose(f); // empty .lock
            set_test_env("NIFT_TEST_LOCK_LEGACY_REMOVE_FAIL", "1");
            ProjectOwnership o(marker);
            CHECK("legacy-removal failure refuses (Failed)",
                  o.acquire() == ProjectOwnership::State::Failed);
            clear_test_env("NIFT_TEST_LOCK_LEGACY_REMOVE_FAIL");
            CHECK("no .unfinished after legacy-removal failure", !fs::exists(marker));
            CHECK("legacy .ownership-gate retained after removal failure", fs::exists(legacy));
            CHECK(".lock retained after legacy-removal failure", fs::exists(lock));
        }
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
