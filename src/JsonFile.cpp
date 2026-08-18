#include "JsonFile.h"
#include "FileSystem.h"

bool load_json_file(const std::filesystem::path& path, json::Document& document, std::string& error) {
    if (!filesystem::path_exists(path)) {
        error = "file does not exist";
        return false;
    }
    if (!filesystem::file_readable(path)) {
        error = "file is not readable";
        return false;
    }
    return json::Document::parse(filesystem::read_file(path), document, error);
}

bool save_json_file(const std::filesystem::path& path, const json::Document& document) {
    return filesystem::write_file(path, document.dump(2) + "\n");
}
