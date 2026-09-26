#pragma once
#include "nift/value.h"
#include "nift/render_error.h"
namespace nift {
class Engine;
class ScriptResult {
public:
    bool ok() const { return ok_; }
    const Value& value() const { return value_; }
    const RenderError& error() const { return error_; }
private:
    friend class Engine;
    bool ok_ = false;
    Value value_;
    RenderError error_;
};
}
