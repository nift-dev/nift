// NR12 benchmark: standalone C++ Engine ns/render for the same representative
// Nift template the Rust bench uses (conditional greeting + 10-post loop).
// Prints "<ns> ns/render\n" on stdout; consumed by nift-rs examples/bench.rs.
#include "nift/nift.h"

#include <chrono>
#include <iostream>
#include <string>

int main() {
    const int iterations = 50000;
    const int warmup = 2000;

    nift::Engine engine;
    engine.set_json("user", R"({"logged_in":true,"name":"Ada"})");
    std::string posts = "[";
    for (int i = 0; i < 10; ++i) {
        if (i) posts += ",";
        posts += "{\"title\":\"Post " + std::to_string(i) +
                 "\",\"body\":\"Some body text for the post.\"}";
    }
    posts += "]";
    engine.set_json("posts", posts);

    nift::Source page(nift::Source::text(
        "@if(user.logged_in){<p>Hello $[user.name]</p>}@else{<p>Hello guest</p>}\n"
        "@for(post : posts){<article><h2>$[post.title]</h2><p>$[post.body]</p></article>}\n"));
    nift::Source tpl(nift::Source::text("@content"));
    nift::Context context;

    for (int i = 0; i < warmup; ++i) engine.render(page, tpl, context);

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) engine.render(page, tpl, context);
    const auto end = std::chrono::steady_clock::now();
    const double ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() /
                      static_cast<double>(iterations);
    std::cout << ns << " ns/render\n";
    return 0;
}
