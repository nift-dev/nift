// Public-header consumer probe. Compiled with ONLY -Iinclude (see the
// test-public-header Makefile target), this proves the public Embedded Nift
// headers are self-contained: a consumer must not need -Isrc, must not see
// json::Document, and must not need to know Jsonic++ exists.
#include <nift/nift.h>

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
