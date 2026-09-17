#pragma once
#include "Types.h"
#include "Json.h"
#include "ProjectRead.h"
#include "FrontMatter.h"
#include "ContentModel.h"
#include <filesystem>
#include <memory>
#include <map>
#include <set>

inline std::shared_ptr<const json::Document> make_project_value(const std::filesystem::path& root, const Config& config, const std::vector<TrackedInfo>& tracked) {
    json::Document p=json::Document::make_object();
    p["name"]=json::Document(root.filename().generic_string()); p["root"]=json::Document(root.generic_string()); p["output"]=json::Document(config.output_dir); p["mode"]=json::Document(config.incremental_mode);
    p["files"]=json::Document::make_array(); p["schemas"]=json::Document::make_object(); p["taxonomies"]=json::Document::make_object(); p["taxonomy_info"]=json::Document::make_object(); p["content"]=json::Document::make_object(); p["content"]["all"]=json::Document::make_array();
    auto model=content_model::load(root,config.schema_files,config.taxonomy_files);
    for(const auto& kv:model.schemas){json::Document s=json::Document::make_object();s["name"]=json::Document(kv.first);s["source"]=json::Document(kv.second.source);s["fields"]=json::Document::make_array();for(const auto&fld:kv.second.fields){json::Document f=json::Document::make_object();f["name"]=json::Document(fld.name);f["type"]=json::Document(fld.type);f["array"]=json::Document(fld.array);f["required"]=json::Document(fld.required);if(!fld.taxonomy.empty())f["taxonomy"]=json::Document(fld.taxonomy);s["fields"].push_back(f);}p["schemas"][kv.first]=s;}
    for(const auto& kv:model.taxonomies){p["taxonomies"][kv.first]=json::Document::make_array();json::Document t=json::Document::make_object();t["name"]=json::Document(kv.first);t["source"]=json::Document(kv.second.source);t["hierarchical"]=json::Document(kv.second.hierarchical);p["taxonomy_info"][kv.first]=t;}
    if(!model.errors.empty()){p["errors"]=json::Document::make_array();for(auto&e:model.errors)p["errors"].push_back(json::Document(e));}
    std::map<std::string,std::map<std::string,std::vector<json::Document>>> tax_content;
    for(const auto& info:tracked){
        json::Document f=json::Document::make_object(); const auto source=project_read::content_path_of(root,config,info); const auto output=project_read::output_path_of(root,config,info);
        f["name"]=json::Document(info.name); f["title"]=json::Document(info.title); f["path"]=json::Document(project_read::relative_of(root,source)); f["output_path"]=json::Document(project_read::relative_of(root,output)); f["url"]=json::Document("/"+project_read::relative_of(root,output).substr(config.output_dir.size())); f["extension"]=json::Document(source.extension().generic_string());
        json::Document metadata=json::Document::make_object(); std::string fm_error; std::string body; const std::string source_text=filesystem::file_exists(source)?filesystem::read_file(source):std::string{}; auto inline_fm=frontmatter::parse_inline(source_text); body=inline_fm.present?inline_fm.body:source_text;
        if(info.frontmatter && inline_fm.present) metadata["_error"]=json::Document("multiple front matter sources"); else if(info.frontmatter){if(!frontmatter::load_external(root,*info.frontmatter,metadata,fm_error))metadata["_error"]=json::Document(fm_error);} else if(inline_fm.present&&inline_fm.error.empty())metadata=inline_fm.value;else if(!inline_fm.error.empty())metadata["_error"]=json::Document(inline_fm.error);
        std::string type; if(info.type)type=*info.type; if(metadata.has("type")){if(!metadata["type"].is_string())metadata["_error"]=json::Document("front matter type must be a string");else if(!type.empty()&&metadata["type"].string!=type)metadata["_error"]=json::Document("tracked type conflicts with front matter type");else type=metadata["type"].string;} if(!type.empty())metadata["type"]=json::Document(type);
        if(!type.empty()&&!metadata.has("_error")){auto si=model.schemas.find(type);if(si==model.schemas.end()){if(!config.schema_files.empty())metadata["_error"]=json::Document("unknown content schema/type: "+type);}else{std::string ve;if(!content_model::validate(metadata,si->second,model,ve))metadata["_error"]=json::Document(ve);}}
        if(info.frontmatter)f["frontmatter_source"]=json::Document(*info.frontmatter); f["metadata"]=metadata; p["files"].push_back(f);
        if(!type.empty()&&!metadata.has("_error")){
            json::Document c=json::Document::make_object(); c["type"]=json::Document(type);c["path"]=f["path"];c["output_path"]=f["output_path"];c["url"]=f["url"];c["content"]=json::Document(body);c["metadata"]=metadata;
            static const std::set<std::string> reserved={"type","path","output_path","url","content","metadata"};for(const auto&kv:metadata.object)if(!reserved.count(kv.first))c[kv.first]=kv.second;
            if(!p["content"].has(type))p["content"][type]=json::Document::make_array();p["content"][type].push_back(c);p["content"]["all"].push_back(c);
            const auto& sch=model.schemas[type];for(const auto&fld:sch.fields)if(fld.type=="taxonomy"&&metadata.has(fld.name)){auto add=[&](const json::Document&v){if(v.is_string())tax_content[fld.taxonomy][v.string].push_back(c);};if(fld.array&&metadata[fld.name].is_array())for(const auto&v:metadata[fld.name].array)add(v);else add(metadata[fld.name]);}
        }
    }
    for(auto&tk:tax_content){if(!p["taxonomies"].has(tk.first))continue;auto& tax=p["taxonomies"][tk.first];std::set<std::string> slugs;for(auto&termkv:tk.second){json::Document term=json::Document::make_object();term["name"]=json::Document(termkv.first);std::string slug;for(char ch:termkv.first){unsigned char c=(unsigned char)ch;if(c>=128)slug+=ch;else if(std::isalnum(c))slug+=(char)std::tolower(c);else if((ch==' '||ch=='_'||ch=='/'||ch=='-')&&!slug.empty()&&slug.back()!='-')slug+='-';}while(!slug.empty()&&slug.back()=='-')slug.pop_back();if(slug.empty()||!slugs.insert(slug).second){if(!p.has("errors"))p["errors"]=json::Document::make_array();p["errors"].push_back(json::Document("taxonomy "+tk.first+": duplicate or empty slug for term '"+termkv.first+"'"));continue;}term["slug"]=json::Document(slug);term["content"]=json::Document::make_array();for(auto&c:termkv.second)term["content"].push_back(c);if(model.taxonomies[tk.first].hierarchical&&termkv.first.find('/')!=std::string::npos){auto pos=termkv.first.rfind('/');term["parent"]=json::Document(termkv.first.substr(0,pos));}tax.push_back(term);} }
    return std::make_shared<const json::Document>(std::move(p));
}
