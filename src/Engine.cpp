#include "nift/engine.h"

#include "FileSystem.h"
#include "Json.h"
#include "Parser.h"
#include "RenderHost.h"
#include "ValueInternal.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>

// Embedded Nift engine: the same parser/evaluator the CLI uses, driven
// through the internal RenderHost seam (CP1). The Engine owns long-lived
// configuration (root, loader, caches); per-render page identity lives in
// nift::Context.

struct nift::Engine::Impl {
    std::filesystem::path root;
    std::function<std::optional<std::string>(std::string_view)> loader;

    // Long-lived application-wide value bindings (engine.set / set_json).
    std::unordered_map<std::string, std::shared_ptr<const json::Document>> defaults;

    mutable std::mutex source_cache_mutex_;
    mutable std::unordered_map<std::string, std::unique_ptr<const std::string>> source_cache_;
    mutable std::mutex json_cache_mutex_;
    mutable std::unordered_map<std::string, std::shared_ptr<const json::Document>> json_cache_;

    std::string relative(const std::filesystem::path& path) const {
        const std::filesystem::path normalized = path.lexically_normal();
        if (root.empty()) return normalized.generic_string();
        const std::filesystem::path rel = normalized.lexically_relative(root.lexically_normal());
        return rel.empty() ? normalized.generic_string() : rel.generic_string();
    }
};

namespace {

class EngineHost : public RenderHost {
public:
    EngineHost(nift::Engine::Impl& impl,
               const std::unordered_map<std::string, std::shared_ptr<const json::Document>>* render_bindings)
        : impl_(impl), render_bindings_(render_bindings) {}

    const std::filesystem::path& root() const override { return impl_.root; }
    std::string relative(const std::filesystem::path& path) const override { return impl_.relative(path); }
    const std::string& output_dir() const override { static const std::string empty; return empty; }
    int build_threads() const override { return 1; }

    std::filesystem::path content_path(const TrackedInfo& info) const override { return impl_.root / info.name; }
    std::filesystem::path output_path(const TrackedInfo& info) const override { return impl_.root / info.name; }
    std::filesystem::path pagination_output_path(const TrackedInfo& info, std::size_t) const override {
        return impl_.root / info.name;
    }

    const TrackedInfo* find(const std::string&) const override { return nullptr; }

    // Per-render Context overlays win over Engine defaults.
    const std::shared_ptr<const json::Document>* binding(const std::string& name) const override {
        if (render_bindings_) {
            const auto it = render_bindings_->find(name);
            if (it != render_bindings_->end()) return &it->second;
        }
        const auto it = impl_.defaults.find(name);
        return it == impl_.defaults.end() ? nullptr : &it->second;
    }

    bool is_contract_name(const std::string&) const override { return false; }
    const std::string* contract_source(const std::string&) const override { return nullptr; }

    const std::string* read_shared_source(const std::filesystem::path& path) const override {
        const std::string key = path.lexically_normal().generic_string();
        {
            std::lock_guard<std::mutex> lock(impl_.source_cache_mutex_);
            const auto it = impl_.source_cache_.find(key);
            if (it != impl_.source_cache_.end()) return it->second.get();
        }

        std::optional<std::string> contents;
        if (impl_.loader) {
            contents = impl_.loader(key);
            if (!contents) return nullptr;
        } else {
            if (!filesystem::file_readable(path)) return nullptr;
            contents = filesystem::read_file(path);
        }
        auto stored = std::make_unique<const std::string>(*contents);
        const std::string* result = stored.get();
        {
            std::lock_guard<std::mutex> lock(impl_.source_cache_mutex_);
            auto [it, inserted] = impl_.source_cache_.emplace(key, std::move(stored));
            if (!inserted) result = it->second.get();
        }
        return result;
    }

    std::shared_ptr<const json::Document> read_shared_json(const std::filesystem::path& path,
                                                           std::string& error) const override {
        const std::string key = path.lexically_normal().generic_string();
        {
            std::lock_guard<std::mutex> lock(impl_.json_cache_mutex_);
            const auto it = impl_.json_cache_.find(key);
            if (it != impl_.json_cache_.end()) return it->second;
        }

        std::optional<std::string> contents;
        if (impl_.loader) {
            contents = impl_.loader(key);
            if (!contents) { error = "JSON file does not exist"; return {}; }
        } else {
            if (!filesystem::file_readable(path)) { error = "JSON file is not readable"; return {}; }
            contents = filesystem::read_file(path);
        }
        auto document = std::make_shared<json::Document>();
        if (!json::Document::parse(*contents, *document, error)) return {};
        {
            std::lock_guard<std::mutex> lock(impl_.json_cache_mutex_);
            const auto [it, inserted] = impl_.json_cache_.emplace(key, document);
            if (!inserted) return it->second;
        }
        return document;
    }

private:
    nift::Engine::Impl& impl_;
    const std::unordered_map<std::string, std::shared_ptr<const json::Document>>* render_bindings_;
};

RenderSource to_render_source(const nift::Source& source, const nift::Engine::Impl& impl) {
    RenderSource out;
    if (source.is_path()) {
        out.path = source.path();
        if (out.path.is_relative() && !impl.root.empty()) out.path = impl.root / out.path;
        out.dependency = impl.relative(out.path);
    } else {
        out.text = source.text();
        out.logical_name = source.logical_name();
    }
    return out;
}

} // namespace

namespace nift {

struct RenderResultBuilder {
    static RenderResult build(const ::RenderResult& internal) {
        RenderResult result;
        if (internal.ok) {
            result.ok_ = true;
            result.output_ = internal.output;
        } else {
            result.ok_ = false;
            result.error_.message = internal.error.message;
            result.error_.source = internal.error.source_file.generic_string();
            result.error_.line = internal.error.line;
            result.error_.column = internal.error.column;
        }
        result.dependencies_.assign(internal.dependencies.begin(), internal.dependencies.end());
        result.requirements_.assign(internal.reqs.begin(), internal.reqs.end());
        return result;
    }
};

} // namespace nift

namespace nift {

Engine::Engine() : impl_(std::make_shared<Impl>()) {}
Engine::~Engine() = default;
Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

void Engine::set_root(std::filesystem::path root) { impl_->root = std::move(root); }
void Engine::set_loader(std::function<std::optional<std::string>(std::string_view path)> loader) {
    impl_->loader = std::move(loader);
}

bool Engine::set(std::string name, Value value) {
    if (!nift::detail::valid_binding_identifier(name) || nift::detail::structural_builtin_name(name))
        return false;
    impl_->defaults[std::move(name)] = std::make_shared<json::Document>(ValueAccess::doc(value));
    return true;
}
bool Engine::set(std::string name, std::string value) {
    return set(std::move(name), Value(std::move(value)));
}
bool Engine::set(std::string name, int value) {
    return set(std::move(name), Value(value));
}
bool Engine::set(std::string name, bool value) {
    return set(std::move(name), Value(value));
}
bool Engine::set_json(std::string name, std::string_view json_text) {
    if (!nift::detail::valid_binding_identifier(name) || nift::detail::structural_builtin_name(name))
        return false;
    auto document = std::make_shared<json::Document>();
    std::string error;
    if (!json::Document::parse(std::string(json_text), *document, error)) return false;
    impl_->defaults[std::move(name)] = std::move(document);
    return true;
}

RenderResult Engine::render(const Source& page, const Source& page_template, const Context& context) {
    const RenderSource page_render_source = to_render_source(page, *impl_);
    const RenderSource template_render_source = to_render_source(page_template, *impl_);
    TrackedInfo info;
    info.name = context.page_name_;
    info.title = context.title_;
    std::unordered_map<std::string, std::shared_ptr<const json::Document>> render_bindings;
    for (const auto& [name, value] : context.bindings_)
        render_bindings[name] = std::make_shared<json::Document>(ValueAccess::doc(value));
    EngineHost host(*impl_, &render_bindings);
    Parser parser(host, info);
    return RenderResultBuilder::build(parser.render_composed(template_render_source, page_render_source,
                                                              /*require_exactly_one_content=*/true));
}

RenderResult Engine::render(const Source& page, const Source& page_template) {
    return render(page, page_template, Context{});
}

RenderResult Engine::render(const Source& partial, const Context& context) {
    const RenderSource partial_render_source = to_render_source(partial, *impl_);
    TrackedInfo info;
    info.name = context.page_name_;
    info.title = context.title_;
    std::unordered_map<std::string, std::shared_ptr<const json::Document>> render_bindings;
    for (const auto& [name, value] : context.bindings_)
        render_bindings[name] = std::make_shared<json::Document>(ValueAccess::doc(value));
    EngineHost host(*impl_, &render_bindings);
    Parser parser(host, info);
    return RenderResultBuilder::build(parser.render_composed(partial_render_source, std::nullopt,
                                                              /*require_exactly_one_content=*/false));
}

RenderResult Engine::render(const Source& partial) {
    return render(partial, Context{});
}

} // namespace nift
