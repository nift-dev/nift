#pragma once

#include <filesystem>
#include <string>

namespace nift {

// A template/page source for rendering: either a filesystem path or
// in-memory text. Source::text() carries an optional logical name so an
// in-memory source has an identity for diagnostics and, later, for resolving
// relative @input paths against a base rather than the process working
// directory.
class Source {
public:
    static Source path(std::filesystem::path path) {
        Source source;
        source.kind_ = Kind::Path;
        source.path_ = std::move(path);
        return source;
    }

    static Source text(std::string content) {
        Source source;
        source.kind_ = Kind::Text;
        source.text_ = std::move(content);
        return source;
    }

    static Source text(std::string content, std::string logical_name) {
        Source source;
        source.kind_ = Kind::Text;
        source.text_ = std::move(content);
        source.logical_name_ = std::move(logical_name);
        return source;
    }

    bool is_path() const { return kind_ == Kind::Path; }
    bool is_text() const { return kind_ == Kind::Text; }

    const std::filesystem::path& path() const { return path_; }
    const std::string& text() const { return text_; }
    const std::string& logical_name() const { return logical_name_; }

private:
    enum class Kind { Path, Text };
    Source() = default;
    Kind kind_ = Kind::Text;
    std::filesystem::path path_;
    std::string text_;
    std::string logical_name_;
};

} // namespace nift
