#pragma once
#include "nift/value.h"
#include "nift/render_error.h"
#include <string>
namespace nift {
class Engine;
// One embedded script operation. Runtime output is captured per operation and
// remains available through stdout_output()/stderr_output() even on failure.
class ScriptResult {
public:
    bool ok() const { return ok_; }
    const Value& value() const { return value_; }
    const RenderError& error() const { return error_; }
    const std::string& stdout_output() const { return stdout_output_; }
    const std::string& stderr_output() const { return stderr_output_; }
private:
    friend class Engine;
    bool ok_ = false;
    Value value_;
    RenderError error_;
    std::string stdout_output_;
    std::string stderr_output_;
};
}
