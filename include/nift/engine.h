#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "nift/context.h"
#include "nift/render_result.h"
#include "nift/source.h"

namespace nift {

// Embedded Nift rendering engine.
//
// One Engine per process, configured once (root, loaders) and then shared
// across concurrent renders; each render supplies its request-scoped state
// through a Context. Rendering shares the exact parser/evaluator used by the
// Nift CLI.
class Engine {
public:
    Engine();
    ~Engine();
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Base directory used to resolve relative path sources and relative @input
    // paths. Without a root, a relative @input path is an error rather than
    // being resolved silently against the process working directory.
    void set_root(std::filesystem::path root);

    // Custom loader for @input/content/template paths. The default reads from
    // the filesystem under the configured root.
    void set_loader(std::function<std::optional<std::string>(std::string_view path)> loader);

    // Environment provider for @getenv. The default reads the process
    // environment; a provider lets an application supply its own lookup
    // (nullopt means unset).
    void set_environment_provider(std::function<std::optional<std::string>(std::string_view name)> provider);

    // Long-lived application-wide value bindings, resolved (after any Context
    // overlay) before @json bindings, contracts and built-in metadata.
    // Returns false if the name is not a valid binding identifier or is a
    // structural built-in (name, content-path, output-path, template-path,
    // loop), or if set_json text is not valid JSON.
    bool set(std::string name, Value value);
    bool set(std::string name, std::string value);
    bool set(std::string name, int value);
    bool set(std::string name, bool value);
    bool set_json(std::string name, std::string_view json_text);

    // Full page + template composition (template contains @content; exactly one
    // @content is required). The page and template may each be text or path.
    RenderResult render(const Source& page, const Source& page_template);
    RenderResult render(const Source& page, const Source& page_template, const Context& context);

    // Standalone partial/fragment rendering. There is no content slot: a
    // partial that contains @content is an error. Use render(page, template)
    // when you want @content.
    RenderResult render(const Source& partial);
    RenderResult render(const Source& partial, const Context& context);

    // Opaque internal state (defined in the implementation translation unit).
    struct Impl;

private:
    std::shared_ptr<Impl> impl_;
};

} // namespace nift
