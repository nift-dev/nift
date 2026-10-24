#pragma once
// CP17 VM file/stream resources (the typed-handle payloads for file(),
// ifstream() and ofstream()). No script representation: the handle table owns
// them and the natives operate through native_handle_arg/result.
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace nift::rt::handles {

struct FileResource {
    std::filesystem::path path;
    std::string mode, working, saved;
    std::size_t cursor = 0;
    bool open = false, dirty = false, existed_at_open = false;
    bool saved_is_working = false;
    void prepare_mutation() {
        if (saved_is_working) { saved = working; saved_is_working = false; }
    }
    void update_dirty() {
        // `open("w")` is dirty until saved even when both buffers are empty;
        // an absent file opened for append must still be created. The
        // saved-is-working flag records the explicit save/revert, so it is the
        // authoritative pending-write bit.
        dirty = (mode == "w" && !saved_is_working) ||  // rt-lint: allow-string-compare compile time
                (mode == "a" && !existed_at_open) ||  // rt-lint: allow-string-compare compile time
                (!saved_is_working && working != saved);
        if (!dirty) { saved_is_working = true; saved.clear(); }
    }
};

struct StreamResource {
    enum class Kind { Input, Output };
    Kind kind = Kind::Input;
    std::shared_ptr<std::ifstream> input;
    std::shared_ptr<std::ofstream> output;
    std::filesystem::path path;
    bool open = false;
    bool closed = false;
};

} // namespace nift::rt::handles
