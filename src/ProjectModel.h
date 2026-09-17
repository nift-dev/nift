#pragma once
#include "Types.h"
#include "Json.h"
#include "ProjectRead.h"
#include "FrontMatter.h"
#include <filesystem>
#include <memory>

inline std::shared_ptr<const json::Document> make_project_value(const std::filesystem::path& root, const Config& config, const std::vector<TrackedInfo>& tracked) {
    json::Document p=json::Document::make_object();
    p["name"]=json::Document(root.filename().generic_string());
    p["root"]=json::Document(root.generic_string());
    p["output"]=json::Document(config.output_dir);
    p["mode"]=json::Document(config.incremental_mode);
    p["files"]=json::Document::make_array();
    for(const auto& info:tracked){
        json::Document f=json::Document::make_object();
        const auto source=project_read::content_path_of(root,config,info);
        const auto output=project_read::output_path_of(root,config,info);
        f["name"]=json::Document(info.name); f["title"]=json::Document(info.title);
        f["path"]=json::Document(project_read::relative_of(root,source));
        f["output_path"]=json::Document(project_read::relative_of(root,output));
        f["url"]=json::Document("/"+project_read::relative_of(root,output).substr(config.output_dir.size()));
        f["extension"]=json::Document(source.extension().generic_string());
        json::Document metadata=json::Document::make_object(); std::string fm_error;
        const std::string source_text=filesystem::file_exists(source)?filesystem::read_file(source):std::string{};
        auto inline_fm=frontmatter::parse_inline(source_text);
        if(info.frontmatter && inline_fm.present) { metadata["_error"]=json::Document("multiple front matter sources"); }
        else if(info.frontmatter) { if(!frontmatter::load_external(root,*info.frontmatter,metadata,fm_error)) metadata["_error"]=json::Document(fm_error); }
        else if(inline_fm.present && inline_fm.error.empty()) metadata=inline_fm.value;
        else if(!inline_fm.error.empty()) metadata["_error"]=json::Document(inline_fm.error);
        if(info.type){ if(metadata.has("type") && metadata["type"].is_string() && metadata["type"].string!=*info.type) metadata["_error"]=json::Document("tracked type conflicts with front matter type"); else metadata["type"]=json::Document(*info.type); }
        f["metadata"]=std::move(metadata);
        p["files"].push_back(f);
    }
    return std::make_shared<const json::Document>(std::move(p));
}
