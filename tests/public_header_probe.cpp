// Public-header consumer probe. Compiled with ONLY -Iinclude (see the
// test-public-header Makefile target), this proves the public Embedded Nift
// headers are self-contained: a consumer must not need -Isrc, must not see
// json::Document, and must not need to know Jsonic++ exists.
#include <nift/nift.h>

#include <type_traits>

// nift::Value move construction/assignment must be nothrow and free of
// allocation: the moved-from source becomes a valid Null Value without the
// move itself performing a potentially throwing allocation.
static_assert(std::is_nothrow_move_constructible<nift::Value>::value,
              "nift::Value move construction must be nothrow");
static_assert(std::is_nothrow_move_assignable<nift::Value>::value,
              "nift::Value move assignment must be nothrow");

int main() {
    nift::Engine engine;
    nift::Context context;
    nift::Value number(1);
    nift::Value text(std::string("hello"));
    nift::Value object = nift::Value::make_object();
    object["key"] = text;
    nift::Value array = nift::Value::make_array();
    array.push_back(number);
    context.set_title(std::string("Title"));
    context.set("count", 42);
    return static_cast<int>(number.number()) == 1 ? 0 : 1;
}
