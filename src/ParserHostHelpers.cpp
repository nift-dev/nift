// Host helpers shared by the legacy interpreter (ParserHelpers.cpp wrappers,
// ParserExpression.cpp) and the Runtime 2.0 Host natives (src/rt/natives).
// Moved verbatim from ParserHelpers.cpp (CP16 certification fix, owner
// Option A): no RuntimeValue, parser or legacy value machinery here, so the
// natives keep linking without the legacy value bridge (CP14 isolation).
#include "ParserHostHelpers.h"

#include <algorithm>
#if defined(NIFT_TEST_GLOB_KEY_STATS) || defined(NIFT_TEST_GLOB_PREFIX_STATS)
#include <cstdio>
#include <cstdlib>
#endif
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <cerrno>
#include <sys/random.h>
#elif defined(__APPLE__)
#include <cstdlib>
#else
#error "secure_random_bytes requires a supported native CSPRNG"
#endif
#ifndef _WIN32
#include <time.h>
#endif

namespace fs = std::filesystem;

namespace nift::detail {

bool nift_unix_epoch_milliseconds(std::int64_t& value, std::string& error) {
#ifdef _WIN32
    FILETIME file_time;
    GetSystemTimeAsFileTime(&file_time);
    ULARGE_INTEGER ticks;
    ticks.LowPart = file_time.dwLowDateTime;
    ticks.HighPart = file_time.dwHighDateTime;
    constexpr std::uint64_t windows_to_unix_ticks = 116444736000000000ULL;
    if (ticks.QuadPart < windows_to_unix_ticks) {
        error = "epoch: system clock predates the Unix epoch";
        return false;
    }
    value = static_cast<std::int64_t>((ticks.QuadPart - windows_to_unix_ticks) / 10000ULL);
#else
    timespec now{};
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        error = "epoch: cannot read the system clock";
        return false;
    }
    using Seconds = decltype(now.tv_sec);
    const auto seconds_raw = now.tv_sec;
    if constexpr (!std::is_signed_v<Seconds>) {
        if (static_cast<std::uintmax_t>(seconds_raw) >
            static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max())) {
            error = "epoch: system time is outside the signed 64-bit millisecond range";
            return false;
        }
    }
    const std::int64_t seconds = static_cast<std::int64_t>(seconds_raw);
    if (static_cast<Seconds>(seconds) != seconds_raw) {
        error = "epoch: system time is outside the signed 64-bit millisecond range";
        return false;
    }
    if (seconds < std::numeric_limits<std::int64_t>::min() / 1000 ||
        seconds > std::numeric_limits<std::int64_t>::max() / 1000) {
        error = "epoch: system time is outside the signed 64-bit millisecond range";
        return false;
    }
    const std::int64_t milliseconds = now.tv_nsec / 1000000;
    const std::int64_t whole_seconds = seconds * 1000;
    if (whole_seconds >= 0 &&
        milliseconds > std::numeric_limits<std::int64_t>::max() - whole_seconds) {
        error = "epoch: system time is outside the signed 64-bit millisecond range";
        return false;
    }
    value = whole_seconds + milliseconds;
#endif
    return true;
}

bool nift_secure_random_fill(unsigned char* data, std::size_t count, std::string& error) {
    if (count == 0) return true;
#ifdef _WIN32
    using BCryptGenRandomFn = LONG (WINAPI*)(void*, unsigned char*, unsigned long, unsigned long);
    wchar_t system_directory[MAX_PATH];
    const UINT system_length = GetSystemDirectoryW(system_directory, MAX_PATH);
    std::wstring bcrypt_path;
    if (system_length > 0 && system_length < MAX_PATH) {
        bcrypt_path.assign(system_directory, system_length);
        bcrypt_path += L"\\bcrypt.dll";
    }
    HMODULE module = bcrypt_path.empty() ? nullptr : LoadLibraryW(bcrypt_path.c_str());
    BCryptGenRandomFn generate = nullptr;
    if (module) {
        const FARPROC address = GetProcAddress(module, "BCryptGenRandom");
        // Windows procedure addresses share the representation of typed function pointers.
        static_assert(sizeof(generate) == sizeof(address));
        std::memcpy(&generate, &address, sizeof(generate));
    }
    std::size_t offset = 0;
    while (generate && offset < count) {
        const auto chunk = static_cast<unsigned long>(std::min<std::size_t>(count - offset, std::numeric_limits<unsigned long>::max()));
        if (generate(nullptr, data + offset, chunk, 0x00000002UL) != 0) break;
        offset += chunk;
    }
    if (module) FreeLibrary(module);
    if (!generate || offset != count) {
        error = "secure_random_bytes: operating-system random source failed";
        return false;
    }
#elif defined(__linux__)
    std::size_t offset = 0;
    while (offset < count) {
        const auto received = ::getrandom(data + offset, count - offset, 0);
        if (received > 0) { offset += static_cast<std::size_t>(received); continue; }
        if (received < 0 && errno == EINTR) continue;
        error = "secure_random_bytes: operating-system random source failed";
        return false;
    }
#elif defined(__APPLE__)
    if (count != 0) ::arc4random_buf(data, count);
#endif
    return true;
}

bool glob_has_magic(const std::string& s) {
    bool escaped=false; for(char c:s){if(escaped){escaped=false;continue;}if(c=='\\'){escaped=true;continue;}if(c=='*'||c=='?')return true;} return false;
}

namespace {


#ifdef NIFT_TEST_GLOB_KEY_STATS
struct GlobKeyStats {
    std::size_t conversions = 0;
    ~GlobKeyStats() {
        if (std::getenv("NIFT_TEST_GLOB_KEY_STATS"))
            std::fprintf(stderr, "glob-order conversions=%zu\n", conversions);
    }
} glob_key_stats;
#endif

// Order by exactly the existing generic path representation, computing it
// once per entry rather than allocating it in every sort comparison.
template<class T, class PathOf>
void sort_glob_paths(std::vector<T>& values, PathOf path_of) {
    if (values.size() < 2) return;
    struct Keyed { std::string key; T value; };
    std::vector<Keyed> keyed;
    keyed.reserve(values.size());
    for (auto& value : values) {
        auto key = path_of(value).generic_string();
#ifdef NIFT_TEST_GLOB_KEY_STATS
        ++glob_key_stats.conversions;
#endif
        keyed.push_back({std::move(key), std::move(value)});
    }
    std::sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
    for (std::size_t i = 0; i < values.size(); ++i) values[i] = std::move(keyed[i].value);
}

#ifdef NIFT_TEST_GLOB_PREFIX_STATS
struct GlobPrefixStats {
    unsigned long long terminal_paths=0, prefix_nodes=0, prefix_resolves=0, full_display_paths=0, final_values=0;
    ~GlobPrefixStats() {
        if (std::getenv("NIFT_TEST_GLOB_PREFIX_STATS"))
            std::fprintf(stderr,"glob-prefix terminal_paths=%llu prefix_nodes=%llu prefix_resolves=%llu full_display_paths=%llu final_values=%llu\n",terminal_paths,prefix_nodes,prefix_resolves,full_display_paths,final_values);
    }
} glob_prefix_stats;
#endif

// Match descriptors share directory ownership through this operation only.
struct SharedGlobOutput {
    std::vector<GlobMatch> values;
    std::unordered_map<fs::path, std::shared_ptr<GlobPrefix>> prefixes;
    std::shared_ptr<GlobPrefix> prefix(const fs::path& directory) {
        // Final absolute paths have always been normalized before display.
        // Escaped dot components can make a walked path lexically different;
        // retain canonical full-leaf resolution for those uncommon cases.
        auto normalized=fs::absolute(directory).lexically_normal();
        const bool changed=normalized!=directory;
        auto found = prefixes.find(normalized);
        if (found == prefixes.end()) {
            record_glob_prefix_event(GlobPrefixEvent::PrefixNode);
            auto prefix = std::make_shared<GlobPrefix>();
            prefix->original = normalized;
            found = prefixes.emplace(std::move(normalized), std::move(prefix)).first;
        }
        if (changed) found->second->requires_leaf_resolution=true;
        return found->second;
    }
    void emit(fs::path absolute, const fs::directory_entry* entry,
              const std::shared_ptr<GlobPrefix>& parent) {
        std::error_code ec;
        const bool resolve_leaf = (entry ? entry->is_symlink(ec) : fs::is_symlink(absolute, ec)) || ec || !absolute.has_filename();
        auto shared = parent ? parent : prefix(absolute.parent_path());
        values.push_back({std::move(absolute), std::move(shared), resolve_leaf});
    }
};
struct GlobVisitorOutput {
    const GlobVisitor& visitor;
};
std::shared_ptr<GlobPrefix> prepare_glob_prefix(GlobVisitorOutput&, const fs::path&) { return {}; }
std::shared_ptr<GlobPrefix> prepare_glob_prefix(std::vector<fs::path>&, const fs::path&) { return {}; }
std::shared_ptr<GlobPrefix> prepare_glob_prefix(SharedGlobOutput& out, const fs::path& directory) {
    return out.prefix(directory);
}
bool emit_glob_match(std::vector<fs::path>& out, fs::path absolute,
                     const fs::directory_entry*, const std::shared_ptr<GlobPrefix>&) {
    record_glob_prefix_event(GlobPrefixEvent::TerminalPath);
    out.push_back(std::move(absolute));
    return true;
}
bool emit_glob_match(SharedGlobOutput& out, fs::path absolute,
                     const fs::directory_entry* entry, const std::shared_ptr<GlobPrefix>& parent) {
    record_glob_prefix_event(GlobPrefixEvent::TerminalPath);
    out.emit(std::move(absolute), entry, parent);
    return true;
}
bool emit_glob_match(GlobVisitorOutput& out, fs::path absolute,
                     const fs::directory_entry*, const std::shared_ptr<GlobPrefix>&) {
    record_glob_prefix_event(GlobPrefixEvent::TerminalPath);
    return out.visitor(std::move(absolute));
}

template<class Output>
bool glob_walk(const fs::path& base, const std::vector<std::string>& parts,
               std::size_t i, Output& out,
               const std::vector<fs::directory_entry>* scanned = nullptr,
               const fs::directory_entry* terminal_entry = nullptr,
               const std::shared_ptr<GlobPrefix>& terminal_parent = {}) {
    if (i == parts.size()) {
        std::error_code ec;
        if (fs::exists(base, ec) && !ec)
            return emit_glob_match(out, fs::absolute(base).lexically_normal(), terminal_entry, terminal_parent);
        return true;
    }
    const auto& part = parts[i];
    if (part != "**" && !glob_has_magic(part)) {
        std::string literal;
        literal.reserve(part.size());
        for (std::size_t k = 0; k < part.size(); ++k) {
            if (part[k] == '\\' && k + 1 < part.size()) literal += part[++k];
            else literal += part[k];
        }
        return glob_walk(base / literal, parts, i + 1, out);
    }
    if (part == "**") {
        std::vector<fs::directory_entry> owned;
        if (!scanned) {
            std::error_code ec;
            if (!fs::is_directory(base, ec) || ec)
                return glob_walk(base, parts, i + 1, out, nullptr, terminal_entry, terminal_parent);
            for (fs::directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec)) owned.push_back(*it);
            scanned = &owned;
        }
        if (!glob_walk(base, parts, i + 1, out, scanned, terminal_entry, terminal_parent)) return false;
        for (const auto& e : *scanned) {
            const auto name = e.path().filename().string();
            if (!name.empty() && name[0] == '.') continue;
            std::error_code ec;
            if (e.is_directory(ec) && !e.is_symlink(ec))
                if (!glob_walk(e.path(), parts, i, out, nullptr, &e)) return false;
        }
        return true;
    }
    // Only zero-width ** components may emit this same candidate later.
    // Intern its parent lazily on the first actual terminal candidate;
    // unmatched directories never allocate a prefix descriptor.
    const bool terminal_suffix = std::all_of(parts.begin() + i + 1, parts.end(),
        [](const auto& component) { return component == "**"; });
    std::shared_ptr<GlobPrefix> directory_prefix;
    const auto consider = [&](const fs::directory_entry& e) {
        if (!glob_component_match(part, e.path().filename().string())) return true;
        if (terminal_suffix && !directory_prefix) directory_prefix = prepare_glob_prefix(out, base);
        return glob_walk(e.path(), parts, i + 1, out, nullptr, &e, directory_prefix);
    };
    if (scanned) {
        for (const auto& e : *scanned)
            if (!consider(e)) return false;
        return true;
    }
    if constexpr (std::is_same_v<Output, GlobVisitorOutput>) {
        std::error_code ec;
        if (!fs::is_directory(base, ec) || ec) return true;
        for (fs::directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec))
            if (!consider(*it)) return false;
        return true;
    } else {
        // Legacy traversal: scan this directory completely before descending,
        // so no directory handle stays open across recursive calls.
        std::vector<fs::directory_entry> owned;
        std::error_code ec;
        if (!fs::is_directory(base, ec) || ec) return true;
        for (fs::directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) owned.push_back(*it);
        for (const auto& e : owned)
            if (!consider(e)) return false;
        return true;
    }
}

}  // namespace

bool glob_component_match(const std::string& pattern,const std::string& name){
    if(!name.empty()&&name[0]=='.'&&(pattern.empty()||pattern[0]!='.'))return false;
    std::size_t p=0,n=0,star=std::string::npos,mark=0; bool escaped=false;
    while(n<name.size()){
        if(p<pattern.size()&&pattern[p]=='\\'&&!escaped){if(p+1<pattern.size()){++p;if(pattern[p]==name[n]){++p;++n;continue;}}}
        else if(p<pattern.size()&&pattern[p]=='?'){++p;++n;continue;}
        else if(p<pattern.size()&&pattern[p]=='*'){star=p++;mark=n;continue;}
        else if(p<pattern.size()&&pattern[p]==name[n]){++p;++n;continue;}
        if(star!=std::string::npos){p=star+1;n=++mark;continue;}return false;
    }
    while(p<pattern.size()&&pattern[p]=='*')++p;
    return p==pattern.size();
}

std::vector<fs::path> glob_expand(const fs::path& resolved_pattern){
    std::string g=resolved_pattern.generic_string();fs::path root=resolved_pattern.root_path();std::string rel=root.empty()?g:g.substr(root.generic_string().size());while(!rel.empty()&&rel.front()=='/')rel.erase(rel.begin());std::vector<std::string> parts;std::stringstream ss(rel);std::string part;while(std::getline(ss,part,'/'))if(!part.empty())parts.push_back(part);std::vector<fs::path> out;glob_walk(root.empty()?fs::path("."):root,parts,0,out);sort_glob_paths(out, [](const auto& path) -> const fs::path& { return path; });out.erase(std::unique(out.begin(),out.end()),out.end());return out;
}

#ifdef NIFT_TEST_GLOB_PREFIX_STATS
void record_glob_prefix_event(GlobPrefixEvent event) {
    switch(event) {
    case GlobPrefixEvent::TerminalPath: ++glob_prefix_stats.terminal_paths; break;
    case GlobPrefixEvent::PrefixNode: ++glob_prefix_stats.prefix_nodes; break;
    case GlobPrefixEvent::PrefixResolve: ++glob_prefix_stats.prefix_resolves; break;
    case GlobPrefixEvent::FullDisplayPath: ++glob_prefix_stats.full_display_paths; break;
    case GlobPrefixEvent::FinalValue: ++glob_prefix_stats.final_values; break;
    }
}
#endif

std::vector<GlobMatch> glob_expand_shared(const fs::path& resolved_pattern) {
    const auto generic = resolved_pattern.generic_string();
    const auto root = resolved_pattern.root_path();
    std::string relative = root.empty() ? generic : generic.substr(root.generic_string().size());
    while (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
    std::vector<std::string> parts;
    std::stringstream components(relative);
    std::string part;
    while (std::getline(components, part, '/')) if (!part.empty()) parts.push_back(part);
    SharedGlobOutput output;
    glob_walk(root.empty() ? fs::path(".") : root, parts, 0, output);
    sort_glob_paths(output.values, [](const auto& match) -> const fs::path& { return match.absolute; });
    output.values.erase(std::unique(output.values.begin(), output.values.end(),
        [](const auto& a, const auto& b) { return a.absolute == b.absolute; }), output.values.end());
    return std::move(output.values);
}

bool glob_visit(const fs::path& resolved_pattern, const GlobVisitor& visitor) {
    const auto generic = resolved_pattern.generic_string();
    const auto root = resolved_pattern.root_path();
    std::string relative = root.empty() ? generic : generic.substr(root.generic_string().size());
    while (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
    std::vector<std::string> parts;
    std::stringstream components(relative);
    std::string part;
    while (std::getline(components, part, '/')) if (!part.empty()) parts.push_back(part);
    GlobVisitorOutput output{visitor};
    return glob_walk(root.empty() ? fs::path(".") : root, parts, 0, output);
}

}  // namespace nift::detail
