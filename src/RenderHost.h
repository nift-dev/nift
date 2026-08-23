#pragma once
#include "Types.h"

#include <filesystem>
#include <memory>
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

    virtual const TrackedInfo* find(const std::string& name) const = 0;

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
};
