#pragma once
#include "Json.h"
#include <filesystem>
#include <string>

bool load_json_file(const std::filesystem::path& path, json::Document& document, std::string& error);
bool save_json_file(const std::filesystem::path& path, const json::Document& document);
