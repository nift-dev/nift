#include "json.h"

#include <cassert>
#include <iostream>
#include <string>

static void expect_invalid(const std::string& source) {
    json::Document document;
    std::string error;
    assert(!json::Document::parse(source, document, error));
    assert(!error.empty());
}

int main() {
    json::Document document;
    std::string error;

    const std::string source =
        R"({"title":"A \"quoted\" title","emoji":"\uD83D\uDE00","items":[1,true,null,{"text":"line\nnext"}]})";

    assert(json::Document::parse(source, document, error));
    assert(document.is_object());
    assert(document["title"].string == "A \"quoted\" title");
    assert(document["emoji"].string == "😀");
    assert(document["items"][3]["text"].string == "line\nnext");

    document["extra"] = "value";
    const std::string serialized = document.dump();
    json::Document reparsed;
    assert(json::Document::parse(serialized, reparsed, error));
    assert(reparsed["extra"].string == "value");

    // Structural errors and ambiguous objects.
    expect_invalid(R"({"a":1,"a":2})");
    expect_invalid(R"([1,])");
    expect_invalid(R"({"a":1,})");
    expect_invalid(R"({"a" 1})");
    expect_invalid(R"({a:1})");

    // Number grammar and finite-range handling.
    expect_invalid("01");
    expect_invalid("-01");
    expect_invalid("1.");
    expect_invalid("1e");
    expect_invalid("1e+");
    expect_invalid("1e309");
    assert(json::Document::parse("-0.25e+2", document, error));
    assert(document.is_number() && document.num == -25.0);

    // String escapes / Unicode surrogate handling.
    expect_invalid(R"("\x")");
    expect_invalid(R"("\uDE00")");
    expect_invalid(R"("\uD83D")");
    expect_invalid(R"("\uD83D\u0041")");
    expect_invalid(std::string("\"line\nraw\""));
    assert(json::Document::parse(R"("\/\b\f\n\r\t")", document, error));

    // Streaming named-array reader should reject duplicate root keys too.
    bool callback_called = false;
    assert(!json::Document::for_each_array_item(
        R"({"tracked":[],"tracked":[]})", "tracked",
        [&](json::Document&&) { callback_called = true; return true; }, error));
    assert(!callback_called);

    std::cout << "JSON smoke test passed\n";
}
