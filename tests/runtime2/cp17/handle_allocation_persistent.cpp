// CP17 rework (F2): allocation failure that persists, through the program API.
//
// handle_allocation.cpp fails one allocation per site. Here allocation `i`
// fails and so do the next `window` allocations (window -1: every later
// allocation of the run), as an exhausted process would see. The independent
// review (F2) found that a second consecutive failure aborted the process
// (std::terminate) from ~HandleTable, whose shutdown() allocates, even for
// programs that never create a handle (every VM owns a handle table).
//
// Each site runs in a forked child so that an abort or a hang is observed
// instead of ending the campaign. Per site the child must: return
// NIFT_ERROR_INTERNAL from the failing run (never a false success, never
// another classification), then, with
// allocation restored, run the same program again and get exactly the
// baseline status, outcome, output, message and code (clean subsequent
// execution). Verdicts:
// contained, FALSE-SUCCESS, RERUN-DIFFERS, CRASH (signal), HANG (alarm).
// Programs: handle-free, try/catch, closed and left-open files, a leak
// completed before a caught Error (in a statement and in a loop), a
// dropped-open file, an unwound frame, written files, input and output
// streams. Scripts cannot hold process resources on the VM in CP17
// (refused before execution), so none is
// listed; the environment natives exercise CP16's HostCall.
//
// Derived from the independent reviewer's adv/persist_alloc.cpp
// (CP17-N2-COMBINED-INDEPENDENT-REVIEW.md, F2).
#include "nift/c_abi.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {
long remaining = -1;  // countdown to the first failure
long window = 0;      // further failures after the first (-1: every one)
bool failing = false, failed = false;
}  // namespace

void* operator new(std::size_t bytes) {
    if (failing) {
        if (window == 0) failing = false;
        else {
            if (window > 0) --window;
            throw std::bad_alloc();
        }
    }
    if (remaining == 0) {
        remaining = -1;
        failed = true;
        failing = true;
        throw std::bad_alloc();
    }
    if (remaining > 0) --remaining;
    if (void* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

int bad = 0;

struct Outcome {
    nift_status status = NIFT_OK;
    int outcome = -1;
    std::string output, message, code;
    bool operator==(const Outcome& o) const {
        return status == o.status && outcome == o.outcome && output == o.output && message == o.message &&
               code == o.code;
    }
};

std::string text(nift_string s) { return s.data ? std::string(s.data, s.length) : std::string(); }

Outcome run_once(nift_program* program) {
    Outcome o;
    nift_program_result* r = nullptr;
    o.status = nift_program_run(program, 0, 0, 0, &r);
    if (r) {
        o.outcome = static_cast<int>(nift_program_result_outcome(r));
        nift_string v{};
        if (nift_program_result_output(r, &v) == NIFT_OK) o.output = text(v);
        if (nift_program_result_error_message(r, &v) == NIFT_OK) o.message = text(v);
        if (nift_program_result_error_code(r, &v) == NIFT_OK) o.code = text(v);
        nift_program_result_free(r);
    }
    return o;
}

void campaign(const char* name, const std::string& text, long win) {
    nift_source source = {NIFT_SOURCE_TEXT, text.c_str(), text.size(), "h.f", 3};
    nift_program* program = nullptr;
    nift_program_result* compiled = nullptr;
    if (nift_program_compile(&source, &program, &compiled) != NIFT_OK) {
        std::printf("FAIL %s: compile failed\n", name);
        nift_program_result_free(compiled);
        ++bad;
        return;
    }
    nift_program_result_free(compiled);
    const Outcome baseline = run_once(program);
    int counts[6] = {0, 0, 0, 0, 0, 0};  // contained, false success, rerun differs, crash, hang, misclassified
    const char* one = std::getenv("ONE_SITE");  // debugging: run one site in this process
    for (long i = one ? std::atol(one) : 0; i < 50000; ++i) {
        std::fflush(stdout);
        const pid_t pid = one ? 0 : fork();
        if (pid == 0) {
            alarm(30);
            window = win;
            failed = false;
            remaining = i;
            nift_program_result* r = nullptr;
            const nift_status st = nift_program_run(program, 0, 0, 0, &r);
            nift_program_result_free(r);
            remaining = -1;
            failing = false;
            window = 0;
            if (!failed) _exit(20);  // no site reached: campaign complete
            if (st == NIFT_OK) _exit(10);
            if (st != NIFT_ERROR_INTERNAL) _exit(12);  // the allocation failure is the primary error
            const Outcome again = run_once(program);
            _exit(again == baseline ? 0 : 11);
        }
        int ws = 0;
        waitpid(pid, &ws, 0);
        if (WIFEXITED(ws) && WEXITSTATUS(ws) == 20) {
            const bool failed_any = counts[1] || counts[2] || counts[3] || counts[4] || counts[5] || i == 0;
            std::printf("%s %s window=%ld: %ld sites contained=%d false_success=%d rerun_differs=%d crash=%d hang=%d "
                        "misclassified=%d\n",
                        failed_any ? "FAIL" : "ok  ", name, win, i, counts[0], counts[1], counts[2], counts[3],
                        counts[4], counts[5]);
            if (failed_any) ++bad;
            nift_program_free(program);
            return;
        }
        if (WIFEXITED(ws) && WEXITSTATUS(ws) == 0) ++counts[0];
        else if (WIFEXITED(ws) && WEXITSTATUS(ws) == 10) {
            ++counts[1];
            std::printf("FALSE-SUCCESS %s window=%ld site=%ld\n", name, win, i);
        } else if (WIFEXITED(ws) && WEXITSTATUS(ws) == 11) {
            ++counts[2];
            std::printf("RERUN-DIFFERS %s window=%ld site=%ld\n", name, win, i);
        } else if (WIFEXITED(ws) && WEXITSTATUS(ws) == 12) {
            ++counts[5];
            std::printf("MISCLASSIFIED %s window=%ld site=%ld\n", name, win, i);
        } else if (WIFSIGNALED(ws) && WTERMSIG(ws) == SIGALRM) {
            ++counts[4];
            std::printf("HANG %s window=%ld site=%ld\n", name, win, i);
        } else {
            ++counts[3];
            std::printf("CRASH %s window=%ld site=%ld status=%d\n", name, win, i, ws);
        }
    }
    std::printf("FAIL %s: did not complete within 50000 sites\n", name);
    nift_program_free(program);
    ++bad;
}

}  // namespace

int main(int argc, char** argv) {
    const auto dir = std::filesystem::temp_directory_path() / ("cp17-persist-" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    std::filesystem::current_path(dir);
    std::ofstream("p") << "one\ntwo\n";
    std::ofstream("q") << "q\n";
    const std::string leak = "fn(leak()) { g := file(\"q\"); g.open(\"r\"); return 1 }\nfn(work()) { throw error(\"s\") }\n";
    const struct {
        const char* name;
        std::string text;
    } programs[] = {
        {"plain-no-handles", "x := \"ab\"\nprint(x + \"c\")\n"},
        {"try-no-handles", "try { throw error(\"s\") } catch(e) { print(e.message) }\n"},
        {"environment", "print(getenv(\"CP17_PERSIST_UNSET\") == \"\")\n"},
        {"closed", "f := file(\"p\")\nf.open(\"r\")\nprint(f.read_line())\nf.close()\n"},
        {"left-open", "f := file(\"p\")\nf.open(\"r\")\nprint(f.read_line())\n"},
        {"caught-after-leak", leak + "try { x := leak() + work() } catch(e) { print(e.message) }\n"},
        {"caught-after-leak-loop", leak + "i := 0\ntry { while (i < 1) { i = i + 1; x := leak() + work() } } catch(e) { print(e.message) }\n"},
        {"dropped-open", leak + "print(leak())\n"},
        {"unwound", "fn(w()) { f := file(\"p\"); f.open(\"r\"); throw error(\"s\") }\ntry { w() } catch(e) { print(e.message) }\n"},
        {"write-two", "f := file(\"w\")\nf.open(\"w\")\nf.write(\"x\")\nf.save()\nf.close()\ng := file(\"p\")\ng.open(\"r\")\ng.close()\n"},
        {"streams", "s := ifstream(\"p\")\nprint(s.read_line())\nclose(s)\no := ofstream(\"o\")\no.write(\"y\")\nclose(o)\n"},
        {"stream-left-open", "o := ofstream(\"o2\")\no.write(\"y\")\nprint(1)\n"},
    };
    const char* only = std::getenv("ONLY_PROG");
    for (long win : {1L, -1L})
        for (const auto& program : programs)
            if (!only || std::strcmp(only, program.name) == 0) campaign(program.name, program.text, win);
    std::filesystem::current_path(std::filesystem::temp_directory_path());
    std::filesystem::remove_all(dir);
    std::printf("%s cp17 persistent allocation failure: %d bad campaign(s)\n", bad ? "FAIL" : "PASS", bad);
    (void)argc;
    (void)argv;
    return bad ? 1 : 0;
}
