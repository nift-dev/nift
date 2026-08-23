#pragma once
#include "Types.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace json { class Document; }

// Internal capability seam between the shared rendering core (Parser) and the
// project/build layer.
//
// CP1 introduces this seam so Parser no longer names ProjectInfo directly.
// Every capability below is exactly what the current Parser requires; nothing
// is added for the sake of a future public Embed API, and a couple of entries
// (build_threads, pagination_output_path) are deliberately project-flavoured
// because that is what the parser genuinely reaches for today.
//
// ProjectInfoHost implements this interface for the CLI with behaviour
// identical to the pre-CP1 implementation.
class RenderHost {
public:
    virtual ~RenderHost() = default;

    virtual const std::filesystem::path& root() const = 0;
    virtual std::string relative(const std::filesystem::path& path) const = 0;
    virtual const std::string& output_dir() const = 0;
    virtual int build_threads() const = 0;

    virtual std::filesystem::path content_path(const TrackedInfo& info) const = 0;
    virtual std::filesystem::path output_path(const TrackedInfo& info) const = 0;
    virtual std::filesystem::path pagination_output_path(const TrackedInfo& info, std::size_t page) const = 0;

    // @pathto capability.
    //
    // has_output_context() reports whether the current page has a usable
    // output location to compute paths from. A filesystem CLI page always
    // does; an Embedded Nift render only does once the caller sets the current
    // output on the per-render Context. If @pathto is used without it, the
    // parser must error rather than invent a location.
    virtual bool has_output_context() const = 0;
    struct TrackedOutput {
        std::filesystem::path path;
        bool index_page = false;
    };
    // Output path of a tracked page name (nullopt when the name is not
    // tracked). The CLI resolves tracked names through its tracking model; an
    // Embedded engine has no tracked pages and returns nullopt, so @pathto
    // treats every argument as a concrete project path.
    virtual std::optional<TrackedOutput> tracked_output_path(const std::string& name) const = 0;

    // Host-supplied value binding (Embedded Nift engine defaults/overlays).
    // Returns a pointer to the shared document, or nullptr when the name is
    // not supplied by the host. The parser resolves these before @json
    // bindings and contracts. ProjectInfoHost returns nullptr (the CLI has no
    // pre-supplied bindings), so CLI resolution is unchanged.
    virtual const std::shared_ptr<const json::Document>* binding(const std::string& name) const = 0;

    // Project-contract namespaces (config.contracts): the parser needs to
    // refuse bindings that collide with a configured contract name, and to
    // resolve a contract name to its configured source path.
    virtual bool is_contract_name(const std::string& name) const = 0;
    virtual const std::string* contract_source(const std::string& name) const = 0;

    virtual const std::string* read_shared_source(const std::filesystem::path& path) const = 0;
    virtual std::shared_ptr<const json::Document> read_shared_json(const std::filesystem::path& path,
                                                                   std::string& error) const = 0;

    // Source existence/readability as seen by this host. For a filesystem host
    // these are the ordinary filesystem checks; for a loader-backed host they
    // are the loader's own view of whether a path has content.
    virtual bool source_exists(const std::filesystem::path& path) const = 0;
    virtual bool source_readable(const std::filesystem::path& path) const = 0;

    // Environment lookup for @getenv. nullopt means the variable is unset.
    virtual std::optional<std::string> environment(const std::string& name) const = 0;
};
