#include "RuntimeValue.h"

#include <cassert>
#include <string>

int main() {
    assert(nift::runtime_valid_utf8(""));
    assert(nift::runtime_valid_utf8("ASCII"));
    assert(nift::runtime_valid_utf8(std::string("\xc2\x80\xe0\xa0\x80\xf0\x90\x80\x80", 9)));
    assert(nift::runtime_valid_utf8(std::string("\xf4\x8f\xbf\xbf", 4)));

    assert(!nift::runtime_valid_utf8(std::string("\x80", 1)));
    assert(!nift::runtime_valid_utf8(std::string("\xe2\x28\xa1", 3)));
    assert(!nift::runtime_valid_utf8(std::string("\xc0\x80", 2)));
    assert(!nift::runtime_valid_utf8(std::string("\xe0\x80\x80", 3)));
    assert(!nift::runtime_valid_utf8(std::string("\xf0\x80\x80\x80", 4)));
    assert(!nift::runtime_valid_utf8(std::string("\xed\xa0\x80", 3)));
    assert(!nift::runtime_valid_utf8(std::string("\xf4\x90\x80\x80", 4)));
    assert(!nift::runtime_valid_utf8(std::string("\xf5\x80\x80\x80", 4)));
    assert(!nift::runtime_valid_utf8(std::string("\xe2\x82", 2)));
}
