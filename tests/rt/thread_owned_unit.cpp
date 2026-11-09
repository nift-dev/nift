// B03 (combined-b3-b5.md section 48): nift::tls::owned<T>() (src/rt/thread_owned.h).
// Per-thread instances; created on first use; destroyed at thread exit newest first (a later
// object may use an earlier one in its destructor, as with thread_locals); an object created by
// another's constructor or destructor is destroyed too; nothing registers a TLS destructor.
// Built with ASan/UBSan (LeakSanitizer: no leak after worker threads exit).
#include "rt/thread_owned.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { ++failures; std::printf("FAIL %s\n", what); }
}
std::atomic<int> live{0};
thread_local std::vector<std::string>* t_log = nullptr;  // per-thread destruction log (trivial)

struct First {
    std::string text = std::string(100, 'a');  // heap storage: LSan sees a leak if never freed
    First() { ++live; }
    ~First() { --live; if (t_log) t_log->push_back("First"); }
};
struct Second {
    std::vector<int> data = std::vector<int>(1000, 7);
    Second() { ++live; (void)nift::tls::owned<First>(); }  // creates an earlier one if needed
    ~Second() {
        --live;
        // newest first: First (created before Second) is still alive here
        check(nift::tls::owned<First>().text.size() == 100, "Second's destructor can use First");
        if (t_log) t_log->push_back("Second");
    }
};
struct Third {
    Third() { ++live; }
    ~Third() { --live; (void)nift::tls::owned<Fourth>(); if (t_log) t_log->push_back("Third"); }
    struct Fourth {
        std::string s = std::string(64, 'x');
        Fourth() { ++live; }
        ~Fourth() { --live; if (t_log) t_log->push_back("Fourth"); }
    };
};
} // namespace

int main() {
    // Same object per thread, distinct across threads.
    First* main_first = &nift::tls::owned<First>();
    check(main_first == &nift::tls::owned<First>(), "one instance per thread");
    std::vector<std::string> logs[4];
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([i, &logs, main_first] {
            t_log = &logs[i];
            First& f = nift::tls::owned<First>();
            check(&f != main_first, "a thread has its own instance");
            if (i % 2 == 0) (void)nift::tls::owned<Second>();
            (void)nift::tls::owned<Third>();
        });
    }
    for (auto& w : workers) w.join();
    for (int i = 0; i < 4; ++i) {
        // Creation order First, (Second), Third; destruction newest first. Fourth, created by
        // Third's destructor, is then the newest and is destroyed next (as glibc does with a
        // thread_local constructed while thread_locals are destroyed).
        std::vector<std::string> want = i % 2 == 0 ? std::vector<std::string>{"Third", "Fourth", "Second", "First"}
                                                   : std::vector<std::string>{"Third", "Fourth", "First"};
        if (logs[i] != want) { std::string got; for (auto& s : logs[i]) got += s + " "; std::printf("thread %d got: %s\n", i, got.c_str()); }
        check(logs[i] == want, "destruction order newest first, late objects destroyed too");
    }
    check(live.load() == 1, "every worker object destroyed (the main thread's First lives on)");
    std::printf("%s thread_owned unit: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
