#include "RuntimeValue.h"
#include <cassert>
#include <string>

int main() {
    using nift::RuntimeValue;
    auto object = RuntimeValue::make_object();
    object.object.emplace_back("same", RuntimeValue(1));
    object.object.emplace_back("same", RuntimeValue(2));
    object.object.emplace_back("tail", RuntimeValue("value"));
    const auto& view = static_cast<const RuntimeValue&>(object);
    assert(view.find_member("same") && view.find_member("same")->num == 1);
    assert(&view["same"] == view.find_member("same"));
    assert(!view.find_member("Same") && !view.find_member("missing"));
    assert(!RuntimeValue().find_member("same"));
    assert(!RuntimeValue(3).find_member("same"));
    assert(object.object.size() == 3); // missing lookup never inserts
    object["new"] = RuntimeValue(9);
    assert(object.object.back().first == "new");
    // Resolve anew after erase, reinsert, replacement and vector relocation.
    object.object.erase(object.object.begin());
    assert(object.find_member("same")->num == 2);
    object.object.emplace_back("same", RuntimeValue(4));
    assert(object.find_member("same")->num == 2);
    for (int i=0; i<8000; ++i) object.object.emplace_back(std::to_string(i), RuntimeValue(i));
    assert(object.find_member("tail")->string == "value");
    *object.find_member("tail") = RuntimeValue(false);
    assert(object.find_member("tail")->is_bool());
    object = RuntimeValue::make_object();
    auto array = RuntimeValue::make_array(); array.array.emplace_back(7);
    auto nested = RuntimeValue::make_object(); nested.object.emplace_back("k", RuntimeValue(8));
    object.object.emplace_back("null", RuntimeValue());
    object.object.emplace_back("number", RuntimeValue(3));
    object.object.emplace_back("string", RuntimeValue("str"));
    object.object.emplace_back("bool", RuntimeValue(true));
    object.object.emplace_back("array", array);
    object.object.emplace_back("object", nested);
    object.object.emplace_back("bytes", RuntimeValue(nift::RuntimeBytes{1,2}));
    object.object.emplace_back("timer", RuntimeValue::make_timer(1,2));
    object.object.emplace_back("error", RuntimeValue::make_error("message", "user.example"));
    for (const auto& entry : object.object) {
        const auto* found = object.find_member(entry.first);
        assert(found && found == &entry.second);
        RuntimeValue copy = *found;
        assert(nift::runtime_equal(copy, entry.second));
    }
    auto copy = *object.find_member("array"); copy.array.emplace_back(9);
    assert(object.find_member("array")->array.size() == 1);
    try { (void)static_cast<const RuntimeValue&>(object)["absent"]; assert(false); }
    catch (const std::out_of_range& error) {
        assert(std::string(error.what()) == "JSON object has no key 'absent'");
    }
}
