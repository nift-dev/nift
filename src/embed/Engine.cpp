#include "nift/engine.h"

#include "FileSystem.h"
#include "Json.h"
#include "Parser.h"
#include "ProjectHost.h"
#include "ProjectState.h"
#include "RenderHost.h"
#include "ValueInternal.h"

#include <cstdlib>

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
    std::function<nift::HostResult(std::string_view)> loader;
    std::function<nift::HostResult(std::string_view)> environment_provider;

    // Long-lived application-wide value bindings (engine.set / set_json).
    std::unordered_map<std::string, std::shared_ptr<const json::Document>> defaults;

    mutable std::mutex source_cache_mutex_;
    mutable std::unordered_map<std::string, std::unique_ptr<const std::string>> source_cache_;
    mutable std::mutex json_cache_mutex_;
    mutable std::unordered_map<std::string, std::shared_ptr<const json::Document>> json_cache_;

    // Project-aware mode (PA3/PA4): the validated immutable snapshot is shared
    // per generation. reload() swaps in a freshly built snapshot atomically
    // under snapshot_mutex_; a render captures its own shared_ptr under the
    // mutex and then renders freely, so in-flight renders finish on the
    // snapshot they started with while later renders observe the new one.
    mutable std::mutex snapshot_mutex_;
    std::shared_ptr<const ProjectState> project_state;
    bool project_open_ok = false;
    std::string project_open_error;

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
               const std::unordered_map<std::string, std::shared_ptr<const json::Document>>* render_bindings,
               const std::filesystem::path& current_output)
        : impl_(impl), render_bindings_(render_bindings), current_output_(current_output) {}

    const std::filesystem::path& root() const override { return impl_.root; }
    std::string relative(const std::filesystem::path& path) const override { return impl_.relative(path); }
    const std::string& output_dir() const override { static const std::string empty; return empty; }
    int build_threads() const override { return 1; }

    std::filesystem::path content_path(const TrackedInfo& info) const override { return impl_.root / info.name; }
    std::filesystem::path output_path(const TrackedInfo& info) const override {
        return current_output_.empty() ? impl_.root / info.name : current_output_;
    }
    std::filesystem::path pagination_output_path(const TrackedInfo& info, std::size_t) const override {
        return impl_.root / info.name;
    }

    bool has_output_context() const override { return !current_output_.empty(); }
    std::optional<TrackedOutput> tracked_output_path(const std::string&) const override {
        return std::nullopt;
    }

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

    HostSource read_shared_source(const std::filesystem::path& path) const override {
        const std::string key = path.lexically_normal().generic_string();
        {
            std::lock_guard<std::mutex> lock(impl_.source_cache_mutex_);
            const auto it = impl_.source_cache_.find(key);
            if (it != impl_.source_cache_.end())
                return {nift::HostStatus::Found, it->second.get(), ""};
        }

        std::optional<std::string> contents;
        if (impl_.loader) {
            nift::HostResult result = impl_.loader(key);
            if (result.status == nift::HostStatus::Error)
                return {nift::HostStatus::Error, nullptr, std::move(result.error)};
            if (result.status == nift::HostStatus::NotFound)
                return {nift::HostStatus::NotFound, nullptr, ""};
            contents = std::move(result.value);
        } else {
            // The read is the authority: no separate file_readable probe
            // (which opened the file a second time); read_file_checked classifies
            // missing/unreadable/non-regular as nullopt -> NotFound.
            contents = filesystem::read_file_checked(path);
            if (!contents) return {nift::HostStatus::NotFound, nullptr, ""};
        }
        auto stored = std::make_unique<const std::string>(*contents);
        const std::string* result_ptr = stored.get();
        {
            std::lock_guard<std::mutex> lock(impl_.source_cache_mutex_);
            auto [it, inserted] = impl_.source_cache_.emplace(key, std::move(stored));
            if (!inserted) result_ptr = it->second.get();
        }
        return {nift::HostStatus::Found, result_ptr, ""};
    }

    bool source_exists(const std::filesystem::path& path) const override {
        if (impl_.loader) {
            nift::HostResult result = impl_.loader(path.lexically_normal().generic_string());
            // A host failure is treated as "exists" so the subsequent read
            // surfaces the distinct host-error diagnostic.
            return result.status != nift::HostStatus::NotFound;
        }
        return filesystem::path_exists(path);
    }
    bool source_readable(const std::filesystem::path& path) const override {
        if (impl_.loader) {
            nift::HostResult result = impl_.loader(path.lexically_normal().generic_string());
            return result.status != nift::HostStatus::NotFound;
        }
        return filesystem::file_readable(path);
    }
    nift::HostResult environment(const std::string& name) const override {
        if (impl_.environment_provider) return impl_.environment_provider(name);
        if (const char* value = std::getenv(name.c_str()))
            return {nift::HostStatus::Found, std::string(value), ""};
        return {nift::HostStatus::NotFound, "", ""};
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
            nift::HostResult result = impl_.loader(key);
            if (result.status == nift::HostStatus::Error) { error = std::move(result.error); return {}; }
            if (result.status == nift::HostStatus::NotFound) { error = "JSON file does not exist"; return {}; }
            contents = std::move(result.value);
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
    std::filesystem::path current_output_;
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
        // Complete pagination: the internal vector holds pages 1..N (page 1 is
        // `output`); expose pages 2..N ascending with their page numbers.
        if (internal.pagination_outputs.size() > 1) {
            result.pagination_.reserve(internal.pagination_outputs.size() - 1);
            for (std::size_t i = 1; i < internal.pagination_outputs.size(); ++i) {
                result.pagination_.push_back({i + 1, internal.pagination_outputs[i]});
            }
        }
        return result;
    }
};

} // namespace nift

namespace nift {

Engine::Engine() : impl_(std::make_shared<Impl>()) {}
Engine::~Engine() = default;
Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

Engine::Engine(std::filesystem::path project_root) : impl_(std::make_shared<Impl>()) {
    impl_->root = std::move(project_root);
    std::string error;
    if (!reload(&error)) impl_->project_open_error = std::move(error);
}

bool Engine::is_open() const {
    std::lock_guard<std::mutex> lock(impl_->snapshot_mutex_);
    return impl_->project_open_ok;
}

std::string Engine::open_error() const {
    std::lock_guard<std::mutex> lock(impl_->snapshot_mutex_);
    return impl_->project_open_error;
}

bool Engine::reload(std::string* error) {
    auto candidate = std::make_shared<ProjectState>();
    std::string candidate_error;
    if (!candidate->open(impl_->root, candidate_error)) {
        if (error) *error = std::move(candidate_error);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->snapshot_mutex_);
        impl_->project_state = std::move(candidate);
        impl_->project_open_ok = true;
        impl_->project_open_error.clear();
    }
    return true;
}

RenderResult Engine::render(std::string_view page_name, const Context& context) {
    std::shared_ptr<const ProjectState> snapshot;
    bool open_ok = false;
    std::string open_error;
    {
        std::lock_guard<std::mutex> lock(impl_->snapshot_mutex_);
        snapshot = impl_->project_state;
        open_ok = impl_->project_open_ok;
        open_error = impl_->project_open_error;
    }

    RenderResult result;
    if (!open_ok) {
        result.ok_ = false;
        result.error_.message = open_error.empty() ? std::string("not a Nift project") : std::move(open_error);
        return result;
    }

    const TrackedInfo* page = snapshot->find(std::string(page_name));
    if (page == nullptr) {
        result.ok_ = false;
        result.error_.message = "unknown page name '" + std::string(page_name) + "'";
        return result;
    }

    TrackedInfo info = *page;
    if (!context.title_.empty()) info.title = context.title_;

    // Context overlays win over Engine defaults; both are host bindings the
    // parser resolves before @json/contracts, exactly like the standalone seam.
    std::unordered_map<std::string, std::shared_ptr<const json::Document>> render_bindings = impl_->defaults;
    for (const auto& [name, value] : context.bindings_)
        render_bindings[name] = std::make_shared<json::Document>(ValueAccess::doc(value));

    ProjectHost host(*snapshot, &render_bindings, impl_->environment_provider);
    Parser parser(host, info);
    return RenderResultBuilder::build(parser.render());
}

RenderResult Engine::render(std::string_view page_name) {
    return render(page_name, Context{});
}

void Engine::set_root(std::filesystem::path root) { impl_->root = std::move(root); }
void Engine::set_loader(std::function<nift::HostResult(std::string_view path)> loader) {
    impl_->loader = std::move(loader);
}

void Engine::set_loader(std::function<std::optional<std::string>(std::string_view path)> loader) {
    impl_->loader = [loader = std::move(loader)](std::string_view path) -> nift::HostResult {
        std::optional<std::string> value = loader(path);
        if (value) return {nift::HostStatus::Found, std::move(*value), ""};
        return {nift::HostStatus::NotFound, "", ""};
    };
}

void Engine::set_environment_provider(std::function<nift::HostResult(std::string_view name)> provider) {
    impl_->environment_provider = std::move(provider);
}

void Engine::set_environment_provider(std::function<std::optional<std::string>(std::string_view name)> provider) {
    impl_->environment_provider = [provider = std::move(provider)](std::string_view name) -> nift::HostResult {
        std::optional<std::string> value = provider(name);
        if (value) return {nift::HostStatus::Found, std::move(*value), ""};
        return {nift::HostStatus::NotFound, "", ""};
    };
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
    EngineHost host(*impl_, &render_bindings, context.current_output_);
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
    EngineHost host(*impl_, &render_bindings, context.current_output_);
    Parser parser(host, info);
    return RenderResultBuilder::build(parser.render_composed(partial_render_source, std::nullopt,
                                                              /*require_exactly_one_content=*/false));
}

RenderResult Engine::render(const Source& partial) {
    return render(partial, Context{});
}

RenderResult Engine::render_path(const std::filesystem::path& path) {
    return render(Source::path(path));
}

RenderResult Engine::render_path(const std::filesystem::path& path, const Context& context) {
    return render(Source::path(path), context);
}

RenderResult Engine::render_text(std::string_view text) {
    return render(Source::text(std::string(text)));
}

RenderResult Engine::render_text(std::string_view text, const Context& context) {
    return render(Source::text(std::string(text)), context);
}

} // namespace nift
