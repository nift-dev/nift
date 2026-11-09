#pragma once
// B03 (docs/runtime-2/checkpoints/combined-b3-b5.md section 48): per-thread objects without C++
// TLS destructor registration.
//
// A `thread_local` object with a non-trivial destructor is registered with __cxa_thread_atexit on
// its first use. glibc (2.39+) allocates that registration and, when the allocation fails, aborts
// the process ("Fatal glibc error: failed to register TLS destructor: out of memory", SIGABRT). A
// first use after memory ran out (a teardown, an out-of-memory stop, a late code path) therefore
// turned an ordinary memory error into an abort.
//
// nift::tls::owned<T>() gives each thread its own T, created on first use with `new` (an exhausted
// heap is an ordinary std::bad_alloc, handled where every allocation failure is) and reached
// through constant-initialised, trivially destructible thread_local pointers, so nothing is
// registered with the C++ runtime. When a thread exits, one POSIX thread-specific-data key
// destructor destroys that thread's objects in reverse order of creation, as C++ destroys
// thread_locals (later objects may refer to earlier ones). pthread_key_create and
// pthread_setspecific allocate nothing here (one key, created once; glibc keeps the first 32
// keys' values in the thread descriptor); a failure there only leaves the objects to the end of
// the process, never an abort. The main thread's objects live until the process exits (a
// thread_local's destructor would have run at exit(); none of these has an effect beyond freeing
// memory).
//
// tests/runtime2/tls_destructors.py requires that nift registers no TLS destructor (imports no
// __cxa_thread_atexit).
#include <new>

#if defined(_WIN32)
namespace nift::tls {
template <class T>
T& owned() {
    thread_local T value;  // Windows: the CRT's TLS callbacks, not glibc's registration
    return value;
}
} // namespace nift::tls
#else
#include <pthread.h>

namespace nift::tls {
namespace detail {

struct Node {
    Node* previous;              // the thread's earlier object (destroyed after this one)
    void (*destroy)(Node*);      // deletes the holder and clears its slot
};

// This thread's objects, newest first.
inline thread_local Node* t_owned = nullptr;  // constant-initialised, trivially destructible

inline void destroy_all(void*) noexcept {
    while (Node* node = t_owned) {
        t_owned = node->previous;
        node->destroy(node);  // newest first: a later object may refer to an earlier one
    }
}

struct Key {
    pthread_key_t key{};
    bool valid = false;
    Key() noexcept { valid = pthread_key_create(&key, &destroy_all) == 0; }
};
inline const Key& key() noexcept {
    static const Key k;  // trivially destructible: no atexit registration
    return k;
}

template <class T>
struct Holder final : Node {
    T value;
    static inline thread_local T* t_slot = nullptr;  // constant-initialised, trivially destructible
    Holder() : Node{nullptr, &Holder::destroy}, value() {}
    static void destroy(Node* node) {
        t_slot = nullptr;
        delete static_cast<Holder*>(node);
    }
};

} // namespace detail

template <class T>
T& owned() {
    using H = detail::Holder<T>;
    if (H::t_slot == nullptr) {
        H* holder = new H();  // std::bad_alloc on exhaustion: an ordinary error
        const detail::Key& k = detail::key();
        // A non-null value makes the key's destructor run at thread exit (on failure: the
        // objects live to the end of the process).
        if (k.valid && detail::t_owned == nullptr) (void)pthread_setspecific(k.key, &detail::t_owned);
        holder->previous = detail::t_owned;  // after T's constructor, which may create others
        detail::t_owned = holder;
        H::t_slot = &holder->value;
    }
    return *H::t_slot;
}
} // namespace nift::tls
#endif
