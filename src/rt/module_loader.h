#pragma once
// Module source access for the Runtime 2.0 compiler (CP12 phase B, D-037):
// the single place a module's text is read and its identity computed. CP16's
// host-read interface replaces load_module_source when it lands; nothing else
// in src/rt reads module files.
#include <filesystem>
#include <string>

namespace nift::rt {

struct ModuleSource {
    bool found = false;   // the file exists and was read
    std::string text;
    std::string message;  // legacy's wording when !found
};

ModuleSource load_module_source(const std::filesystem::path& path);

// Legacy's stable_import_identity (src/ParserScript.cpp): weakly canonical
// (symlinks and `..` resolved), else the absolute normalised path;
// case-folded on Windows.
std::filesystem::path module_identity(const std::filesystem::path& path);

} // namespace nift::rt
