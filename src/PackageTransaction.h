#pragma once

#include "Json.h"

#include <filesystem>
#include <string>
#include <vector>

class PackageTransaction {
public:
    enum class Kind { Replace, Remove };
    struct Operation {
        std::string name;
        Kind kind = Kind::Replace;
    };

    explicit PackageTransaction(std::filesystem::path project_root);
    ~PackageTransaction();

    PackageTransaction(const PackageTransaction&) = delete;
    PackageTransaction& operator=(const PackageTransaction&) = delete;

    bool acquire(std::string& error);
    bool acquire_read(std::string& error);
    bool lock_identity_valid() const;
    bool create_staging(std::filesystem::path& staging_root, std::string& error);
    bool commit(const std::vector<Operation>& operations,
                const json::Document& manifest,
                const json::Document& lock,
                std::string& error);

private:
    bool recover(std::string& error);
    bool apply_journal(const json::Document& journal, std::string& error);
    void release();

    std::filesystem::path project_root_;
    std::filesystem::path nift_root_;
    std::filesystem::path package_root_;
    std::filesystem::path journal_path_;
    std::filesystem::path staging_root_;
    void* lock_file_ = nullptr;
    bool locked_ = false;
    bool journal_published_ = false;
    bool exclusive_ = false;
    bool old_manifest_missing_ = true;
    bool old_lock_missing_ = true;
    std::string old_manifest_;
    std::string old_lock_;
};
