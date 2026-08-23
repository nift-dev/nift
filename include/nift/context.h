#pragma once

#include <string>

namespace nift {

class Engine;

// Per-render state: the page identity that varies from request to request.
// Long-lived capabilities (loaders, root, default values) belong to Engine;
// page name/title and later request-scoped values belong here.
class Context {
public:
    void set_page_name(std::string name) { page_name_ = std::move(name); }
    void set_title(std::string title) { title_ = std::move(title); }

private:
    friend class Engine;
    std::string page_name_;
    std::string title_;
};

} // namespace nift
