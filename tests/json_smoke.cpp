#include "Json.h"

#include <cassert>
#include <iostream>

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

    std::cout << "JSON smoke test passed\n";
}
