#pragma once
// The module graph of a compilation unit (CP12 phase B, D-037). Static
// imports are resolved before anything executes, with legacy's path rules
// (src/ParserScript.cpp execute_import_file): relative to the importing
// module's directory (the entry script's for the unit), `\` read as `/`,
// absolute and lexically normalised, canonical identity for cycles and the
// cache. Each module is read (rt/module_loader.h, the only file access) and
// parsed once per program by identity; read modules are the dependencies,
// unreadable ones are recorded as absent. What this cannot describe exactly
// is refused with a reason, before execution (legacy then runs the unit):
//   import-dynamic          the argument is not a plain string literal
//   import-package          a package name (no `/` or `\`, no extension)
//   import-nested           an import anywhere but the top level of a unit
//   import-outside-project  outside the enforced project root (builds)
//   import-missing          the module cannot be read
//   import-cycle            a module imports itself through the active chain
//   import-parse-error      the module does not parse
//   import-opaque:<class>   the module holds an Opaque node
//   import-reinitialised    temporary policy only (InitPolicy, D-041 #2)
//   import-collision        an export clashes with a different binding of the
//                           importer (the same declaration again is a rebind)
//   export-missing          `export(x)` names no earlier top-level binding,
//                           function or import of the module
//   export-duplicate / export-nested / export-struct (structs: B4)
//   module-return           `return <value>` outside functions in a module
//   import-limit            more than kMaxModules modules or kMaxModuleDepth
#include "rt/ast.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nift::rt {

// The one switch for module initialisation (D-041). Compilation is once per
// program under every policy; this only decides which units are accepted when
// legacy (until it initialises once) would run a module more than once.
enum class InitPolicy {
    // D-041: once per execution context; importers share the instance.
    // Module::legacy_initialisations > 1 marks where today's legacy differs.
    OncePerContext,
    // Temporary (D-041 #2), never part of the design: refuse units where
    // legacy would initialise a module again, for use only if the VM lands
    // before legacy's initialise-once. Removal condition: legacy
    // execute_import_file initialises once per context and both engines pass
    // the D-041 fixtures (D-041 #2). Must not survive into CP25.
    RefuseReinitialisation,
};

struct ModuleGraphOptions {
    InitPolicy policy = InitPolicy::OncePerContext;
    std::filesystem::path project_root;  // non-empty: imports must stay inside (legacy's enforced root)
};

struct ModuleImport {
    NodeId statement = kNoNode;  // the Import statement in the importer
    std::size_t module = 0;      // index into ModuleGraph::modules
};

struct Module {
    std::filesystem::path path;      // absolute, normalised (the import base is its directory)
    std::filesystem::path identity;  // module_identity(path)
    std::string source;              // owned; `ast` views it
    std::unique_ptr<Ast> ast;
    NodeId root = kNoNode;
    std::vector<ModuleImport> imports;      // in statement order
    std::vector<std::string_view> exports;  // in `export` order
    std::vector<std::size_t> export_origins; // per export: the module declaring it (re-exports keep theirs)
    std::size_t legacy_initialisations = 0; // how often legacy runs it per program run
};

struct ModuleGraph {
    bool ok = true;
    std::string reason;
    std::size_t where = 0;     // module of the refused construct (0: the entry unit)
    std::uint32_t offset = 0;  // source offset in that module
    // modules[0] is the entry unit: path/identity only (its Ast is the caller's).
    std::vector<std::unique_ptr<Module>> modules;
    std::vector<std::filesystem::path> dependencies;  // every module read, in first-read order
    std::vector<std::filesystem::path> absent;        // modules looked for and not readable
};

inline constexpr std::size_t kMaxModules = 256;
inline constexpr std::size_t kMaxModuleDepth = 64;

// `entry_path`: the unit's file (empty: relative imports resolve against the
// current directory, legacy's `<nift-sh>` rule).
ModuleGraph build_module_graph(const Ast& entry, NodeId root, const std::filesystem::path& entry_path,
                               const ModuleGraphOptions& options = {});

} // namespace nift::rt
