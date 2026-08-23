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
// One Engine per process, configured once (root, loaders, defaults) and then
// shared across concurrent renders; each render supplies its request-scoped
// state through a per-render Context. Rendering shares the exact
// parser/evaluator used by the Nift CLI.
//
// Thread-safety contract (CP7a):
// - Concurrent render() calls on one Engine are supported and safe, provided
//   the Engine is not being mutated concurrently.
// - Engine mutation (set / set_json / set_loader / set_environment_provider /
//   set_root) is NOT thread-safe with active renders. Configure the Engine
//   before serving; do not call the mutators concurrently with render().
// - Context is per-render state owned by the calling thread. It must not be
//   shared across threads, and must not be mutated while a render that uses it
//   is running.
// - The loader and environment provider may be invoked concurrently by render
//   threads, so they must be thread-safe. The defaults (filesystem and the
//   process environment) are.
// - The internal source/JSON caches are mutex-protected.
class Engine {
public:
    Engine();
    ~Engine();
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Project-aware construction (PA3): associates the Engine with a Nift
    // project at `project_root`, loading and validating .nift/config.json and
    // .nift/tracked.json into an immutable snapshot. Non-throwing: check
    // is_open()/open_error(). The snapshot is never mutated and never reloaded
    // implicitly; every render observes it for the Engine's lifetime. Project
    // discovery is explicit only - default Engine() stays deterministic
    // standalone and never walks the filesystem.
    explicit Engine(std::filesystem::path project_root);

    // Project-aware state.
    // is_open(): the project snapshot was loaded and validated.
    // open_error(): the construction failure message, empty when is_open().
    bool is_open() const;
    std::string open_error() const;

    // Replaces the immutable project snapshot atomically (PA4). In-flight
    // renders finish on the snapshot they started with; later renders observe
    // the new one. The snapshot covers project config/tracking state and
    // geometry (content/output/pagination paths, contracts, tracked-name
    // registry); content/template/JSON sources are still read from the
    // filesystem at render time, so only state captured in the snapshot is
    // generation-scoped. Returns true when the new snapshot loaded and
    // validated; otherwise false (with `error` filled when provided) and the
    // last known-good snapshot remains in service - reload never fails closed
    // and never writes to the project. This is also how an Engine constructed
    // before its project existed can later open it. The host application
    // decides when a reload matters; the embedded Engine never watches the
    // filesystem. The Engine defaults and environment provider are unaffected
    // by a reload. reload() is safe to call concurrently with render().
    bool reload(std::string* error = nullptr);

    // Project-aware rendering by tracked page name, e.g. render("about").
    // The page's content/template/output geometry comes from the project
    // snapshot; @pathto, @input, @json, contracts, dependencies, requirements
    // and pagination behave exactly like the CLI. The page-name argument is
    // authoritative (Context::set_page_name is ignored) and the project defines
    // the current output (Context::set_current_output is ignored). Context
    // value overlays and title override Engine defaults and the tracked title.
    // A failed project open or an unknown page name is a controlled error in
    // the returned RenderResult, never a throw or process termination.
    RenderResult render(std::string_view page_name);
    RenderResult render(std::string_view page_name, const Context& context);

    // Base directory used to resolve relative path sources and relative @input
    // paths. Without a root, a relative @input path is an error rather than
    // being resolved silently against the process working directory.
    void set_root(std::filesystem::path root);

    // Custom loader for @input/content/template paths. The default reads from
    // the filesystem under the configured root. A loader is a repeatable
    // lookup function, not a one-shot stream: it may be called more than once
    // per source (existence/readability probe then read), and may be called
    // concurrently from render threads, so it must be thread-safe.
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
    //
    // @pathto requires a path context: set Context::set_current_output (and
    // Context::set_page_name for the 404 rule). Without it, @pathto errors
    // rather than guessing a location.
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
