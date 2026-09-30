#include "RuntimeJson.h"
#include "Parser.h"
#include "ParserHelpers.h"
#include "FrontMatter.h"
#include "FileSystem.h"
#include "JsonSchema.h"
#include <markup/Markup.h>
#include <nift/context.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

using nift::detail::append_indented;
using nift::detail::built_in_metadata_name;
using nift::detail::call_runtime_host;
using nift::detail::compare_sort_keys;
using nift::detail::entity;
using nift::detail::insertion_indent;
using nift::detail::kMaxCallableDepth;
using nift::detail::make_loop_metadata;
using nift::detail::nift_atomic_add_sub_checked;
using nift::detail::nift_binding_type;
using nift::detail::nift_type_assignable;
using nift::detail::normalize_control_block_body;
using nift::detail::parse_callable_parameters;
using nift::detail::parse_for_collection_clause;
using nift::detail::parse_parameters;
using nift::detail::parse_runtime_json;
using nift::detail::reserved_binding_name;
using nift::detail::runtime_contains_bytes;
using nift::detail::runtime_bytes_string;
using nift::detail::runtime_scalar_key;
using nift::detail::sortable_scalar;
using nift::detail::valid_binding_identifier;

bool Parser::resolve_pagination_value(const std::string& expression,
                                      std::shared_ptr<const nift::RuntimeValue>& value) const {
    if (!pagination_context_active_) return false;
    if (expression == "paginate.items") value = std::make_shared<const nift::RuntimeValue>(pagination_items_text_);
    else if (expression == "paginate.current") value = std::make_shared<const nift::RuntimeValue>(static_cast<double>(pagination_current_));
    else if (expression == "paginate.total") value = std::make_shared<const nift::RuntimeValue>(static_cast<double>(pagination_total_));
    else if (expression == "paginate.first") value = std::make_shared<const nift::RuntimeValue>(pagination_current_ == 1);
    else if (expression == "paginate.last") value = std::make_shared<const nift::RuntimeValue>(pagination_current_ == pagination_total_);
    else if (expression == "paginate.previous") value = std::make_shared<const nift::RuntimeValue>(static_cast<double>(pagination_current_ > 1 ? pagination_current_ - 1 : 1));
    else if (expression == "paginate.next") value = std::make_shared<const nift::RuntimeValue>(static_cast<double>(pagination_current_ < pagination_total_ ? pagination_current_ + 1 : pagination_total_));
    else return false;
    return true;
}

std::string Parser::path_to_page(std::size_t page) {
    if (!pagination_context_active_ || !tracked_info_.paginate.has_value()) {
        result_.ok = false;
        result_.error = {tracked_info_.name, {}, 0, "pathtopage: only available while rendering pagination"};
        return {};
    }
    if (page < 1 || page > pagination_total_) {
        result_.ok = false;
        result_.error = {tracked_info_.name, {}, 0, "pathtopage: page must be between 1 and " + std::to_string(pagination_total_)};
        return {};
    }
    const fs::path destination = host_.pagination_output_path(tracked_info_, page);
    const fs::path base = pagination_current_output_.empty()
        ? host_.output_path(tracked_info_).parent_path()
        : pagination_current_output_.parent_path();
    if (page == 1 && (tracked_info_.name == "/" || (!tracked_info_.name.empty() && tracked_info_.name.back() == '/'))) {
        fs::path relative_path = destination.parent_path().lexically_normal().lexically_relative(base.lexically_normal());
        std::string relative = relative_path.empty() ? destination.parent_path().generic_string() : relative_path.generic_string();
        if (relative.empty() || relative == ".") return "./";
        if (relative.back() != '/') relative += '/';
        return relative;
    }
    fs::path relative_path = destination.lexically_normal().lexically_relative(base.lexically_normal());
    std::string relative = relative_path.empty() ? destination.generic_string() : relative_path.generic_string();
    if (relative.find('/') == std::string::npos && relative.rfind("..", 0) != 0) relative = "./" + relative;
    return relative;
}


std::string Parser::path_to(const std::string& argument, const std::string& directive) {
    if (!host_.has_output_context()) {
        result_.ok = false;
        result_.error = {tracked_info_.name, {}, 0, "@" + directive + " requires a path context; set the current output on the render context to resolve '" + argument + "'"};
        return {};
    }
    const fs::path output = host_.output_path(tracked_info_);
    const bool absolute_404 = (tracked_info_.name == "404");
    const fs::path base = output.parent_path();
    fs::path destination;
    bool index_page = false;

    if (const auto target = host_.tracked_output_path(argument)) {
        destination = target->path;
        index_page = target->index_page;
    } else {
        destination = (host_.root() / argument).lexically_normal();
        if (!filesystem::path_within(host_.root(), destination)) {
            result_.ok = false;
            result_.error = {tracked_info_.name, {}, 0, directive + ": path must stay inside the Nift project: " + argument};
            return {};
        }
        if (!host_.source_exists(destination)) {
            result_.ok = false;
            result_.error = {tracked_info_.name, {}, 0, "'" + argument + "' is neither a tracked name nor a file that exists"};
            return {};
        }
    }

    // A deployed 404 document is served at any request depth, so a relative
    // path from its on-disk location is meaningless: the browser might think it
    // is at /docs/foo/bar while the file physically lives at /404.html. Emit a
    // root-absolute web path instead. All existence/dependency checking above
    // is unchanged; only the path representation differs. Targets outside the
    // output directory keep the ordinary relative behaviour.
    if (absolute_404) {
        const fs::path web_root = (host_.root() / host_.output_dir()).lexically_normal();
        fs::path web_rel = destination.lexically_normal().lexically_relative(web_root.lexically_normal());
        const std::string web_rel_str = web_rel.generic_string();
        const bool escapes_web_root = web_rel_str == ".." || web_rel_str.rfind("../", 0) == 0;
        if (!escapes_web_root) {
            if (index_page) {
                const std::string dir = web_rel.parent_path().generic_string();
                if (dir.empty() || dir == ".") return "/";
                std::string p = "/" + dir;
                if (p.back() != '/') p += '/';
                return p;
            }
            return "/" + web_rel_str;
        }
    }

    fs::path relative_path = destination.lexically_normal().lexically_relative(base.lexically_normal());
    std::string relative = relative_path.empty() ? destination.generic_string() : relative_path.generic_string();

    if (index_page && destination.filename().generic_string().rfind("index", 0) == 0) {
        relative_path = destination.parent_path().lexically_normal().lexically_relative(base.lexically_normal());
        relative = relative_path.empty() ? destination.parent_path().generic_string() : relative_path.generic_string();
        if (relative.empty() || relative == ".") return "./";
        if (relative.back() != '/') relative += '/';
        return relative;
    }
    if (relative.find('/') == std::string::npos && relative.rfind("..", 0) != 0) relative = "./" + relative;
    return relative;
}

RenderResult Parser::parse(const std::string& source, const fs::path& source_path, int depth) {
    if (depth > 64) {
        fail(source_path, source, 0, "maximum template parse depth exceeded (possible recursion)");
        return result_;
    }

    const int base_code_block_depth = code_block_depth_;
    std::size_t open_code_offset = 0;
    std::string output;
    output.reserve(source.size() + 64);

    for (std::size_t i = 0; i < source.size() && result_.ok;) {
        if (pending_control_.kind != ControlFlow::None) break;
        if (in_fragment_body_ && source.compare(i, 6, "return") == 0 &&
            (i + 6 == source.size() || source[i + 6] == ';' || source[i + 6] == '\n' || source[i + 6] == '\r' || std::isspace(static_cast<unsigned char>(source[i + 6])))) {
            std::size_t e = i + 6;
            while (e < source.size() && (source[e] == ' ' || source[e] == '\t')) ++e;
            if (e < source.size() && source[e] != ';' && source[e] != '\n' && source[e] != '\r') {
                fail(source_path, source, i, "fragment return cannot have a value"); break;
            }
            pending_control_.kind = ControlFlow::Return; pending_control_.value.reset(); break;
        }
        if (source.compare(i, 5, "break") == 0 &&
            (i + 5 == source.size() || source[i + 5] == ';' || source[i + 5] == '\n' || source[i + 5] == '\r')) {
            if (loop_depth_ <= 0) { fail(source_path, source, i, "break is only valid inside a loop"); break; }
            pending_control_.kind = ControlFlow::Break;
            break;
        }
        if (source.compare(i, 8, "continue") == 0 &&
            (i + 8 == source.size() || source[i + 8] == ';' || source[i + 8] == '\n' || source[i + 8] == '\r')) {
            if (loop_depth_ <= 0) { fail(source_path, source, i, "continue is only valid inside a loop"); break; }
            pending_control_.kind = ControlFlow::Continue;
            break;
        }
        if (i + 1 < source.size() && source[i] == '\\' && (source[i + 1] == '@' || source[i + 1] == '$' || source[i + 1] == '#')) {
            output += source[i + 1];
            i += 2;
            continue;
        }

        if (source.compare(i, 3, "@/*") == 0) {
            const auto end = source.find("*/", i + 3);
            if (end == std::string::npos) { fail(source_path, source, i, "open comment '@/*' has no close '*/'"); break; }
            i = end + 2;
            continue;
        }
        if (source.compare(i, 3, "@//") == 0) {
            const auto end = source.find('\n', i);
            i = end == std::string::npos ? source.size() : end;
            continue;
        }


        // Match stripped Nift's <pre*> handling. While inside a pre block,
        // literal '<' characters are escaped except for <code>/</code> tags and
        // the outer </pre>. Nift expressions still parse normally, so \@ / \$
        // remain the way to show Nift syntax literally in code examples.
        if (source.compare(i, 4, "<!--") == 0) {
            ++html_comment_depth_;
        }
        if (source.compare(i, 3, "-->") == 0 && html_comment_depth_ > 0) {
            --html_comment_depth_;
        }
        if (source[i] == '<' && html_comment_depth_ == 0) {
            const bool closes_pre = source.compare(i + 1, 4, "/pre") == 0 &&
                i + 5 < source.size() && source[i + 5] == '>';
            const bool opens_pre = source.compare(i + 1, 3, "pre") == 0 &&
                i + 4 < source.size() &&
                (source[i + 4] == '>' || source[i + 4] == ' ' || source[i + 4] == '\t' ||
                 source[i + 4] == '\r' || source[i + 4] == '\n');

            if (closes_pre) {
                --code_block_depth_;
                if (code_block_depth_ < base_code_block_depth) {
                    fail(source_path, source, i, "</pre> close tag has no preceding <pre*> open tag");
                    code_block_depth_ = base_code_block_depth;
                    break;
                }
            }

            const bool code_tag = source.compare(i + 1, 4, "code") == 0 ||
                                  source.compare(i + 1, 5, "/code") == 0;
            if (code_block_depth_ > 0 && !code_tag) output += "&lt;";
            else output.push_back('<');

            if (opens_pre) {
                if (code_block_depth_ == base_code_block_depth) open_code_offset = i;
                ++code_block_depth_;
            }
            ++i;
            continue;
        }

        if (source.compare(i, 7, "@script") == 0 && (i+7==source.size() || std::isspace(static_cast<unsigned char>(source[i+7])) || source[i+7]=='{')) {
            std::size_t bo=i+7; while(bo<source.size()&&std::isspace(static_cast<unsigned char>(source[bo])))++bo; std::size_t bc=0;
            if(bo>=source.size()||source[bo]!='{'||!find_balanced(source,bo,'{','}',bc)){fail(source_path,source,i,"@script requires a balanced block");break;}
            if(pending_control_.kind!=ControlFlow::None) pending_control_={};
            std::unordered_set<std::string> pre_script_files; for(const auto& kv:file_instances_)pre_script_files.insert(kv.first);
            ++function_call_depth_;
            const bool saved_strict=strict_script_mode_; strict_script_mode_=true;
            auto nested=execute_native_program(source.substr(bo+1,bc-bo-1),source_path,depth+1);
            strict_script_mode_=saved_strict;
            --function_call_depth_;
            if(nested.ok){for(auto& kv:file_instances_)if(!pre_script_files.count(kv.first)&&kv.second->open){auto f=kv.second;f->open=false;f->dirty=false;f->mode.clear();f->working.clear();f->saved.clear();f->cursor=0;nested.ok=false;nested.error.message="managed file left open at @script completion: "+f->path.generic_string();break;}}
            if(!nested.ok){result_=nested;break;}
            if(pending_control_.kind==ControlFlow::Return){
                if(pending_control_.value) { const auto& rv=*pending_control_.value;if(rv.is_bytes()||rv.is_array()||rv.is_object()||(rv.is_string()&&rv.string.rfind("\x1fnift:",0)==0)){pending_control_={};fail(source_path,source,i,"@script return value is not directly renderable");break;}output += render_expression_value(rv); }
                pending_control_={};
            }
            i=bc+1; continue;
        }

        if (source.compare(i, 16, "@__bare_return()") == 0) {
            if(function_call_depth_<=0){fail(source_path,source,i,"return is only valid in script/function execution");break;}
            pending_control_.kind=ControlFlow::Return; pending_control_.value.reset(); i+=16; break;
        }

        if (source.compare(i, 12, "@__nift_cmd(") == 0) {
            // Executable-path command style from script land (e.g. ./generate.f
            // foo bar): ordinary OS process execution. Never in-process Nift
            // evaluation. Output is streamed; a non-zero exit status does not
            // abort the script (matching the REPL shell), but a process that
            // cannot be launched is a hard error.
            std::size_t cc=0; if(!find_balanced(source,i+11,'(',')',cc)){fail(source_path,source,i,"@__nift_cmd has no matching ')'");break;}
            const std::string raw=source.substr(i+12,cc-(i+12));
            if(std::getenv("NIFT_NO_PROCESS")){fail(source_path,source,i,"external process execution disabled");break;}
            // Quote-aware tokenization of the command line.
            std::vector<std::string> toks; std::string cur; bool sq=false,dq=false,esc=false;
            for(std::size_t t=0;t<raw.size();++t){char c=raw[t];if(esc){cur+=c;esc=false;continue;}if(c=='\\'&&!sq){esc=true;continue;}if(c=='\''&&!dq){sq=!sq;continue;}if(c=='"'&&!sq){dq=!dq;continue;}if((c==' '||c=='\t'||c=='\r'||c=='\n')&&!sq&&!dq){if(!cur.empty()){toks.push_back(cur);cur.clear();}continue;}cur+=c;}if(!cur.empty())toks.push_back(cur);
            if(toks.empty()){fail(source_path,source,i,"empty command");break;}
            ProcessSpec spec; spec.program=toks.front(); spec.args.assign(toks.begin()+1,toks.end());
            auto pr=nift_run_process(spec,true,true);
            if(!pr.launched){fail(source_path,source,i,pr.error.empty()?("cannot launch command: "+spec.program):pr.error);break;}
            i=cc+1; continue;
        }

        if (source.compare(i, 10, "@__export(") == 0) {
            std::size_t ec=0; if(!find_balanced(source,i+9,'(',')',ec)){fail(source_path,source,i,"export has no matching ')'");break;}
            if(!in_import_program_ && !standalone_script_host_){fail(source_path,source,i,"export() is only valid in an imported/external script");break;}
            const std::string name=trim_copy(source.substr(i+10,ec-(i+10)));
            if(!valid_binding_identifier(name)){fail(source_path,source,i,"export requires a binding name");break;}
            if(std::find(requested_exports_.begin(),requested_exports_.end(),name)!=requested_exports_.end()){fail(source_path,source,i,"duplicate export: "+name);break;}
            bool export_found=false;
            for(auto scope=variable_scopes_.rbegin();scope!=variable_scopes_.rend()&&!export_found;++scope) if(scope->count(name))export_found=true;
            if(callables_.count(name)||structs_.count(name))export_found=true;
            if(!export_found){fail(source_path,source,i,"export names no existing binding: "+name);break;}
            requested_exports_.push_back(name); i=ec+1; continue;
        }

        const bool bare_import=strict_script_mode_&&source.compare(i,6,"import")==0&&(i+6==source.size()||(!std::isalnum(static_cast<unsigned char>(source[i+6]))&&source[i+6]!='_'));
        if (bare_import || source.compare(i, 8, "@import(") == 0) {
            std::size_t po=bare_import?i+6:i+7;while(po<source.size()&&std::isspace(static_cast<unsigned char>(source[po])))++po;
            auto import_fail=[&](const std::string& message,std::size_t length){fail(source_path,source,i,message);if(bare_import)result_.error.source_length=length;};
            std::size_t ec=0; if(po>=source.size()||source[po]!='('||!find_balanced(source,po,'(',')',ec)){import_fail(bare_import?"import has no matching ')'":"@import has no matching ')'",6);break;}
            std::string arg=trim_copy(source.substr(po+1,ec-(po+1))); nift::RuntimeValue pv; std::string pe;
            if(!evaluate_expression(arg,pv,pe)||!pv.is_string()){import_fail(bare_import?"import path must be a string expression":"@import path must be a string expression",ec-i+1);break;}
            if(!execute_import_file(pv.string,source_path,depth+1,!bare_import,pe)){import_fail((bare_import?"import: ":"@import: ")+pe,ec-i+1);break;}
            i=ec+1; continue;
        }

        if(source.compare(i,6,"@enum(")==0){
            std::size_t hc=0;if(!find_balanced(source,i+5,'(',')',hc)){fail(source_path,source,i,"enum definition has no matching ')'");break;}const std::string name=trim_copy(source.substr(i+6,hc-(i+6)));if(!valid_binding_identifier(name)){fail(source_path,source,i,"invalid enum name");break;}std::size_t bo=hc+1;while(bo<source.size()&&std::isspace((unsigned char)source[bo]))++bo;std::size_t bc=0;if(bo>=source.size()||source[bo]!='{'||!find_balanced(source,bo,'{','}',bc)){fail(source_path,source,i,"enum definition requires a block");break;}
            if(variable_scopes_.empty())variable_scopes_.emplace_back();
            if(variable_scopes_.back().count(name)){fail(source_path,source,i,"binding already declared in this scope: "+name);break;}
            std::string body=source.substr(bo+1,bc-bo-1);for(char& c:body)if(c=='\n'||c==';')c=',';bool ok=false;auto items=parse_parameters(body,ok);if(!ok||items.empty()){fail(source_path,source,i,"enum requires at least one member");break;}nift::RuntimeValue ns=nift::RuntimeValue::make_object();std::set<std::int64_t> used;std::int64_t next=0;bool good=true;
            for(auto raw:items){raw=trim_copy(raw);if(raw.empty())continue;auto eq=raw.find('=');std::string member=trim_copy(eq==std::string::npos?raw:raw.substr(0,eq));if(!valid_binding_identifier(member)||ns.has(member)){fail(source_path,source,i,"invalid or duplicate enum member: "+member);good=false;break;}std::int64_t val=next;if(eq!=std::string::npos){std::string vs=trim_copy(raw.substr(eq+1));auto pr=std::from_chars(vs.data(),vs.data()+vs.size(),val);if(pr.ec!=std::errc()||pr.ptr!=vs.data()+vs.size()){fail(source_path,source,i,"enum values must be signed integer literals");good=false;break;}}if(!used.insert(val).second){fail(source_path,source,i,"duplicate enum integer value: "+std::to_string(val));good=false;break;}ns[member]=nift::RuntimeValue(std::string("\x1fnift:enum:")+name+":"+member+":"+std::to_string(val));if(val==std::numeric_limits<std::int64_t>::max()){next=val;}else next=val+1;}
            if(ns.object.empty()){fail(source_path,source,i,"enum requires at least one member");good=false;}
            if(!good)break;
            auto sp=std::make_shared<nift::RuntimeValue>(std::move(ns));variable_scopes_.back().emplace(name,VariableBinding{sp,nift_binding_type_from_text("{}",*sp),false,true});i=bc+1;continue;
        }

        if (source.compare(i, 8, "@struct(") == 0) {
            std::size_t hc = 0;
            if (!find_balanced(source, i + 7, '(', ')', hc)) { fail(source_path, source, i, "struct definition has no matching ')'"); break; }
            const std::string struct_name = trim_copy(source.substr(i + 8, hc - (i + 8)));
            if (!valid_binding_identifier(struct_name)) { fail(source_path, source, i, "invalid struct name"); break; }
            std::size_t bo = hc + 1; while (bo < source.size() && std::isspace(static_cast<unsigned char>(source[bo]))) ++bo;
            std::size_t bc = 0;
            if (bo >= source.size() || source[bo] != '{' || !find_balanced(source, bo, '{', '}', bc)) { fail(source_path, source, i, "struct definition requires a block"); break; }
            StructDefinition def; def.name = struct_name;
            const std::string body = source.substr(bo + 1, bc - bo - 1);
            std::size_t p = 0; bool struct_ok = true;
            while (p < body.size()) {
                while (p < body.size() && std::isspace(static_cast<unsigned char>(body[p]))) ++p;
                if (p >= body.size()) break;
                bool priv = false;
                if (body.compare(p, 8, "private ") == 0) { priv = true; p += 8; while (p < body.size() && std::isspace(static_cast<unsigned char>(body[p]))) ++p; }
                if (body.compare(p, 3, "fn(") == 0) {
                    std::size_t mhc = 0; if (!find_balanced(body, p + 2, '(', ')', mhc)) { fail(source_path, source, i, "struct method has malformed signature"); struct_ok=false; break; }
                    const std::string sig = trim_copy(body.substr(p + 3, mhc - (p + 3))); const auto lp=sig.find('(');
                    if(lp==std::string::npos||sig.back()!=')'){fail(source_path,source,i,"struct method signature must be name(args)");struct_ok=false;break;}
                    const std::string mn=trim_copy(sig.substr(0,lp)); std::vector<std::string> ps; std::string variadic_param; const bool pok=parse_callable_parameters(sig.substr(lp+1,sig.size()-lp-2),ps,variadic_param);
                    if(variadic_param=="this"||std::find(ps.begin(),ps.end(),"this")!=ps.end()){fail(source_path,source,i,"struct method parameters cannot be named 'this'");struct_ok=false;break;}
                    std::size_t mbo=mhc+1; while(mbo<body.size()&&std::isspace(static_cast<unsigned char>(body[mbo])))++mbo; std::size_t mbc=0;
                    if(!valid_binding_identifier(mn)||!pok||mbo>=body.size()||body[mbo]!='{'||!find_balanced(body,mbo,'{','}',mbc)){fail(source_path,source,i,"invalid struct method");struct_ok=false;break;}
                    const bool ctor=mn==struct_name; if(ctor&&!variadic_param.empty()){fail(source_path,source,i,"struct constructors cannot be variadic");struct_ok=false;break;}if(ctor && def.methods.find(struct_name)!=def.methods.end()){fail(source_path,source,i,"struct may define at most one constructor");struct_ok=false;break;}
                    const auto mb=normalize_control_block_body(body.substr(mbo+1,mbc-mbo-1));
                    def.methods[mn]=StructMethod{Callable{ps,variadic_param,mb.text,source_path,false,false,{}},priv,ctor}; p=mbc+1; continue;
                }
                // Struct fields are separated by top-level newlines or
                // semicolons; a ';' or newline inside a nested lambda/block must
                // not split the field. Scan with paren/bracket/brace/quote depth.
                std::size_t eol=p; { int par=0,br=0,bc=0; bool q=false; char qc=0; for(;eol<body.size();++eol){char ch=body[eol];if(q){if(ch=='\\'){++eol;continue;}if(ch==qc)q=false;continue;}if(ch=='\''||ch=='"'){q=true;qc=ch;continue;}if(ch=='(')++par;else if(ch==')')--par;else if(ch=='[')++br;else if(ch==']')--br;else if(ch=='{')++bc;else if(ch=='}')--bc;else if(!par&&!br&&!bc&&(ch=='\n'||ch==';'))break;} }
                std::string line=trim_copy(body.substr(p,eol-p)); const auto dp=line.find(":=");
                if(dp==std::string::npos){fail(source_path,source,i,"struct fields require 'name := initializer'");struct_ok=false;break;}
                std::string fn=trim_copy(line.substr(0,dp)), init=trim_copy(line.substr(dp+2));
                if(!valid_binding_identifier(fn)||init.empty()){fail(source_path,source,i,"invalid struct field declaration");struct_ok=false;break;}
                for(const auto& f:def.fields)if(f.name==fn){fail(source_path,source,i,"duplicate struct field: "+fn);struct_ok=false;break;}
                if(!struct_ok) break;
                def.fields.push_back(StructField{fn,init,priv}); p=eol<body.size()?eol+1:eol;
            }
            if(!struct_ok) break;
            structs_[struct_name]=std::move(def); i=bc+1; continue;
        }

        if (source.compare(i, 4, "@fn(") == 0 || source.compare(i, 11, "@fn[async](") == 0 || source.compare(i, 10, "@fragment(") == 0) {
            const bool fragment = source.compare(i, 10, "@fragment(") == 0;
            const bool async = !fragment && source.compare(i, 11, "@fn[async](") == 0;
            const std::size_t open = i + (fragment ? 9 : (async ? 10 : 3));
            std::size_t header_close = 0; if (!find_balanced(source, open, '(', ')', header_close)) { fail(source_path, source, i, "callable definition has no matching ')'"); break; }
            const std::string signature = trim_copy(source.substr(open + 1, header_close - open - 1)); const auto lp = signature.find('(');
            if (lp == std::string::npos || signature.back() != ')') { fail(source_path, source, i, "callable signature must be name(args)"); break; }
            const std::string name = trim_copy(signature.substr(0, lp)); std::vector<std::string> params; std::string variadic_param; const bool params_ok=parse_callable_parameters(signature.substr(lp+1, signature.size()-lp-2),params,variadic_param);
            if (!valid_binding_identifier(name) || !params_ok) { fail(source_path, source, i, "invalid callable signature"); break; }
            std::size_t bo=header_close+1; while(bo<source.size()&&std::isspace(static_cast<unsigned char>(source[bo])))++bo; std::size_t bc=0;
            if(bo>=source.size()||source[bo]!='{'||!find_balanced(source,bo,'{','}',bc)){fail(source_path,source,i,"callable definition requires a block");break;}
            const auto def_body = normalize_control_block_body(source.substr(bo+1,bc-bo-1));
            callables_[name]=Callable{params,variadic_param,def_body.text,source_path,fragment,async,{}}; i=bc+1; continue;
        }

        if (source.compare(i, 4, "@:=(") == 0) {
            std::size_t header_close = 0;
            if (!find_balanced(source, i + 3, '(', ')', header_close)) { fail(source_path, source, i, "@:= has no matching ')'"); break; }
            const std::string name = trim_copy(source.substr(i + 4, header_close - (i + 4)));
            std::size_t block_open = header_close + 1; while (block_open < source.size() && std::isspace(static_cast<unsigned char>(source[block_open]))) ++block_open;
            std::size_t block_close = 0;
            if (block_open >= source.size() || source[block_open] != '{' || !find_balanced(source, block_open, '{', '}', block_close)) { fail(source_path, source, i, "@:= requires a balanced '{...}' expression block"); break; }
            nift::RuntimeValue declared; std::string declaration_error;
            if (!evaluate_expression(name + " := " + source.substr(block_open + 1, block_close - block_open - 1), declared, declaration_error)) { fail(source_path, source, i, "@:= " + declaration_error); break; }
            i = block_close + 1; continue;
        }

        if (source.compare(i, 2, "$[") == 0) {
            std::size_t end = i + 2;
            std::size_t nested_brackets = 0;
            bool value_quoted = false;
            char value_quote = 0;
            for (; end < source.size(); ++end) {
                const char c = source[end];
                if (value_quoted) {
                    if (c == '\\' && end + 1 < source.size()) ++end;
                    else if (c == value_quote) value_quoted = false;
                    continue;
                }
                if (c == '\'' || c == '"') { value_quoted = true; value_quote = c; continue; }
                if (c == '[') {
                    ++nested_brackets;
                } else if (c == ']') {
                    if (nested_brackets == 0) break;
                    --nested_brackets;
                }
            }

            if (end < source.size()) {
                const std::string key = source.substr(i + 2, end - i - 2);

                auto split_ternary = [&](const std::string& expression,
                                         std::string& condition,
                                         std::string& when_true,
                                         std::string& when_false,
                                         bool& malformed) -> bool {
                    malformed = false;
                    bool quoted = false; char quote = 0;
                    int parens = 0, brackets = 0, braces = 0;
                    std::size_t question = std::string::npos;
                    int nested_ternary = 0;
                    for (std::size_t pos = 0; pos < expression.size(); ++pos) {
                        const char c = expression[pos];
                        if (quoted) {
                            if (c == '\\' && pos + 1 < expression.size()) ++pos;
                            else if (c == quote) quoted = false;
                            continue;
                        }
                        if (c == '\'' || c == '"') { quoted = true; quote = c; continue; }
                        if (c == '(') { ++parens; continue; }
                        if (c == ')') { if (parens) --parens; continue; }
                        if (c == '[') { ++brackets; continue; }
                        if (c == ']') { if (brackets) --brackets; continue; }
                        if (c == '{') { ++braces; continue; }
                        if (c == '}') { if (braces) --braces; continue; }
                        if (parens || brackets || braces) continue;
                        if (c == '?') {
                            // '??', '?.' and '?[' are optionality markers, not
                            // ternary; they fall through to evaluate_expression.
                            if (pos + 1 < expression.size() && (expression[pos + 1] == '?' || expression[pos + 1] == '.' || expression[pos + 1] == '[')) continue;
                            if (pos > 0 && expression[pos - 1] == '?') continue;
                            if (question == std::string::npos) question = pos;
                            else ++nested_ternary;
                            continue;
                        }
                        if (c == ':' && question != std::string::npos) {
                            if (nested_ternary > 0) { --nested_ternary; continue; }
                            condition = trim_copy(expression.substr(0, question));
                            when_true = expression.substr(question + 1, pos - question - 1);
                            when_false = expression.substr(pos + 1);
                            return true;
                        }
                    }
                    if (question != std::string::npos) {
                        condition = trim_copy(expression.substr(0, question));
                        when_true = expression.substr(question + 1);
                        when_false.clear();
                        malformed = condition.empty();
                        return !malformed;
                    }
                    return false;
                };

                std::string ternary_condition, when_true, when_false;
                bool malformed_ternary = false;
                if (split_ternary(key, ternary_condition, when_true, when_false, malformed_ternary)) {
                    bool condition_value = false;
                    std::string condition_error;
                    if (!evaluate_condition(ternary_condition, condition_value, condition_error)) {
                        fail(source_path, source, i, condition_error);
                        break;
                    }
                    const std::string& selected = condition_value ? when_true : when_false;

                    // Ternary branches are normally parsed lazily as Nift source so a
                    // selected branch can contain directives such as @input(...). A
                    // branch that is itself a quoted scalar literal is different: the
                    // quotes delimit the value and must not become rendered output.
                    // This keeps constructs such as class="x$[active ? ' active' : '']"
                    // consistent with the scalar-value semantics used elsewhere in
                    // $[...] while preserving lazy source parsing for non-literal
                    // branches.
                    nift::RuntimeValue selected_literal;
                    std::string selected_literal_error;
                    if (scalar_literal(trim_copy(selected), selected_literal, selected_literal_error) &&
                        selected_literal.is_string()) {
                        output += selected_literal.string;
                    } else {
                        const auto nested = parse(selected, source_path, depth + 1);
                        if (!nested.ok) break;
                        output += nested.output;
                    }
                    i = end + 1;
                    continue;
                }
                if (malformed_ternary) {
                    fail(source_path, source, i, "ternary expression has '?' without a matching ':'");
                    break;
                }

                nift::RuntimeValue expression_value;
                std::string expression_error;
                if (evaluate_expression(trim_copy(key), expression_value, expression_error)) {
                    if (last_expression_mutation_) { i = end + 1; continue; }
                    if (expression_value.is_array()) {
                        // Script land: a bare call statement that returns a
                        // non-renderable collection is fire-and-forget.
                        const std::string ck=trim_copy(key);
                        if (standalone_script_host_ && !ck.empty() && ck.back()==')') { i = end + 1; continue; }
                        fail(source_path, source, i, "cannot render JSON array $[" + key + "]; select an element first");
                        break;
                    }
                    if (expression_value.is_object()) {
                        const std::string ck=trim_copy(key);
                        if (standalone_script_host_ && !ck.empty() && ck.back()==')') { i = end + 1; continue; }
                        fail(source_path, source, i, "cannot render JSON object $[" + key + "]; select a member first");
                        break;
                    }
                    if (expression_value.is_bytes()) {
                        fail(source_path, source, i, "bytes values cannot be rendered as text in $[" + key + "]; decode as UTF-8 first");
                        break;
                    }
                    if (function_call_depth_ == 0 &&
                        expression_value.is_string() &&
                        (expression_value.string.rfind("\x1fnift:struct:", 0) == 0 ||
                         expression_value.string.rfind("\x1fnift:callable:", 0) == 0 ||
                         expression_value.string.rfind("\x1fnift:collection:", 0) == 0 ||
                         expression_value.string.rfind("\x1fnift:file:", 0) == 0 ||
                         expression_value.string.rfind("\x1fnift:stream:", 0) == 0)) {
                        fail(source_path, source, i, "cannot render a struct, callable or collection instance $[" + key + "]; select a member or a value-returning call first");
                        break;
                    }
                    output += render_expression_value(expression_value);
                    i = end + 1;
                    continue;
                }
                if (strict_script_mode_ || (!expression_error.empty() && expression_error.rfind("unknown value or malformed expression:", 0) != 0)) {
                    if (expression_error.empty()) expression_error = "unknown value or malformed expression: " + trim_copy(key);
                    fail(source_path, source, i, expression_error);
                    break;
                }
                // A direct legacy lookup below preserves its established diagnostics.
                std::shared_ptr<const nift::RuntimeValue> pagination_value;
                if (resolve_pagination_value(trim_copy(key), pagination_value)) {
                    output += pagination_value->is_string() ? pagination_value->string : pagination_value->dump(0);
                    i = end + 1;
                    continue;
                }

                const std::string metadata_value = metadata(key);
                const bool known_metadata = built_in_metadata_name(key);

                if (known_metadata) {
                    output += metadata_value;
                    i = end + 1;
                    continue;
                }

                std::string json_output;
                std::string json_error;
                if (json_value(key, json_output, json_error)) {
                    if (!json_error.empty()) {
                        fail(source_path, source, i, json_error);
                        break;
                    }
                    output += json_output;
                    i = end + 1;
                    continue;
                }
            }
        }

        if (source.compare(i, 5, "@item") == 0 &&
            (i + 5 == source.size() || source[i + 5] == '{' || std::isspace(static_cast<unsigned char>(source[i + 5])))) {
            if (!pagination_collecting_) { fail(source_path, source, i, "@item requires pagination on the tracked item"); break; }
            std::size_t block_open = i + 5;
            while (block_open < source.size() && std::isspace(static_cast<unsigned char>(source[block_open]))) ++block_open;
            if (block_open >= source.size() || source[block_open] != '{') { fail(source_path, source, i, "@item must be followed by a '{...}' block"); break; }
            std::size_t block_close = 0;
            if (!find_balanced(source, block_open, '{', '}', block_close)) { fail(source_path, source, block_open, "@item block has no matching '}'"); break; }
            const auto body = normalize_control_block_body(source.substr(block_open + 1, block_close - block_open - 1));
            push_json_scope();
            const auto nested = parse(body.text, source_path, depth + 1);
            pop_json_scope();
            if (!nested.ok) break;
            result_.pagination_items.push_back(nested.output);
            i = block_close + 1;
            continue;
        }

        if (source.compare(i, 9, "@paginate") == 0 &&
            (i + 9 == source.size() || !(std::isalnum(static_cast<unsigned char>(source[i + 9])) || source[i + 9] == '_'))) {
            if (!pagination_collecting_) { fail(source_path, source, i, "@paginate requires pagination on the tracked item"); break; }
            if (++result_.paginate_count > 1) { fail(source_path, source, i, "paginated tracked items must execute exactly one @paginate"); break; }
            output += "\x1dNIFT_PAGINATE\x1d";
            i += 9;
            continue;
        }

        if (source.compare(i, 4, "@if(") == 0) {
            std::size_t condition_close = 0;
            if (!find_balanced(source, i + 3, '(', ')', condition_close)) {
                fail(source_path, source, i, "@if has no matching ')' for its condition");
                break;
            }

            std::size_t block_open = condition_close + 1;
            while (block_open < source.size() &&
                   (source[block_open] == ' ' || source[block_open] == '\t' ||
                    source[block_open] == '\r' || source[block_open] == '\n')) {
                ++block_open;
            }
            if (block_open >= source.size() || source[block_open] != '{') {
                fail(source_path, source, i, "@if(...) must be followed by a '{...}' block");
                break;
            }

            std::size_t block_close = 0;
            if (!find_balanced(source, block_open, '{', '}', block_close)) {
                fail(source_path, source, block_open, "@if block has no matching '}'");
                break;
            }

            bool selected = false;
            bool condition_value = false;
            std::string condition_error;
            if (!evaluate_condition(
                    source.substr(i + 4, condition_close - (i + 4)),
                    condition_value,
                    condition_error)) {
                fail(source_path, source, i, condition_error);
                break;
            }

            const std::string control_indent = insertion_indent(output);
            const int insertion_code_block_depth = code_block_depth_;
            std::size_t chain_end = block_close + 1;
            if (condition_value) {
                const auto body = normalize_control_block_body(
                    source.substr(block_open + 1, block_close - block_open - 1));
                push_json_scope();
                const auto nested = parse(body.text, source_path, depth + 1);
                pop_json_scope();
                if (!nested.ok) break;
                append_indented(output, nested.output, control_indent, insertion_code_block_depth);
                selected = true;
            }

            std::size_t cursor = block_close + 1;
            while (cursor < source.size()) {
                const std::size_t whitespace_start = cursor;
                while (cursor < source.size() &&
                       (source[cursor] == ' ' || source[cursor] == '\t' ||
                        source[cursor] == '\r' || source[cursor] == '\n')) {
                    ++cursor;
                }

                if (source.compare(cursor, 4, "else") != 0 ||
                    (cursor + 4 < source.size() &&
                     (std::isalnum(static_cast<unsigned char>(source[cursor + 4])) ||
                      source[cursor + 4] == '_'))) {
                    chain_end = whitespace_start;
                    break;
                }
                cursor += 4;

                while (cursor < source.size() &&
                       (source[cursor] == ' ' || source[cursor] == '\t' ||
                        source[cursor] == '\r' || source[cursor] == '\n')) {
                    ++cursor;
                }

                bool branch_condition = true;
                bool is_else_if = false;
                if (source.compare(cursor, 2, "if") == 0 &&
                    cursor + 2 < source.size() &&
                    (source[cursor + 2] == '(' ||
                     source[cursor + 2] == ' ' || source[cursor + 2] == '\t' ||
                     source[cursor + 2] == '\r' || source[cursor + 2] == '\n')) {
                    is_else_if = true;
                    cursor += 2;
                    while (cursor < source.size() &&
                           (source[cursor] == ' ' || source[cursor] == '\t' ||
                            source[cursor] == '\r' || source[cursor] == '\n')) {
                        ++cursor;
                    }
                    if (cursor >= source.size() || source[cursor] != '(') {
                        fail(source_path, source, cursor, "else if must contain a parenthesised condition");
                        break;
                    }
                    std::size_t else_condition_close = 0;
                    if (!find_balanced(source, cursor, '(', ')', else_condition_close)) {
                        fail(source_path, source, cursor, "else if has no matching ')' for its condition");
                        break;
                    }

                    if (!selected) {
                        std::string else_error;
                        if (!evaluate_condition(
                                source.substr(cursor + 1, else_condition_close - cursor - 1),
                                branch_condition,
                                else_error)) {
                            fail(source_path, source, cursor, else_error);
                            break;
                        }
                    }
                    cursor = else_condition_close + 1;
                }

                while (cursor < source.size() &&
                       (source[cursor] == ' ' || source[cursor] == '\t' ||
                        source[cursor] == '\r' || source[cursor] == '\n')) {
                    ++cursor;
                }

                if (cursor >= source.size() || source[cursor] != '{') {
                    fail(source_path, source, cursor, "else/else if must be followed by a '{...}' block");
                    break;
                }

                std::size_t else_block_close = 0;
                if (!find_balanced(source, cursor, '{', '}', else_block_close)) {
                    fail(source_path, source, cursor, "else/else if block has no matching '}'");
                    break;
                }

                if (!selected && branch_condition) {
                    const auto body = normalize_control_block_body(
                        source.substr(cursor + 1, else_block_close - cursor - 1));
                    push_json_scope();
                    ++loop_depth_;
                    const auto nested = parse(body.text, source_path, depth + 1);
                    --loop_depth_;
                    pop_json_scope();
                    if (!nested.ok) break;
                    append_indented(output, nested.output, control_indent, insertion_code_block_depth);
                    // A break/continue/return raised inside an else branch must
                    // propagate to the enclosing loop/function; do not consume
                    // it here or the control-flow signal is silently lost.
                    if (pending_control_.kind != ControlFlow::None) break;
                    selected = true;
                }

                cursor = else_block_close + 1;
                chain_end = cursor;

                if (!is_else_if) {
                    std::size_t after_else = cursor;
                    while (after_else < source.size() &&
                           (source[after_else] == ' ' || source[after_else] == '\t' ||
                            source[after_else] == '\r' || source[after_else] == '\n')) {
                        ++after_else;
                    }
                    if (source.compare(after_else, 4, "else") == 0) {
                        fail(source_path, source, after_else,
                             "plain else must be the final branch of an @if chain");
                    }
                    chain_end = cursor;
                    break;
                }
            }

            if (!result_.ok) break;
            i = chain_end;
            continue;
        }

        // Shared prepared-loop infrastructure (CP16 + runtime completion):
        // prepare a supported loop body once into statement trees, then execute
        // those trees against live bindings each iteration. Unsupported syntax
        // falls back to the compatibility evaluator, which remains the oracle.
            // Split a simple binding.method(args) call into root/method/arg trees
            // once at preparation time so hot loops do not re-parse the call text.
            auto split_native_call=[&](const std::string& text,std::string& root,std::string& method,std::vector<std::unique_ptr<nift::ast::Expr>>& args)->bool{
                const std::size_t lp=text.find('(');
                if(lp==std::string::npos||text.empty()||text.back()!=')')return false;
                std::string target=trim_copy(text.substr(0,lp));
                const std::size_t dot=target.rfind('.');
                if(dot==std::string::npos)return false;
                root=trim_copy(target.substr(0,dot));method=trim_copy(target.substr(dot+1));
                if(!valid_binding_identifier(root)||!valid_binding_identifier(method))return false;
                const std::string body=text.substr(lp+1,text.size()-lp-2);
                std::vector<std::string> arg_texts;std::string cur;int dep=0;bool q=false;char qc=0;
                for(std::size_t k=0;k<body.size();++k){char ch=body[k];if(q){if(ch=='\\'&&k+1<body.size())++k;else if(ch==qc)q=false;cur+=ch;continue;}if(ch=='\''||ch=='"'){q=true;qc=ch;cur+=ch;continue;}if(ch=='('||ch=='[')++dep;else if(ch==')'||ch==']')--dep;if(ch==','&&dep==0){arg_texts.push_back(cur);cur.clear();continue;}cur+=ch;}
                arg_texts.push_back(cur);
                for(const auto& at:arg_texts){std::string a=trim_copy(at);if(a.empty())continue;auto r=nift::ast::parse_expression(a);if(!r.supported)return false;args.push_back(std::move(r.expr));}
                return true;};
        std::function<bool(const std::string&,std::vector<std::unique_ptr<nift::ast::Stmt>>&)> prepare_loop_body;
        prepare_loop_body=[&](const std::string& text,std::vector<std::unique_ptr<nift::ast::Stmt>>& out_stmts)->bool{
                        out_stmts.clear();
            for(std::size_t bp=0;bp<text.size();){
                while(bp<text.size()&&std::isspace((unsigned char)text[bp]))++bp;
                if(bp>=text.size())return true;
                if(text.compare(bp,2,"$[")==0){std::size_t close=0;if(!find_balanced(text,bp+1,'[',']',close))return false;auto ps=nift::ast::parse_statement(text.substr(bp+2,close-bp-2));if(!ps.supported)return false;const auto k=ps.stmt->kind;if(k==nift::ast::StmtKind::Declaration)ps.stmt->decl_type=expression_type(ps.stmt->text.substr(ps.stmt->text.find(":=")+2));if(k==nift::ast::StmtKind::Assignment||k==nift::ast::StmtKind::CompoundAssignment||k==nift::ast::StmtKind::Increment||k==nift::ast::StmtKind::Declaration){out_stmts.push_back(std::move(ps.stmt));bp=close+1;continue;}if(k==nift::ast::StmtKind::Expression&&ps.stmt->expr&&ps.stmt->expr->kind==nift::ast::Kind::Call){std::string nroot,nmethod;std::vector<std::unique_ptr<nift::ast::Expr>> nargs;if(split_native_call(ps.stmt->expr->text,nroot,nmethod,nargs)){ps.stmt->name=nroot;ps.stmt->op=nmethod;ps.stmt->call_args=std::move(nargs);out_stmts.push_back(std::move(ps.stmt));bp=close+1;continue;}return false;}return false;}
                if(text.compare(bp,4,"@if(")==0){std::size_t hc=0;if(!find_balanced(text,bp+3,'(',')',hc))return false;std::size_t bo=hc+1;while(bo<text.size()&&std::isspace((unsigned char)text[bo]))++bo;std::size_t bc=0;if(bo>=text.size()||text[bo]!='{'||!find_balanced(text,bo,'{','}',bc))return false;auto cond=nift::ast::parse_expression(text.substr(bp+4,hc-(bp+4)));if(!cond.supported)return false;auto st=std::make_unique<nift::ast::Stmt>();st->kind=nift::ast::StmtKind::If;st->condition=std::move(cond.expr);if(!prepare_loop_body(text.substr(bo+1,bc-bo-1),st->body))return false;out_stmts.push_back(std::move(st));bp=bc+1;continue;}
                if(text.compare(bp,7,"@while(")==0){std::size_t hc=0;if(!find_balanced(text,bp+6,'(',')',hc))return false;std::size_t bo=hc+1;while(bo<text.size()&&std::isspace((unsigned char)text[bo]))++bo;std::size_t bc=0;if(bo>=text.size()||text[bo]!='{'||!find_balanced(text,bo,'{','}',bc))return false;auto cond=nift::ast::parse_expression(text.substr(bp+7,hc-(bp+7)));if(!cond.supported)return false;auto st=std::make_unique<nift::ast::Stmt>();st->kind=nift::ast::StmtKind::While;st->condition=std::move(cond.expr);if(!prepare_loop_body(text.substr(bo+1,bc-bo-1),st->body))return false;out_stmts.push_back(std::move(st));bp=bc+1;continue;}
                if(text.compare(bp,5,"@for(")==0){std::size_t hc=0;if(!find_balanced(text,bp+4,'(',')',hc))return false;std::size_t bo=hc+1;while(bo<text.size()&&std::isspace((unsigned char)text[bo]))++bo;std::size_t bc=0;if(bo>=text.size()||text[bo]!='{'||!find_balanced(text,bo,'{','}',bc))return false;std::string header=trim_copy(text.substr(bp+5,hc-(bp+5)));auto sep=header.find(':');if(sep==std::string::npos)return false;auto st=std::make_unique<nift::ast::Stmt>();st->kind=nift::ast::StmtKind::For;st->name=trim_copy(header.substr(0,sep));auto it=nift::ast::parse_expression(trim_copy(header.substr(sep+1)));if(!it.supported||!valid_binding_identifier(st->name))return false;st->iterable=std::move(it.expr);if(!prepare_loop_body(text.substr(bo+1,bc-bo-1),st->body))return false;out_stmts.push_back(std::move(st));bp=bc+1;continue;}
                if(text.compare(bp,5,"break")==0&&(bp+5>=text.size()||std::isspace((unsigned char)text[bp+5]))){auto st=std::make_unique<nift::ast::Stmt>();st->kind=nift::ast::StmtKind::Break;out_stmts.push_back(std::move(st));bp+=5;continue;}
                if(text.compare(bp,8,"continue")==0&&(bp+8>=text.size()||std::isspace((unsigned char)text[bp+8]))){auto st=std::make_unique<nift::ast::Stmt>();st->kind=nift::ast::StmtKind::Continue;out_stmts.push_back(std::move(st));bp+=8;continue;}
                if(text.compare(bp,8,"@return(")==0){std::size_t hc=0;if(!find_balanced(text,bp+7,'(',')',hc))return false;auto st=std::make_unique<nift::ast::Stmt>();st->kind=nift::ast::StmtKind::Return;std::string ex=trim_copy(text.substr(bp+8,hc-(bp+8)));if(!ex.empty()){auto r=nift::ast::parse_expression(ex);if(!r.supported)return false;st->expr=std::move(r.expr);}out_stmts.push_back(std::move(st));bp=hc+1;continue;}
                return false;
            }
            return true;};
        std::function<bool(const std::vector<std::unique_ptr<nift::ast::Stmt>>&,std::string&)> execute_body;
        std::function<bool(const nift::ast::Stmt&,std::string&)> execute_prepared;
        auto ast_context=[&](){nift::ast::Context c;c.resolve=[&](const std::string& name,nift::RuntimeValue& out,std::string& e){for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(name);if(it!=sc->end()){it->second.sync();if(!it->second.value){e="reference target no longer exists: "+name;return false;}out=*it->second.value;if(out.is_string()&&out.string.rfind("\x1fnift:atomic:",0)==0){auto ai=atomic_instances_.find(out.string.substr(13));if(ai==atomic_instances_.end()){e="atomic: invalid handle";return false;}if(ai->second->kind==AtomicInstance::Kind::Bool)out=nift::RuntimeValue(ai->second->bool_value.load());else{auto v=ai->second->int_value.load();out=nift::RuntimeValue((double)v);if(v>9007199254740992LL||v<-9007199254740992LL){out.type=nift::RuntimeType::StrNumber;out.string=std::to_string(v);}}}return true;}}e="unknown value or malformed expression: "+name;return false;};c.legacy=[&](const std::string& x,nift::RuntimeValue& out,std::string& e){return evaluate_expression(x,out,e);};c.arg_is_location=[&](const std::string& arg)->bool{VariableBinding loc;const std::string ref_source=trim_copy(arg);if(valid_binding_identifier(ref_source)){for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(ref_source);if(it!=sc->end()){it->second.sync();if(it->second.value&&it->second.value->is_string()&&it->second.value->string.rfind("\x1fnift:atomic:",0)==0)return true;break;}}}auto parsed=nift::ast::parse_expression(ref_source);if(!parsed.supported||!parsed.expr)return false;std::shared_ptr<std::shared_ptr<nift::RuntimeValue>> root;std::vector<PathComponent> path;std::function<bool(const nift::ast::Expr&)> walk;walk=[&](const nift::ast::Expr& ex)->bool{if(ex.kind==nift::ast::Kind::Binding){VariableBinding* b=nullptr;for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(ex.name);if(it!=sc->end()){it->second.sync();b=&it->second;break;}}if(!b||!b->value)return false;if(b->is_location_ref()){root=b->ref_root_slot;path=b->ref_path;}else root=b->slot;return true;}if(ex.kind==nift::ast::Kind::Member){if(!ex.left||!walk(*ex.left))return false;path.push_back(PathComponent::member(ex.name));return true;}if(ex.kind==nift::ast::Kind::Index){if(!ex.left||!walk(*ex.left))return false;nift::RuntimeValue idx;std::string index_error;if(!evaluate_expression(ex.right->text.empty()?ref_source.substr(ex.right->span.begin,ex.right->span.end-ex.right->span.begin):ex.right->text,idx,index_error))return false;if(idx.is_number()&&idx.num>=0&&std::trunc(idx.num)==idx.num)path.push_back(PathComponent::at((std::size_t)idx.num));else if(idx.is_string())path.push_back(PathComponent::member(idx.string));else return false;return true;}return false;};if(!walk(*parsed.expr)||path.empty())return false;loc.ref_root_slot=std::move(root);loc.ref_path=std::move(path);loc.sync();if(!loc.value)return false;return loc.value->is_array()||loc.value->is_object()||(loc.value->is_string()&&loc.value->string.rfind("\x1fnift:",0)==0);};c.call=[&](const std::string& name,std::vector<nift::RuntimeValue>&& args,nift::RuntimeValue& out,std::string& e)->bool{
            // Prepared dispatch for user callables: bind args, run the prepared
            // body once, propagate the return value. Any failure falls back to
            // the legacy evaluator (the oracle) via the Call handler.
            if(name=="ffi_buffer"){
                if(args.size()!=1){e="ffi_buffer: expected string, bytes, or byte array";return true;}auto st=std::make_shared<FfiBufferInstance>();const auto& d=args[0];if(d.is_string())st->bytes.assign(d.string.begin(),d.string.end());else if(d.is_bytes())st->bytes.assign(d.bytes->begin(),d.bytes->end());else if(d.is_array()){for(const auto& x:d.array){std::size_t byte=0;if(!nift::runtime_number_to_size(x,byte)||byte>255){e="ffi_buffer: array values must be bytes";return true;}st->bytes.push_back(static_cast<unsigned char>(byte));}}else{e="ffi_buffer: expected string, bytes, or byte array";return true;}const std::string id=std::to_string(next_ffi_buffer_id_++);ffi_buffers_[id]=st;out=nift::RuntimeValue(std::string("\x1fnift:ffi-buffer:")+id);return true;
            }
            if(name=="ffi_snapshot_bytes"){
                if(args.size()!=1||!args[0].is_string()||args[0].string.rfind("\x1fnift:ffi-buffer:",0)!=0){e="ffi_snapshot_bytes: expected buffer handle";return true;}auto bi=ffi_buffers_.find(args[0].string.substr(17));if(bi==ffi_buffers_.end()){e="ffi_snapshot_bytes: invalid buffer handle";return true;}out=nift::RuntimeValue(nift::RuntimeBytes(bi->second->bytes.begin(),bi->second->bytes.end()));return true;
            }
            const Callable* callee=nullptr;
            for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto bi=sc->find(name);if(bi!=sc->end()){bi->second.sync();if(bi->second.value&&bi->second.value->is_string()&&bi->second.value->string.rfind("\x1fnift:callable:",0)==0)return false;break;}}
            std::shared_ptr<ModuleEnv> callee_env;
            if(active_module_env_){auto mit=active_module_env_->callables.find(name);if(mit!=active_module_env_->callables.end()){callee=&mit->second;callee_env=active_module_env_;}}
            if(!callee){auto ci=callables_.find(name);if(ci!=callables_.end())callee=&ci->second;}
            if(!callee&&host_.has_host_callable(name))return call_runtime_host(host_, name, args, out, e);
            if(!callee||callee->fragment)return false;
            if(callee->async)return false; // legacy evaluator schedules async call and returns a future
            if(callee->variadic_param.empty()?args.size()!=callee->params.size():args.size()<callee->params.size())return false;
            auto& prep=prepared_callables_[callee];
            if(!prep.ready){
                std::string program,perr;
                if(!translate_function_program(callee->body,program,perr))return false;
                if(!prepare_loop_body(program,prep.stmts))return false;
                prep.ready=true;
            }
            if(callable_call_depth_>=kMaxCallableDepth){e="callable recursion depth exceeded: "+name;return false;}
            auto lexical_env=enter_lexical_environment(callee_env ? callee_env : callee->module_env);
            push_variable_scope();auto& scope=variable_scopes_.back();
            for(std::size_t ai=0;ai<callee->params.size()&&ai<args.size();++ai){auto sp=std::make_shared<nift::RuntimeValue>(std::move(args[ai]));scope.emplace(callee->params[ai],VariableBinding{sp,nift_binding_type(*sp),true,false});}
            if(!callee->variadic_param.empty()){nift::RuntimeValue rest=nift::RuntimeValue::make_array();for(std::size_t ai=callee->params.size();ai<args.size();++ai)rest.array.push_back(std::move(args[ai]));auto sp=std::make_shared<nift::RuntimeValue>(std::move(rest));scope.emplace(callee->variadic_param,VariableBinding{sp,nift_binding_type(*sp),true,false});}
            ++callable_call_depth_;const int saved_loop_depth=loop_depth_;loop_depth_=0;pending_control_={};
            std::string pe;const bool ok=execute_body(prep.stmts,pe);
            loop_depth_=saved_loop_depth;--callable_call_depth_;
            pop_variable_scope();
            leave_lexical_environment(std::move(lexical_env));
            if(!ok){e=pe;return false;}
            if(pending_control_.kind==ControlFlow::Return)consume_return(out);
            else out=nift::RuntimeValue(nullptr);
            return true;};c.resolve_ref=[&](const std::string& name,std::shared_ptr<const nift::RuntimeValue>& out,std::string& e){for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(name);if(it!=sc->end()){it->second.sync();if(!it->second.value){e="reference target no longer exists: "+name;return false;}out=it->second.value;return true;}}e="unknown value or malformed expression: "+name;return false;};c.native_method=[&](const nift::RuntimeValue& recv,const std::string& method,std::vector<nift::RuntimeValue>&& args,nift::RuntimeValue& out,std::string& e)->bool{
            if(recv.is_string()&&recv.string.rfind("\x1fnift:atomic:",0)==0){
                auto ai=atomic_instances_.find(recv.string.substr(13));if(ai==atomic_instances_.end()){e="atomic: invalid handle";return false;}auto st=ai->second;
                auto int_doc=[](std::int64_t v){nift::RuntimeValue d(static_cast<double>(v));if(v>9007199254740992LL||v<-9007199254740992LL){d.type=nift::RuntimeType::StrNumber;d.string=std::to_string(v);}return d;};
                auto as_int=[&](const nift::RuntimeValue& v,std::int64_t& x){if(nift::runtime_number_to_i64(v,x))return true;e="atomic<int>: expected signed 64-bit integer";return false;};
                if(method=="load"){if(!args.empty()){e="load: expected no arguments";return false;}if(st->kind==AtomicInstance::Kind::Int)out=int_doc(st->int_value.load());else out=nift::RuntimeValue(st->bool_value.load());return true;}
                if(method=="store"||method=="exchange"){if(args.size()!=1){e=method+": expected one value";return false;}if(st->kind==AtomicInstance::Kind::Int){std::int64_t x=0;if(!as_int(args[0],x))return false;if(method=="store"){st->int_value.store(x);out=nift::RuntimeValue(nullptr);}else out=int_doc(st->int_value.exchange(x));}else{if(!args[0].is_bool()){e=method+": atomic<bool> requires bool";return false;}if(method=="store"){st->bool_value.store(args[0].boolean);out=nift::RuntimeValue(nullptr);}else out=nift::RuntimeValue(st->bool_value.exchange(args[0].boolean));}return true;}
                if(method=="fetch_add"||method=="fetch_sub"){if(st->kind!=AtomicInstance::Kind::Int){e=method+": only valid for atomic<int>";return false;}if(args.size()!=1){e=method+": expected one integer";return false;}std::int64_t x=0;if(!as_int(args[0],x))return false;std::int64_t before=0,after=0;if(!nift_atomic_add_sub_checked(st->int_value,x,method=="fetch_sub",before,after)){e=method+": integer overflow";return false;}out=int_doc(before);return true;}
                if(method=="compare_exchange"){if(args.size()!=2){e="compare_exchange: expected expected and desired values";return false;}if(st->kind==AtomicInstance::Kind::Int){std::int64_t expected=0,desired=0;if(!as_int(args[0],expected)||!as_int(args[1],desired))return false;out=nift::RuntimeValue(st->int_value.compare_exchange_strong(expected,desired));}else{if(!args[0].is_bool()||!args[1].is_bool()){e="compare_exchange: atomic<bool> requires bool values";return false;}bool expected=args[0].boolean;out=nift::RuntimeValue(st->bool_value.compare_exchange_strong(expected,args[1].boolean));}return true;}
            }
            if(method=="to_string"&&recv.is_number()){out=nift::RuntimeValue(render_expression_value(recv));return true;}
            if(method=="to_string"&&recv.is_string()){out=recv;return true;}
            if(method=="encode"&&recv.is_string()){if(args.size()!=1||!args[0].is_string()||args[0].string!="utf-8"){e="encode: encoding must be exactly 'utf-8'";return false;}if(!nift::runtime_valid_utf8(recv.string)){e="encode: invalid UTF-8 text";return false;}out=nift::RuntimeValue(nift::RuntimeBytes(recv.string.begin(),recv.string.end()));return true;}
            if(recv.is_bytes()){
                const nift::RuntimeBytes empty;const auto& data=recv.bytes?*recv.bytes:empty;
                if(method=="length"||method=="size"){if(!args.empty()){e=method+": expected no arguments";return false;}out=nift::RuntimeValue(static_cast<double>(data.size()));return true;}
                if(method=="empty"){if(!args.empty()){e="empty: expected no arguments";return false;}out=nift::RuntimeValue(data.empty());return true;}
                if(method=="slice"){if(args.empty()||args.size()>2){e="slice: expected start and optional end";return false;}std::size_t begin=0,end=data.size();if(!nift::runtime_number_to_size(args[0],begin)){e="slice: invalid start";return false;}if(args.size()==2&&!nift::runtime_number_to_size(args[1],end)){e="slice: invalid end";return false;}begin=std::min(begin,data.size());end=std::min(end,data.size());if(end<begin)end=begin;out=nift::RuntimeValue(nift::RuntimeBytes(data.begin()+begin,data.begin()+end));return true;}
                if(method=="decode"){if(args.size()!=1||!args[0].is_string()||args[0].string!="utf-8"){e="decode: encoding must be exactly 'utf-8'";return false;}std::string decoded=runtime_bytes_string(recv);if(!nift::runtime_valid_utf8(decoded)){e="decode: invalid UTF-8";return false;}out=nift::RuntimeValue(std::move(decoded));return true;}
            }
            if(method=="to_int"&&recv.is_number()){if(!args.empty()){e="to_int: expected no arguments";return false;}std::int64_t value=0;if(!nift::runtime_number_to_i64(recv,value)){e="to_int: invalid or out-of-range integer";return false;}out=nift::runtime_integer(value);return true;}
            if(method=="to_double"&&recv.is_number()){out=recv;return true;}
            if(method=="abs"&&recv.is_number()){out=nift::RuntimeValue(std::fabs(recv.num));return true;}
            if(method=="floor"&&recv.is_number()){out=nift::RuntimeValue(std::floor(recv.num));return true;}
            if(method=="ceil"&&recv.is_number()){out=nift::RuntimeValue(std::ceil(recv.num));return true;}
            if(method=="round"&&recv.is_number()){out=nift::RuntimeValue(std::round(recv.num));return true;}
            if(method=="length"&&recv.is_string()){out=nift::RuntimeValue((double)recv.string.size());return true;}
            if(method=="trim"&&recv.is_string()){std::string s=recv.string;auto b=s.find_first_not_of(" \t\r\n");if(b==std::string::npos)s.clear();else{s=s.substr(b);auto epos=s.find_last_not_of(" \t\r\n");s=s.substr(0,epos+1);}out=nift::RuntimeValue(s);return true;}
            if(method=="to_lower"&&recv.is_string()){std::string s=recv.string;for(auto&ch:s)ch=(char)std::tolower((unsigned char)ch);out=nift::RuntimeValue(s);return true;}
            if(method=="to_upper"&&recv.is_string()){std::string s=recv.string;for(auto&ch:s)ch=(char)std::toupper((unsigned char)ch);out=nift::RuntimeValue(s);return true;}
            if(method=="size"){if(recv.is_array()){out=nift::RuntimeValue((double)recv.array.size());return true;}if(recv.is_object()){out=nift::RuntimeValue((double)recv.object.size());return true;}}
            if(method=="length"){if(recv.is_array()){out=nift::RuntimeValue((double)recv.array.size());return true;}if(recv.is_string()){out=nift::RuntimeValue((double)recv.string.size());return true;}if(recv.is_object()){out=nift::RuntimeValue((double)recv.object.size());return true;}}
            if(method=="empty"){if(recv.is_array()){out=nift::RuntimeValue(recv.array.empty());return true;}if(recv.is_string()){out=nift::RuntimeValue(recv.string.empty());return true;}if(recv.is_object()){out=nift::RuntimeValue(recv.object.empty());return true;}}
            if(method=="first"&&recv.is_array()){if(recv.array.empty()){e="first: array is empty";return false;}out=recv.array.front();return true;}
            if(method=="last"&&recv.is_array()){if(recv.array.empty()){e="last: array is empty";return false;}out=recv.array.back();return true;}
            return false;};c.render=[&](const nift::RuntimeValue& v){return render_expression_value(v);};return c;};
        auto find_binding=[&](const std::string& name)->VariableBinding*{for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(name);if(it!=sc->end())return &it->second;}return nullptr;};
        // Build a persistent logical location from an AST Binding/Index/Member
        // chain.  Indices/keys are evaluated once when the reference is formed;
        // subsequent reads/writes re-resolve that path from the live root slot.
        std::function<bool(const nift::ast::Expr&,std::shared_ptr<std::shared_ptr<nift::RuntimeValue>>&,std::vector<PathComponent>&,std::string&)> build_location_ref;
        build_location_ref=[&](const nift::ast::Expr& ex,std::shared_ptr<std::shared_ptr<nift::RuntimeValue>>& root,std::vector<PathComponent>& path,std::string& e)->bool{
            if(ex.kind==nift::ast::Kind::Binding){
                auto* b=find_binding(ex.name);if(!b)return false;b->sync();if(!b->value){e="reference target no longer exists: "+ex.name;return false;}
                if(b->is_location_ref()){root=b->ref_root_slot;path=b->ref_path;}else root=b->slot;
                return true;
            }
            if(ex.kind==nift::ast::Kind::Member){if(!ex.left||!build_location_ref(*ex.left,root,path,e))return false;path.push_back(PathComponent::member(ex.name));return true;}
            if(ex.kind==nift::ast::Kind::Index){
                if(!ex.left||!build_location_ref(*ex.left,root,path,e))return false;
                nift::RuntimeValue idx;auto cc=ast_context();if(!nift::ast::evaluate(*ex.right,cc,idx,e))return false;
                std::size_t index=0;if(nift::runtime_number_to_size(idx,index))path.push_back(PathComponent::at(index));
                else if(idx.is_string())path.push_back(PathComponent::member(idx.string));
                else{e="invalid reference index";return false;}return true;
            }
            return false;
        };
        auto bind_location=[&](VariableBinding& dst,const nift::ast::Expr& ex,std::string& e)->bool{
            std::shared_ptr<std::shared_ptr<nift::RuntimeValue>> root;std::vector<PathComponent> path;if(!build_location_ref(ex,root,path,e)||path.empty())return false;
            dst.ref_root_slot=std::move(root);dst.ref_path=std::move(path);dst.sync();if(!dst.value){e="reference target no longer exists";return false;}dst.type=nift_binding_type(*dst.value);return dst.value->is_array()||dst.value->is_object()||(dst.value->is_string()&&dst.value->string.rfind("\x1fnift:",0)==0);
        };
        auto assign_plain=[&](const std::string& name,nift::RuntimeValue v,std::string& e)->bool{for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(name);if(it==sc->end())continue;if(!it->second.mutable_binding){e="cannot assign to const binding: "+name;return false;}it->second.sync();if(it->second.value&&it->second.value->is_string()&&it->second.value->string.rfind("\x1fnift:atomic:",0)==0&&!(v.is_string()&&v.string.rfind("\x1fnift:atomic:",0)==0)){auto ai=atomic_instances_.find(it->second.value->string.substr(13));if(ai==atomic_instances_.end()){e="atomic: invalid handle";return false;}if(ai->second->kind==AtomicInstance::Kind::Int){std::int64_t x=0;if(!nift::runtime_number_to_i64(v,x)){e="assignment to atomic<int> requires signed 64-bit integer";return false;}ai->second->int_value.store(x);}else{if(!v.is_bool()){e="assignment to atomic<bool> requires bool";return false;}ai->second->bool_value.store(v.boolean);}last_expression_mutation_=true;return true;}const int at=nift_binding_type(v);if(!nift_type_assignable(at,it->second.type)){e="cannot change binding type: "+name;return false;}it->second.rebind(std::make_shared<nift::RuntimeValue>(std::move(v)));return true;}e="assignment to undefined binding: "+name;return false;};
            // Resolve an Index/Member chain to a non-const Document reference so
            // that assignment mutates the aliased collection element in place
            // (json-mutate's e["total"] = e.v * 2 must write back into arr).
            std::function<bool(const nift::ast::Expr&,nift::RuntimeValue*&,std::string&)> resolve_target_ref;
            resolve_target_ref=[&](const nift::ast::Expr& t,nift::RuntimeValue*& out,std::string& e)->bool{
                if(t.kind==nift::ast::Kind::Binding){for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(t.name);if(it!=sc->end()){it->second.sync();if(!it->second.mutable_binding){e="cannot assign to const binding: "+t.name;return false;}if(!it->second.value){e="reference target no longer exists: "+t.name;return false;}out=it->second.is_location_ref()?it->second.resolve_location():it->second.value.get();if(!out){e="reference target no longer exists: "+t.name;return false;}return true;}}e="assignment to undefined binding: "+t.name;return false;}
                if(t.kind==nift::ast::Kind::Index){nift::RuntimeValue* b=nullptr;if(!resolve_target_ref(*t.left,b,e))return false;nift::RuntimeValue i;auto c=ast_context();if(!nift::ast::evaluate(*t.right,c,i,e))return false;std::size_t index=0;if(b->is_array()&&nift::runtime_number_to_size(i,index)&&index<b->array.size()){out=&b->array[index];return true;}if(b->is_object()&&i.is_string()){out=&(*b)[i.string];return true;}e="invalid index target";return false;}
                if(t.kind==nift::ast::Kind::Member){nift::RuntimeValue* b=nullptr;if(!resolve_target_ref(*t.left,b,e))return false;if(!b->is_object()){e="value has no member: "+t.name;return false;}out=&(*b)[t.name];return true;}
                e="unsupported assignment target";return false;};
            auto assign_indexed=[&](const nift::ast::Expr& target,nift::RuntimeValue rhs,std::string& e)->bool{nift::RuntimeValue* slot=nullptr;if(!resolve_target_ref(target,slot,e))return false;*slot=std::move(rhs);last_expression_mutation_=true;return true;};
        auto large_num=[&](const nift::RuntimeValue& d)->bool{return d.is_number()&&(d.type==nift::RuntimeType::StrNumber||d.num>=9007199254740992.0||d.num<=-9007199254740992.0);};
            // Execute a prepared native collection call (push/size/pop/empty/etc.)
            // against the live receiver binding. Falls back to the legacy evaluator
            // when the receiver is not a plain array or the method is unsupported.
            auto execute_native_call=[&](const nift::ast::Stmt& st,std::string& e)->bool{
                auto c=ast_context();std::vector<nift::RuntimeValue> av;
                VariableBinding* rb=nullptr;
                for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(st.name);if(it!=sc->end()){rb=&it->second;break;}}
                for(const auto& a:st.call_args){nift::RuntimeValue v;if(!nift::ast::evaluate(*a,c,v,e))return false;av.push_back(std::move(v));}
                if(rb)rb->sync();
                if(!rb||!rb->value){nift::RuntimeValue lege;return c.legacy(st.text,lege,e);}
                if(rb->value->is_string()&&rb->value->string.rfind("\x1fnift:file:",0)==0){
                    auto fid=rb->value->string.substr(11);
                    auto fi=file_instances_.find(fid);
                    if(fi==file_instances_.end()){e="invalid file handle";return false;}
                    auto& f=fi->second;
                    if(st.op=="write"||st.op=="write_line"){
                        if(av.size()!=1||!f->open||!(f->mode=="w"||f->mode=="a"||f->mode=="rw")){if(av.size()!=1&&e.empty())e=st.op+": expected one value";return false;}
                        nift::RuntimeValue v=std::move(av[0]);
                        std::string d;if(st.op=="write"&&v.is_bytes()){const nift::RuntimeBytes empty;const auto& raw=v.bytes?*v.bytes:empty;d.assign(raw.begin(),raw.end());}else{if(v.is_bytes()||v.is_array()||v.is_object()||(v.is_string()&&v.string.rfind("\x1fnift:",0)==0)){e=st.op+": value is not directly renderable";return false;}d=render_expression_value(v);if(st.op=="write_line")d+='\n';}
                        const std::size_t base=std::min(f->cursor,f->working.size());
                        const std::size_t ov=std::min(d.size(),f->working.size()-base);
                        f->working.replace(base,ov,d);
                        f->cursor=base+d.size();
                        f->dirty=f->working!=f->saved;
                        return true;
                    }
                    {nift::RuntimeValue lege;return c.legacy(st.text,lege,e);}
                }
                if(rb->value->is_string()&&rb->value->string.rfind("\x1fnift:collection:",0)==0){
                    auto cid=rb->value->string.substr(17);
                    auto ci=collection_instances_.find(cid);
                    if(ci==collection_instances_.end()){e="invalid collection handle";return false;}
                    auto& cc=ci->second;
                    const bool is_map=cc->kind==CollectionKind::Map||cc->kind==CollectionKind::SortedMap;
                    auto ckey=[&](const nift::RuntimeValue& x)->std::string{return runtime_scalar_key(x);};
                    if(!is_map&&(st.op=="add"||st.op=="contains"||st.op=="size"||st.op=="empty")){
                        if(st.op=="size"){if(!av.empty()){e="size: expected no arguments";return false;}return true;}
                        if(st.op=="empty"){if(!av.empty()){e="empty: expected no arguments";return false;}return true;}
                        if(st.op=="add"||st.op=="contains"){
                            if(av.size()!=1){e=st.op+": expected one value";return false;}
                            const nift::RuntimeValue& v=av[0];
                            const std::string k=ckey(v);
                            const bool indexed=!k.empty();
                            if(!indexed){nift::RuntimeValue lege;return c.legacy(st.text,lege,e);}
                            const bool present=cc->scalar_keys.count(k)!=0&&std::any_of(cc->values.begin(),cc->values.end(),[&](const auto& item){return ckey(item)==k&&nift::runtime_equal(item,v);});
                            if(st.op=="contains"){return true;}
                            if(!rb->mutable_binding){e="cannot mutate const collection: "+st.name;return false;}
                            if(st.op=="add"&&!present){cc->values.push_back(v);cc->scalar_keys.insert(k);}else if(st.op=="add"&&present){/* already present: no-op */}last_expression_mutation_=true;return true;
                        }
                    }
                    {nift::RuntimeValue lege;return c.legacy(st.text,lege,e);}
                }
                if(!rb->value->is_array()){nift::RuntimeValue lege;return c.legacy(st.text,lege,e);}
                auto& arr=rb->value->array;
                if(st.op=="push"){if(av.size()!=1){e="push: expected one argument";return false;}if(!rb->mutable_binding){e="cannot mutate const array: "+st.name;return false;}arr.push_back(std::move(av[0]));last_expression_mutation_=true;return true;}
                if(st.op=="size"||st.op=="length"){if(!av.empty()){e=st.op+": expected no arguments";return false;}last_expression_mutation_=false;return true;}
                if(st.op=="empty"){if(!av.empty()){e="empty: expected no arguments";return false;}last_expression_mutation_=false;return true;}
                if(st.op=="first"||st.op=="last"||st.op=="pop"){if(!av.empty()){e=st.op+": expected no arguments";return false;}if(arr.empty()){e=st.op+": array is empty";return false;}if(st.op=="pop"){if(!rb->mutable_binding){e="cannot mutate const array: "+st.name;return false;}arr.pop_back();last_expression_mutation_=true;}else last_expression_mutation_=false;return true;}
                if(st.op=="clear"){if(!av.empty()){e="clear: expected no arguments";return false;}if(!rb->mutable_binding){e="cannot mutate const array: "+st.name;return false;}arr.clear();last_expression_mutation_=true;return true;}
                {nift::RuntimeValue lege;return c.legacy(st.text,lege,e);}};
        execute_body=[&](const std::vector<std::unique_ptr<nift::ast::Stmt>>& body,std::string& e)->bool{for(const auto& s:body){if(pending_control_.kind!=ControlFlow::None)return true;if(!execute_prepared(*s,e))return false;}return true;};
        execute_prepared=[&](const nift::ast::Stmt& st,std::string& e)->bool{auto c=ast_context();nift::RuntimeValue rhs,oldv,next;if(st.kind==nift::ast::StmtKind::If){nift::RuntimeValue cv;if(!st.condition){e="unsupported prepared if condition";return false;}if(!nift::ast::evaluate(*st.condition,c,cv,e))return false;if(!nift::ast::truthy(cv))return true;return execute_body(st.body,e);}if(st.kind==nift::ast::StmtKind::While){nift::RuntimeValue cv;while(true){if(pending_control_.kind!=ControlFlow::None)return true;if(!st.condition){e="unsupported prepared while condition";return false;}if(!nift::ast::evaluate(*st.condition,c,cv,e))return false;if(!nift::ast::truthy(cv))return true;push_json_scope();++loop_depth_;const bool wok=execute_body(st.body,e);--loop_depth_;pop_json_scope();if(!wok)return false;if(pending_control_.kind==ControlFlow::Break){pending_control_={};return true;}if(pending_control_.kind==ControlFlow::Continue){pending_control_={};continue;}}return true;}if(st.kind==nift::ast::StmtKind::For){std::shared_ptr<const nift::RuntimeValue> col;nift::RuntimeValue colv;if(st.iterable&&st.iterable->kind==nift::ast::Kind::Binding){if(!c.resolve_ref(st.iterable->name,col,e))return false;}else if(st.iterable){if(!nift::ast::evaluate(*st.iterable,c,colv,e))return false;col=std::make_shared<const nift::RuntimeValue>(std::move(colv));}else{e="unsupported prepared for iterable";return false;}if(!col->is_array()){e="for iteration requires an array";return false;}for(std::size_t k=0;k<col->array.size();++k){if(pending_control_.kind!=ControlFlow::None)break;push_json_scope();VariableBinding lb{std::make_shared<nift::RuntimeValue>(col->array[k]),nift_binding_type(col->array[k]),true,false};
            // Loop elements are location references rooted at the iterated
            // aggregate binding, so in-body mutation of the parent (push/pop/
            // replace) cannot dangle the element (no interior pointer retained).
            if(st.iterable&&st.iterable->kind==nift::ast::Kind::Binding){auto* src=find_binding(st.iterable->name);if(src){src->sync();if(src->value&&(src->value->is_array()||src->value->is_object())){lb.ref_root_slot=src->slot;std::vector<PathComponent> pc;pc.push_back(PathComponent::at(k));lb.ref_path=std::move(pc);}}}
            variable_scopes_.back().emplace(st.name,std::move(lb));++loop_depth_;const bool bok=execute_body(st.body,e);--loop_depth_;pop_json_scope();if(!bok)return false;if(pending_control_.kind==ControlFlow::Break){pending_control_={};break;}if(pending_control_.kind==ControlFlow::Continue){pending_control_={};continue;}}return true;}if(st.kind==nift::ast::StmtKind::Break){pending_control_.kind=ControlFlow::Break;pending_control_.value.reset();return true;}if(st.kind==nift::ast::StmtKind::Continue){pending_control_.kind=ControlFlow::Continue;pending_control_.value.reset();return true;}if(st.kind==nift::ast::StmtKind::Return){nift::RuntimeValue rv;pending_control_.ref_root_slot.reset();pending_control_.ref_path.clear();if(st.expr){if(!nift::ast::evaluate(*st.expr,c,rv,e))return false;VariableBinding rl;std::string locerr;if(bind_location(rl,*st.expr,locerr)&&rl.is_location_ref()&&rl.value){pending_control_.ref_root_slot=rl.ref_root_slot;pending_control_.ref_path=rl.ref_path;}else if(last_call_return_loc_root_&&(rv.is_array()||rv.is_object()||(rv.is_string()&&rv.string.rfind("\x1fnift:",0)==0))){pending_control_.ref_root_slot=last_call_return_loc_root_;pending_control_.ref_path=last_call_return_loc_path_;}pending_control_.value=std::make_shared<nift::RuntimeValue>(std::move(rv));}else pending_control_.value.reset();pending_control_.kind=ControlFlow::Return;return true;}if(st.kind==nift::ast::StmtKind::Expression){return execute_native_call(st,e);}if(st.kind==nift::ast::StmtKind::Declaration){last_call_return_loc_root_.reset();last_call_return_loc_path_.clear();if(!nift::ast::evaluate(*st.expr,c,rhs,e))return false;if(large_num(rhs)){nift::RuntimeValue lege;return c.legacy(st.text,lege,e);}if(variable_scopes_.empty()){e="no scope for declaration: "+st.name;return false;}auto& scope=variable_scopes_.back();if(scope.count(st.name)){e="binding already declared in this scope: "+st.name;return false;}if(last_call_return_loc_root_&&(rhs.is_array()||rhs.is_object()||(rhs.is_string()&&rhs.string.rfind("\x1fnift:",0)==0))){VariableBinding dloc;dloc.ref_root_slot=last_call_return_loc_root_;dloc.ref_path=last_call_return_loc_path_;dloc.sync();if(!dloc.value){e="reference target no longer exists";return false;}dloc.type=nift_binding_type(*dloc.value);dloc.mutable_binding=true;last_call_return_loc_root_.reset();last_call_return_loc_path_.clear();scope.emplace(st.name,std::move(dloc));return true;}auto sp=std::make_shared<nift::RuntimeValue>(std::move(rhs));const int dt=st.decl_type;const int bt=(dt==3)?3:((dt==2)?nift_binding_type(*sp):((dt>=0&&dt!=2&&dt!=3)?dt:nift_binding_type(*sp)));auto ins=scope.emplace(st.name,VariableBinding{sp,bt,true,false});if(st.expr&&(st.expr->kind==nift::ast::Kind::Index||st.expr->kind==nift::ast::Kind::Member)){std::string re;if(!bind_location(ins.first->second,*st.expr,re)&&!re.empty()){e=re;scope.erase(ins.first);return false;}}else if(st.expr&&st.expr->kind==nift::ast::Kind::Binding&&(sp->is_array()||sp->is_object())){auto* src=find_binding(st.expr->name);if(src){src->sync();if(src->value){ins.first->second.value=src->value;*ins.first->second.slot=src->value;}}}return true;}if(st.kind==nift::ast::StmtKind::Assignment){if(!nift::ast::evaluate(*st.expr,c,rhs,e))return false;if(large_num(rhs)){nift::RuntimeValue lege;return c.legacy(st.text,lege,e);}if(st.target)return assign_indexed(*st.target,std::move(rhs),e);return assign_plain(st.name,std::move(rhs),e);}VariableBinding* cb=nullptr;for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(st.name);if(it!=sc->end()){cb=&it->second;break;}}if(!cb){e="assignment to undefined binding: "+st.name;return false;}cb->sync();if(!cb->mutable_binding){e="cannot assign to const binding: "+st.name;return false;}if(cb->value&&cb->value->is_string()&&cb->value->string.rfind("\x1fnift:atomic:",0)==0){nift::RuntimeValue lege;return c.legacy(st.text,lege,e);}if(st.kind==nift::ast::StmtKind::Increment){if(!cb->value->is_number()){e="increment/decrement requires a numeric lvalue";return false;}oldv=*cb->value;if(large_num(oldv)){nift::RuntimeValue lege;return c.legacy(st.text,lege,e);}next=nift::RuntimeValue(oldv.num+(st.op=="++"?1.0:-1.0));return assign_plain(st.name,std::move(next),e);}if(!nift::ast::evaluate(*st.expr,c,rhs,e))return false;if(st.op=="+="&&cb->value->is_string()&&rhs.is_string()){if(cb->value.use_count()==2){cb->value->string.append(rhs.string);last_expression_mutation_=true;return true;}oldv=*cb->value;next=nift::RuntimeValue(oldv.string+rhs.string);}else if(st.op=="+="&&cb->value->is_array()&&rhs.is_array()){oldv=*cb->value;next=nift::RuntimeValue::make_array();next.array.reserve(oldv.array.size()+rhs.array.size());next.array.insert(next.array.end(),oldv.array.begin(),oldv.array.end());next.array.insert(next.array.end(),rhs.array.begin(),rhs.array.end());}else{oldv=*cb->value;if(!oldv.is_number()||!rhs.is_number()){e="arithmetic operators require numeric operands";return false;}if(large_num(oldv)||large_num(rhs)){nift::RuntimeValue lege;return c.legacy(st.text,lege,e);}if(st.op=="+=")next=nift::RuntimeValue(oldv.num+rhs.num);else if(st.op=="-=")next=nift::RuntimeValue(oldv.num-rhs.num);else if(st.op=="*=")next=nift::RuntimeValue(oldv.num*rhs.num);else if(st.op=="/="){if(rhs.num==0){e="division by zero";return false;}next=nift::RuntimeValue(oldv.num/rhs.num);}else{if(rhs.num==0){e="modulo by zero";return false;}next=nift::RuntimeValue(std::fmod(oldv.num,rhs.num));}}return assign_plain(st.name,std::move(next),e);};
        if (source.compare(i, 7, "@while(") == 0) {
            std::size_t hc=0;if(!find_balanced(source,i+6,'(',')',hc)){fail(source_path,source,i,"@while has no matching ')' for its condition");break;}
            std::size_t bo=hc+1;while(bo<source.size()&&std::isspace((unsigned char)source[bo]))++bo;std::size_t bc=0;
            if(bo>=source.size()||source[bo]!='{'||!find_balanced(source,bo,'{','}',bc)){fail(source_path,source,i,"@while(...) must be followed by a '{...}' block");break;}
            const std::string condition=source.substr(i+7,hc-(i+7));
            const auto body=normalize_control_block_body(source.substr(bo+1,bc-bo-1));
            auto prepared_condition=nift::ast::parse_expression(condition);
            std::vector<std::unique_ptr<nift::ast::Stmt>> prepared_body; const bool prepared_body_ok=prepare_loop_body(body.text,prepared_body);

            while(result_.ok){bool yes=false;std::string e;if(prepared_condition.supported){auto c=ast_context();nift::RuntimeValue cv;if(!nift::ast::evaluate(*prepared_condition.expr,c,cv,e)){fail(source_path,source,i,e);break;}yes=nift::ast::truthy(cv);last_expression_mutation_=false;}else if(!evaluate_condition(condition,yes,e)){fail(source_path,source,i,e);break;}if(!yes)break;
                push_json_scope(); ++loop_depth_; RenderResult nested;if(prepared_body_ok){if(!execute_body(prepared_body,e)){nested.ok=false;nested.error.message=e;}}else nested=parse(body.text,source_path,depth+1); --loop_depth_; pop_json_scope();if(!nested.ok){fail(source_path,source,i,nested.error.message);break;}append_indented(output,nested.output,"",code_block_depth_);
                if (pending_control_.kind == ControlFlow::Continue) { pending_control_ = {}; continue; }
                if (pending_control_.kind == ControlFlow::Break) { pending_control_ = {}; break; }
                if(pending_control_.kind!=ControlFlow::None)break;
            }
            if(!result_.ok)break;
            i=bc+1;continue;
        }

        if (source.compare(i, 5, "@for(") == 0) {
            std::size_t header_close = 0;
            if (!find_balanced(source, i + 4, '(', ')', header_close)) {
                fail(source_path, source, i, "@for has no matching ')' for its header");
                break;
            }

            std::size_t block_open = header_close + 1;
            while (block_open < source.size() &&
                   (source[block_open] == ' ' || source[block_open] == '\t' ||
                    source[block_open] == '\r' || source[block_open] == '\n')) {
                ++block_open;
            }
            if (block_open >= source.size() || source[block_open] != '{') {
                fail(source_path, source, i, "@for(...) must be followed by a '{...}' block");
                break;
            }

            std::size_t block_close = 0;
            if (!find_balanced(source, block_open, '{', '}', block_close)) {
                fail(source_path, source, block_open, "@for block has no matching '}'");
                break;
            }

            const std::string header = trim_copy(
                source.substr(i + 5, header_close - (i + 5)));

            bool quoted = false;
            char quote = 0;
            std::size_t paren_depth = 0;
            std::size_t bracket_depth = 0;
            std::size_t separator_position = std::string::npos;
            for (std::size_t h = 0; h < header.size(); ++h) {
                const char c = header[h];
                if (quoted) {
                    if (c == '\\') ++h;
                    else if (c == quote) quoted = false;
                    continue;
                }
                if (c == '\'' || c == '"') { quoted = true; quote = c; continue; }
                if (c == '(') { ++paren_depth; continue; }
                if (c == ')') { if (paren_depth) --paren_depth; continue; }
                if (c == '[') { ++bracket_depth; continue; }
                if (c == ']') { if (bracket_depth) --bracket_depth; continue; }

                if (paren_depth == 0 && bracket_depth == 0 && c == ':') {
                    separator_position = h;
                    break;
                }
            }

            if (separator_position == std::string::npos) {
                fail(source_path, source, i, "@for header must contain ':'");
                break;
            }

            const std::string binding_part = trim_copy(header.substr(0, separator_position));
            const std::string collection_clause = trim_copy(header.substr(separator_position + 1));
            std::string collection_expression;
            std::string sort_expression;
            bool sort_descending = false;
            std::string sort_clause_error;
            if (!parse_for_collection_clause(collection_clause, collection_expression,
                                             sort_expression, sort_descending, sort_clause_error)) {
                fail(source_path, source, i, sort_clause_error);
                break;
            }

            std::shared_ptr<const nift::RuntimeValue> collection;
            if (sort_expression.empty() && valid_binding_identifier(collection_expression)) {
                for (auto scope = variable_scopes_.rbegin(); scope != variable_scopes_.rend(); ++scope) {
                    auto found = scope->find(collection_expression);
                    if (found != scope->end()) { collection = found->second.value; break; }
                }
            }
            if (!collection) {
                nift::RuntimeValue collection_value; std::string collection_error;
                if (!evaluate_collection_value(collection_expression, collection_value, collection_error)) { fail(source_path, source, i, "@for collection: " + collection_error); break; }
                collection = std::make_shared<const nift::RuntimeValue>(std::move(collection_value));
            }

            // Materialize internal v4.3 sequential/set collections for the
            // existing @for engine; maps/sorted_maps are iterated directly so
            // typed keys are preserved (no JSON-object stringification).
            if(collection->is_string()&&collection->string.rfind("\x1fnift:collection:",0)==0){
                auto ci=collection_instances_.find(collection->string.substr(17));
                if(ci==collection_instances_.end()){fail(source_path,source,i,"@for collection: invalid collection");break;}
                if(ci->second->kind==CollectionKind::Map||ci->second->kind==CollectionKind::SortedMap){
                    // keep the collection reference; the dedicated map branch below iterates entries.
                }else{
                    nift::RuntimeValue materialized=nift::RuntimeValue::make_array();materialized.array=ci->second->values;if(ci->second->kind==CollectionKind::Stack)std::reverse(materialized.array.begin(),materialized.array.end());
                    collection=std::make_shared<const nift::RuntimeValue>(std::move(materialized));
                }
            }

            const auto body = normalize_control_block_body(
                source.substr(block_open + 1, block_close - block_open - 1));
            std::vector<std::unique_ptr<nift::ast::Stmt>> for_prepared_body;
            const bool for_prepared_ok=prepare_loop_body(body.text,for_prepared_body);
            const std::string control_indent = insertion_indent(output);
            const int insertion_code_block_depth = code_block_depth_;

            // Direct typed-entry iteration for v4.3 maps/sorted_maps: bind the
            // original typed key and value, preserving the map's key domain and
            // ordering (insertion for map, sorted for sorted_map) without an
            // intermediate JSON-object representation.
            if (collection->is_string() && collection->string.rfind("\x1fnift:collection:",0)==0) {
                auto mci=collection_instances_.find(collection->string.substr(17));
                if(mci==collection_instances_.end()||(mci->second->kind!=CollectionKind::Map&&mci->second->kind!=CollectionKind::SortedMap)){fail(source_path,source,i,"@for collection: invalid map");break;}
                const auto& entries=mci->second->entries;
                if(binding_part.size()<5||binding_part.front()!='('||binding_part.back()!=')'){
                    fail(source_path,source,i,"map @for syntax is @for((key, val) : map){...}");break;
                }
                const std::string pair=binding_part.substr(1,binding_part.size()-2);
                const auto comma=pair.find(',');
                if(comma==std::string::npos||pair.find(',',comma+1)!=std::string::npos){fail(source_path,source,i,"map @for requires exactly two bindings: (key, val)");break;}
                const std::string key_name=trim_copy(pair.substr(0,comma));
                const std::string value_name=trim_copy(pair.substr(comma+1));
                if(!valid_binding_identifier(key_name)||!valid_binding_identifier(value_name)||key_name==value_name){fail(source_path,source,i,"map @for key and value bindings must be distinct identifiers");break;}
                if(reserved_binding_name(key_name)||reserved_binding_name(value_name)){fail(source_path,source,i,"@for bindings cannot conflict with built-in metadata");break;}
                if(host_.is_contract_name(key_name)||host_.is_contract_name(value_name)){fail(source_path,source,i,"@for bindings cannot conflict with configured contract namespaces");break;}
                const bool valid_map_sort_root=sort_expression.empty()||sort_expression==key_name||sort_expression==value_name||
                    sort_expression.rfind(key_name+".",0)==0||sort_expression.rfind(key_name+"[",0)==0||
                    sort_expression.rfind(value_name+".",0)==0||sort_expression.rfind(value_name+"[",0)==0;
                if(!valid_map_sort_root){fail(source_path,source,i,"@for map sort key must begin with key/value binding '"+key_name+"' or '"+value_name+"'");break;}

                const auto old_key_it=json_bindings_.find(key_name);
                const auto old_value_it=json_bindings_.find(value_name);
                const bool had_old_key=old_key_it!=json_bindings_.end();
                const bool had_old_value=old_value_it!=json_bindings_.end();
                std::shared_ptr<const nift::RuntimeValue> old_key,old_value;
                if(had_old_key)old_key=old_key_it->second;
                if(had_old_value)old_value=old_value_it->second;
                const auto previous_loop=json_bindings_.find("loop");
                const bool had_previous_loop=previous_loop!=json_bindings_.end();
                std::shared_ptr<const nift::RuntimeValue> previous_loop_value;
                if(had_previous_loop)previous_loop_value=previous_loop->second;

                std::vector<std::size_t> order(entries.size());
                for(std::size_t n=0;n<order.size();++n)order[n]=n;
                std::vector<nift::RuntimeValue> sort_keys;
                if(!sort_expression.empty()){
                    sort_keys.reserve(entries.size());
                    nift::RuntimeType key_type=nift::RuntimeType::Null;bool have_key_type=false;
                    for(std::size_t n=0;n<entries.size();++n){
                        json_bindings_[key_name]=std::make_shared<const nift::RuntimeValue>(entries[n].first);
                        json_bindings_[value_name]=std::make_shared<const nift::RuntimeValue>(entries[n].second);
                        std::shared_ptr<const nift::RuntimeValue> key;std::string key_error;
                        if(!resolve_json_value(sort_expression,key,key_error)||!key_error.empty()){fail(source_path,source,i,key_error.empty()?"@for sort key is not a bound JSON path: "+sort_expression:key_error);break;}
                        if(!sortable_scalar(*key)){fail(source_path,source,i,"@for sort keys must all be numbers or all be strings: "+sort_expression);break;}
                        if(!have_key_type){key_type=key->type;have_key_type=true;}
                        else if(key->type!=key_type){fail(source_path,source,i,"@for sort keys must have the same type: "+sort_expression);break;}
                        sort_keys.push_back(*key);
                    }
                    if(!result_.ok){if(had_old_key)json_bindings_[key_name]=old_key;else json_bindings_.erase(key_name);if(had_old_value)json_bindings_[value_name]=old_value;else json_bindings_.erase(value_name);break;}
                    std::stable_sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b){const int comparison=compare_sort_keys(sort_keys[a],sort_keys[b]);return sort_descending?comparison>0:comparison<0;});
                }

                for(std::size_t position=0;position<order.size()&&result_.ok;++position){
                    const auto& entry=entries[order[position]];
                    json_bindings_[key_name]=std::make_shared<const nift::RuntimeValue>(entry.first);
                    json_bindings_[value_name]=std::make_shared<const nift::RuntimeValue>(entry.second);
                    json_bindings_["loop"]=make_loop_metadata(position,order.size());
                    push_json_scope();
                    variable_scopes_.back().emplace(key_name,VariableBinding{std::make_shared<nift::RuntimeValue>(entry.first),nift_binding_type(entry.first),true,false});
                    variable_scopes_.back().emplace(value_name,VariableBinding{std::make_shared<nift::RuntimeValue>(entry.second),nift_binding_type(entry.second),true,false});
                    ++loop_depth_;
                    const auto nested=parse(body.text,source_path,depth+1);
                    --loop_depth_;
                    pop_json_scope();
                    if(!nested.ok)break;
                    append_indented(output,nested.output,control_indent,insertion_code_block_depth);
                    if(pending_control_.kind==ControlFlow::Continue){pending_control_={};continue;}
                    if(pending_control_.kind==ControlFlow::Break){pending_control_={};break;}
                    if(body.multiline&&position+1<order.size())output+="\n"+control_indent;
                }
                if(had_old_key)json_bindings_[key_name]=std::move(old_key);else json_bindings_.erase(key_name);
                if(had_old_value)json_bindings_[value_name]=std::move(old_value);else json_bindings_.erase(value_name);
                if(had_previous_loop)json_bindings_["loop"]=std::move(previous_loop_value);else json_bindings_.erase("loop");
                if(!result_.ok)break;
                i=block_close+1;
                continue;
            }

            if (collection->is_array()) {
                if (!valid_binding_identifier(binding_part)) {
                    fail(source_path, source, i, "array @for syntax is @for(item : array){...}");
                    break;
                }
                if (reserved_binding_name(binding_part)) {
                    fail(source_path, source, i,
                         "@for binding '" + binding_part + "' conflicts with built-in metadata");
                    break;
                }
                if (host_.is_contract_name(binding_part)) {
                    fail(source_path, source, i,
                         "@for binding '" + binding_part + "' conflicts with configured contract namespace");
                    break;
                }

                if (!sort_expression.empty() &&
                    !(sort_expression == binding_part ||
                      sort_expression.rfind(binding_part + ".", 0) == 0 ||
                      sort_expression.rfind(binding_part + "[", 0) == 0)) {
                    fail(source_path, source, i,
                         "@for array sort key must begin with loop binding '" + binding_part + "'");
                    break;
                }

                const auto previous = json_bindings_.find(binding_part);
                const bool had_previous = previous != json_bindings_.end();
                std::shared_ptr<const nift::RuntimeValue> previous_value;
                if (had_previous) previous_value = previous->second;
                const auto previous_loop = json_bindings_.find("loop");
                const bool had_previous_loop = previous_loop != json_bindings_.end();
                std::shared_ptr<const nift::RuntimeValue> previous_loop_value;
                if (had_previous_loop) previous_loop_value = previous_loop->second;

                std::vector<std::size_t> order(collection->array.size());
                for (std::size_t n = 0; n < order.size(); ++n) order[n] = n;
                std::vector<nift::RuntimeValue> sort_keys;
                if (!sort_expression.empty()) {
                    sort_keys.reserve(collection->array.size());
                    nift::RuntimeType key_type = nift::RuntimeType::Null;
                    bool have_key_type = false;
                    for (std::size_t n = 0; n < collection->array.size(); ++n) {
                        const nift::RuntimeValue* element = &collection->array[n];
                        json_bindings_[binding_part] =
                            std::shared_ptr<const nift::RuntimeValue>(collection, element);
                        std::shared_ptr<const nift::RuntimeValue> key;
                        std::string key_error;
                        if (!resolve_json_value(sort_expression, key, key_error) || !key_error.empty()) {
                            fail(source_path, source, i, key_error.empty()
                                ? "@for sort key is not a bound JSON path: " + sort_expression
                                : key_error);
                            break;
                        }
                        if (!sortable_scalar(*key)) {
                            fail(source_path, source, i,
                                 "@for sort keys must all be numbers or all be strings: " + sort_expression);
                            break;
                        }
                        if (!have_key_type) { key_type = key->type; have_key_type = true; }
                        else if (key->type != key_type) {
                            fail(source_path, source, i,
                                 "@for sort keys must have the same type: " + sort_expression);
                            break;
                        }
                        sort_keys.push_back(*key);
                    }
                    if (!result_.ok) {
                        if (had_previous) json_bindings_[binding_part] = previous_value;
                        else json_bindings_.erase(binding_part);
                        break;
                    }
                    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                        const int comparison = compare_sort_keys(sort_keys[a], sort_keys[b]);
                        return sort_descending ? comparison > 0 : comparison < 0;
                    });
                }

                for (std::size_t position = 0; position < order.size() && result_.ok; ++position) {
                    const std::size_t index = order[position];
                    const nift::RuntimeValue* element = &collection->array[index];
                    json_bindings_[binding_part] = std::make_shared<const nift::RuntimeValue>(*element);
                    json_bindings_["loop"] = make_loop_metadata(position, order.size());

                    push_json_scope();
                    VariableBinding lb{std::make_shared<nift::RuntimeValue>(*element),
                        nift_binding_type(*element), true, false};
                    // @for element is a location reference rooted at the iterated
                    // aggregate binding so in-body parent mutation cannot dangle it.
                    if(valid_binding_identifier(collection_expression)){VariableBinding* srcb=nullptr;for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(collection_expression);if(it!=sc->end()){srcb=&it->second;break;}}if(srcb){srcb->sync();if(srcb->value&&(srcb->value->is_array()||srcb->value->is_object())){lb.ref_root_slot=srcb->slot;std::vector<PathComponent> pc;pc.push_back(PathComponent::at(index));lb.ref_path=std::move(pc);}}}
                    variable_scopes_.back().emplace(binding_part, std::move(lb));
                    ++loop_depth_;
                    RenderResult nested;
                    if(for_prepared_ok){
                        std::string pe;
                        if(!execute_body(for_prepared_body,pe)){nested.ok=false;nested.error.message=pe;}
                    }else nested=parse(body.text, source_path, depth + 1);
                    --loop_depth_;
                    pop_json_scope();
                    if (!nested.ok) break;
                    append_indented(output, nested.output, control_indent, insertion_code_block_depth);
                    if (pending_control_.kind == ControlFlow::Continue) { pending_control_ = {}; continue; }
                    if (pending_control_.kind == ControlFlow::Break) { pending_control_ = {}; break; }
                    if (body.multiline && position + 1 < order.size())
                        output += "\n" + control_indent;
                }

                if (had_previous) json_bindings_[binding_part] = std::move(previous_value);
                else json_bindings_.erase(binding_part);
                if (had_previous_loop) json_bindings_["loop"] = std::move(previous_loop_value);
                else json_bindings_.erase("loop");

                if (!result_.ok) break;
            } else if (collection->is_object()) {
                if (binding_part.size() < 5 ||
                    binding_part.front() != '(' || binding_part.back() != ')') {
                    fail(source_path, source, i,
                         "object @for syntax is @for((key, val) : object){...}");
                    break;
                }

                const std::string pair = binding_part.substr(1, binding_part.size() - 2);
                const auto comma = pair.find(',');
                if (comma == std::string::npos || pair.find(',', comma + 1) != std::string::npos) {
                    fail(source_path, source, i,
                         "object @for requires exactly two bindings: (key, val)");
                    break;
                }

                const std::string key_name = trim_copy(pair.substr(0, comma));
                const std::string value_name = trim_copy(pair.substr(comma + 1));
                if (!valid_binding_identifier(key_name) || !valid_binding_identifier(value_name) ||
                    key_name == value_name) {
                    fail(source_path, source, i,
                         "object @for key and value bindings must be distinct identifiers");
                    break;
                }
                if (reserved_binding_name(key_name) || reserved_binding_name(value_name)) {
                    fail(source_path, source, i,
                         "@for bindings cannot conflict with built-in metadata");
                    break;
                }
                if (host_.is_contract_name(key_name) || host_.is_contract_name(value_name)) {
                    fail(source_path, source, i,
                         "@for bindings cannot conflict with configured contract namespaces");
                    break;
                }

                const bool valid_object_sort_root = sort_expression.empty() ||
                    sort_expression == key_name || sort_expression == value_name ||
                    sort_expression.rfind(key_name + ".", 0) == 0 ||
                    sort_expression.rfind(key_name + "[", 0) == 0 ||
                    sort_expression.rfind(value_name + ".", 0) == 0 ||
                    sort_expression.rfind(value_name + "[", 0) == 0;
                if (!valid_object_sort_root) {
                    fail(source_path, source, i,
                         "@for object sort key must begin with key/value binding '" +
                         key_name + "' or '" + value_name + "'");
                    break;
                }

                const auto old_key_it = json_bindings_.find(key_name);
                const auto old_value_it = json_bindings_.find(value_name);
                const bool had_old_key = old_key_it != json_bindings_.end();
                const bool had_old_value = old_value_it != json_bindings_.end();
                std::shared_ptr<const nift::RuntimeValue> old_key;
                std::shared_ptr<const nift::RuntimeValue> old_value;
                if (had_old_key) old_key = old_key_it->second;
                if (had_old_value) old_value = old_value_it->second;
                const auto previous_loop = json_bindings_.find("loop");
                const bool had_previous_loop = previous_loop != json_bindings_.end();
                std::shared_ptr<const nift::RuntimeValue> previous_loop_value;
                if (had_previous_loop) previous_loop_value = previous_loop->second;

                std::vector<std::size_t> order(collection->object.size());
                for (std::size_t n = 0; n < order.size(); ++n) order[n] = n;
                std::vector<nift::RuntimeValue> sort_keys;
                if (!sort_expression.empty()) {
                    sort_keys.reserve(collection->object.size());
                    nift::RuntimeType key_type = nift::RuntimeType::Null;
                    bool have_key_type = false;
                    for (std::size_t n = 0; n < collection->object.size(); ++n) {
                        const auto& entry = collection->object[n];
                        json_bindings_[key_name] = std::make_shared<const nift::RuntimeValue>(entry.first);
                        json_bindings_[value_name] = std::make_shared<const nift::RuntimeValue>(entry.second);
                        std::shared_ptr<const nift::RuntimeValue> key;
                        std::string key_error;
                        if (!resolve_json_value(sort_expression, key, key_error) || !key_error.empty()) {
                            fail(source_path, source, i, key_error.empty()
                                ? "@for sort key is not a bound JSON path: " + sort_expression
                                : key_error);
                            break;
                        }
                        if (!sortable_scalar(*key)) {
                            fail(source_path, source, i,
                                 "@for sort keys must all be numbers or all be strings: " + sort_expression);
                            break;
                        }
                        if (!have_key_type) { key_type = key->type; have_key_type = true; }
                        else if (key->type != key_type) {
                            fail(source_path, source, i,
                                 "@for sort keys must have the same type: " + sort_expression);
                            break;
                        }
                        sort_keys.push_back(*key);
                    }
                    if (!result_.ok) {
                        if (had_old_key) json_bindings_[key_name] = old_key; else json_bindings_.erase(key_name);
                        if (had_old_value) json_bindings_[value_name] = old_value; else json_bindings_.erase(value_name);
                        break;
                    }
                    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                        const int comparison = compare_sort_keys(sort_keys[a], sort_keys[b]);
                        return sort_descending ? comparison > 0 : comparison < 0;
                    });
                }

                for (std::size_t position = 0; position < order.size() && result_.ok; ++position) {
                    const auto& entry = collection->object[order[position]];
                    json_bindings_[key_name] = std::make_shared<const nift::RuntimeValue>(entry.first);
                    json_bindings_[value_name] = std::make_shared<const nift::RuntimeValue>(entry.second);
                    json_bindings_["loop"] = make_loop_metadata(position, order.size());

                    push_json_scope();
                    variable_scopes_.back().emplace(key_name, VariableBinding{
                        std::make_shared<nift::RuntimeValue>(entry.first),
                        nift_binding_type(nift::RuntimeValue(entry.first)), true, false});
                    VariableBinding vb{std::make_shared<nift::RuntimeValue>(entry.second),
                        nift_binding_type(entry.second), true, false};
                    // Map @for value is a location reference (key path) so
                    // in-body map mutation cannot dangle the retained value.
                    if(valid_binding_identifier(collection_expression)){VariableBinding* srcb=nullptr;for(auto sc=variable_scopes_.rbegin();sc!=variable_scopes_.rend();++sc){auto it=sc->find(collection_expression);if(it!=sc->end()){srcb=&it->second;break;}}if(srcb){srcb->sync();if(srcb->value){vb.ref_root_slot=srcb->slot;std::vector<PathComponent> pc;pc.push_back(PathComponent::member(entry.first));vb.ref_path=std::move(pc);}}}
                    variable_scopes_.back().emplace(value_name, std::move(vb));
                    ++loop_depth_;
                    const auto nested = parse(body.text, source_path, depth + 1);
                    --loop_depth_;
                    pop_json_scope();
                    if (!nested.ok) break;
                    append_indented(output, nested.output, control_indent, insertion_code_block_depth);
                    if (pending_control_.kind == ControlFlow::Continue) { pending_control_ = {}; continue; }
                    if (pending_control_.kind == ControlFlow::Break) { pending_control_ = {}; break; }
                    if (body.multiline && position + 1 < order.size())
                        output += "\n" + control_indent;
                }

                if (had_old_key) json_bindings_[key_name] = std::move(old_key);
                else json_bindings_.erase(key_name);
                if (had_old_value) json_bindings_[value_name] = std::move(old_value);
                else json_bindings_.erase(value_name);
                if (had_previous_loop) json_bindings_["loop"] = std::move(previous_loop_value);
                else json_bindings_.erase("loop");

                if (!result_.ok) break;
            } else {
                fail(source_path, source, i, "@for can only iterate over JSON arrays or objects");
                break;
            }

            i = block_close + 1;
            continue;
        }

        if (source[i] == '@' && i + 1 < source.size() && source[i + 1] >= 'a' && source[i + 1] <= 'z') {
            std::size_t name_end = i + 1;
            while (name_end < source.size() && source[name_end] >= 'a' && source[name_end] <= 'z') ++name_end;
            const std::string function = source.substr(i + 1, name_end - i - 1);
            std::size_t call_start = name_end;
            std::vector<std::string> parameters;
            std::vector<bool> parameter_quoted;
            bool has_parameters = false;
            bool parameters_ok = true;
            std::size_t end = call_start;

            if (call_start < source.size() && source[call_start] == '(') {
                has_parameters = true;
                std::size_t close = 0;
                if (!find_balanced(source, call_start, '(', ')', close)) { fail(source_path, source, i, function + ": malformed parameters"); break; }
                parameters = parse_parameters(source.substr(call_start + 1, close - call_start - 1), parameters_ok,
                                              function == "json" ? &parameter_quoted : nullptr);
                if (!parameters_ok) { fail(source_path, source, i, function + ": malformed parameters"); break; }
                end = close + 1;
                if (end < source.size() && source[end] == ';') ++end;
            }

            // Inside @fn bodies, @return(expr) captures the function result and
            // stops the current parse level; enclosing constructs unwind when
            // the pending return is set. Outside function bodies it remains a
            // passthrough so existing literal "@return(...)" text is preserved.
            if (function == "return" && function_call_depth_ > 0 && !in_fragment_body_) {
                if (!has_parameters) {
                    fail(source_path, source, i, "return requires an argument expression");
                    break;
                }
                std::size_t return_close = 0;
                if (!find_balanced(source, call_start, '(', ')', return_close)) {
                    fail(source_path, source, i, "return: malformed expression");
                    break;
                }
                const std::string return_expr = trim_copy(source.substr(call_start + 1, return_close - call_start - 1));
                nift::RuntimeValue return_value(nullptr);
                if (!return_expr.empty()) {
                    std::string return_error;
                    if (!evaluate_expression(return_expr, return_value, return_error)) {
                        fail(source_path, source, i, return_error.rfind("callable recursion depth exceeded",0)==0 ? return_error : "return: " + return_error);
                        break;
                    }
                }
                pending_control_.ref_root_slot.reset(); pending_control_.ref_path.clear();
                if (!return_expr.empty()) {
                    auto rp = nift::ast::parse_expression(return_expr);
                    if (rp.supported && rp.expr) {
                        VariableBinding rl; std::string locerr;
                        if (bind_location(rl, *rp.expr, locerr) && rl.is_location_ref() && rl.value) {
                            pending_control_.ref_root_slot = rl.ref_root_slot;
                            pending_control_.ref_path = rl.ref_path;
                        }
                    }
                    if(!pending_control_.ref_root_slot&&last_call_return_loc_root_&&
                       (return_value.is_array()||return_value.is_object()||(return_value.is_string()&&return_value.string.rfind("\x1fnift:",0)==0))){
                        pending_control_.ref_root_slot=last_call_return_loc_root_;
                        pending_control_.ref_path=last_call_return_loc_path_;
                    }
                }
                pending_control_.value = std::make_shared<nift::RuntimeValue>(std::move(return_value));
                pending_control_.kind = ControlFlow::Return;
                break;
            }

            // Parameterised functions are only calls when followed by (...).
            // This keeps prose such as "Partials & @input" literal while preserving
            // strict validation for actual calls such as @input().
            if (!has_parameters && function != "content") {
                if (name_end < source.size() && source[name_end] == '[') {
                    fail(source_path, source, i, function + ": expected parentheses for parameters");
                    break;
                }
                output += source.substr(i, name_end - i);
                i = name_end;
                continue;
            }

            const auto previous_newline = output.find_last_of('\n');
            const std::string current_line = previous_newline == std::string::npos ? output : output.substr(previous_newline + 1);
            std::string indent = current_line;
            for (char c : current_line) if (c != ' ' && c != '\t') { indent.assign(current_line.size(), ' '); break; }

            if (function == "content") {
                if (has_parameters && !parameters.empty()) { fail(source_path, source, i, "content: expected 0 parameters"); break; }
                if (!page_source_.has_value()) {
                    fail(source_path, source, i, "@content requires a page source; render with a page and template, or use @input for a partial");
                    break;
                }
                if (++result_.content_count > 1) {
                    fail(source_path, source, i, "@content may be executed exactly once for a templated tracked item");
                    break;
                }

                std::string content_source;
                fs::path content_identity;
                if (!page_source_->path.empty()) {
                    const fs::path content_path = fs::absolute(page_source_->path).lexically_normal();
                    if (std::find(input_stack_.begin(), input_stack_.end(), content_path) != input_stack_.end()) {
                        fail(source_path, source, i, "@content would result in an input loop through " + content_path.generic_string());
                        break;
                    }
                    content_identity = content_path;
                    // The read is the authority: missing/unreadable content
                    // yields nullptr -> the typed content error, with no
                    // separate readability probe (performance-regression repair).
                    const auto cached = host_.read_shared_source(content_path);
                    if (cached.status == nift::HostStatus::Error) { fail(source_path, source, i, cached.error); break; }
                    if (!cached.content) { fail(source_path, source, i, "content file is not readable"); break; }
                    content_source = frontmatter::parse_inline(*cached.content).body;
                    result_.dependencies.insert(page_source_->dependency.empty() ? host_.relative(content_path) : page_source_->dependency);
                } else {
                    content_identity = fs::path(page_source_->logical_name);
                    if (!content_identity.empty() &&
                        std::find(input_stack_.begin(), input_stack_.end(), content_identity) != input_stack_.end()) {
                        fail(source_path, source, i, "@content would result in an input loop through " + content_identity.generic_string());
                        break;
                    }
                    content_source = page_source_->text;
                }
                input_stack_.push_back(content_identity);
                const int insertion_code_block_depth = code_block_depth_;
                const auto nested = parse(content_source, content_identity, depth + 1);
                input_stack_.pop_back();

                if (!nested.ok) break;

                append_indented(output, nested.output, indent, insertion_code_block_depth);
                result_.content_used = true;
                i = end;
                continue;
            }

            if (function == "pathtopage") {
                if (!has_parameters || parameters.size() != 1) { fail(source_path, source, i, "pathtopage: expected 1 page number"); break; }
                const std::string raw_page = trim_copy(parameters[0]);
                const bool relative = !raw_page.empty() && (raw_page.front() == '+' || raw_page.front() == '-');
                const int relative_sign = !raw_page.empty() && raw_page.front() == '-' ? -1 : 1;
                const std::string page_argument = relative ? raw_page.substr(1) : raw_page;
                std::string resolved, interpolation_error;
                if (!interpolate_parameter(page_argument, resolved, interpolation_error)) { fail(source_path, source, i, "pathtopage: " + interpolation_error); break; }
                const std::string trimmed = trim_copy(resolved);
                char* endp = nullptr;
                const unsigned long long number = std::strtoull(trimmed.c_str(), &endp, 10);
                if (trimmed.empty() || !endp || *endp != '\0' || number == 0) { fail(source_path, source, i, "pathtopage: page/offset must be a positive integer"); break; }
                long long page = static_cast<long long>(number);
                if (relative) page = static_cast<long long>(pagination_current_) + relative_sign * static_cast<long long>(number);
                if (page < 1 || static_cast<unsigned long long>(page) > static_cast<unsigned long long>(pagination_total_)) {
                    fail(source_path, source, i, "pathtopage: resolved page must be between 1 and " + std::to_string(pagination_total_)); break;
                }
                output += path_to_page(static_cast<std::size_t>(page));
                if (!result_.ok) break;
                i = end;
                continue;
            }

            if (function == "filter" || function == "map" || function == "sort" || function == "slice" ||
                function == "find" || function == "some" || function == "every" || function == "distinct" || function == "reverse" ||
                function == "sum" || function == "prod" || function == "min" || function == "max" || function == "reduce") {
                if (!has_parameters) { fail(source_path, source, i, function + ": expected parameters"); break; }
                nift::RuntimeValue collection_result; std::string collection_error;
                const std::size_t call_end = (end > call_start && source[end - 1] == ';') ? end - 1 : end;
                const std::string call = "@" + function + source.substr(call_start, call_end - call_start);
                if (!evaluate_collection_value(call, collection_result, collection_error)) { fail(source_path, source, i, collection_error); break; }
                if(runtime_contains_bytes(collection_result)){fail(source_path,source,i,"bytes values cannot be rendered as text");break;}output += render_expression_value(collection_result); i = end; continue;
            }

            if (function == "substr") {
                if (!has_parameters || parameters.size() != 3) { fail(source_path, source, i, "substr: expected value, position and length"); break; }
                std::string text, interpolation_error;
                if (!interpolate_parameter(parameters[0], text, interpolation_error)) {
                    fail(source_path, source, i, "substr: " + interpolation_error); break;
                }
                auto parse_index = [&](const std::string& raw, const char* label, std::size_t& parsed) {
                    const std::string trimmed = trim_copy(raw);
                    if (trimmed.empty() || trimmed.front() == '-') {
                        fail(source_path, source, i, std::string("substr: ") + label + " must be a non-negative integer"); return false;
                    }
                    char* endp = nullptr;
                    const unsigned long long value = std::strtoull(trimmed.c_str(), &endp, 10);
                    if (!endp || *endp != '\0') {
                        fail(source_path, source, i, std::string("substr: ") + label + " must be a non-negative integer"); return false;
                    }
                    parsed = static_cast<std::size_t>(value);
                    return true;
                };
                std::size_t position = 0, length = 0;
                if (!parse_index(parameters[1], "position", position) || !parse_index(parameters[2], "length", length)) break;

                std::vector<std::size_t> offsets;
                offsets.reserve(text.size() + 1);
                for (std::size_t byte = 0; byte < text.size();) {
                    offsets.push_back(byte);
                    const unsigned char lead = static_cast<unsigned char>(text[byte]);
                    std::size_t width = 0;
                    if (lead < 0x80) width = 1;
                    else if ((lead & 0xE0) == 0xC0) width = 2;
                    else if ((lead & 0xF0) == 0xE0) width = 3;
                    else if ((lead & 0xF8) == 0xF0) width = 4;
                    else { fail(source_path, source, i, "substr: value contains invalid UTF-8"); break; }
                    if (byte + width > text.size()) { fail(source_path, source, i, "substr: value contains truncated UTF-8"); break; }
                    for (std::size_t c = 1; c < width; ++c) {
                        if ((static_cast<unsigned char>(text[byte + c]) & 0xC0) != 0x80) {
                            fail(source_path, source, i, "substr: value contains invalid UTF-8 continuation byte"); break;
                        }
                    }
                    if (!result_.ok) break;
                    byte += width;
                }
                if (!result_.ok) break;
                offsets.push_back(text.size());
                const std::size_t count = offsets.size() - 1;
                if (position < count && length > 0) {
                    const std::size_t finish = std::min(count, position + std::min(length, count - position));
                    output.append(text, offsets[position], offsets[finish] - offsets[position]);
                }
                i = end;
                continue;
            }

            if (function == "join") {
                if (!has_parameters || parameters.size() != 2) { fail(source_path, source, i, "join: expected array and separator"); break; }
                std::string expression = trim_copy(parameters[0]);
                if (expression.size() >= 3 && expression.rfind("$[", 0) == 0 && expression.back() == ']') {
                    expression = expression.substr(2, expression.size() - 3);
                }
                nift::RuntimeValue array_value;
                std::string join_error;
                if (!evaluate_collection_value(expression, array_value, join_error)) {
                    fail(source_path, source, i, "join: " + join_error); break;
                }
                auto array = std::make_shared<const nift::RuntimeValue>(std::move(array_value));
                if (!array->is_array()) { fail(source_path, source, i, "join: first parameter must resolve to a JSON array"); break; }
                std::string separator, interpolation_error;
                if (!interpolate_parameter(parameters[1], separator, interpolation_error)) {
                    fail(source_path, source, i, "join: " + interpolation_error); break;
                }
                for (std::size_t item_index = 0; item_index < array->array.size(); ++item_index) {
                    const auto& item = array->array[item_index];
                    if (item.is_bytes() || item.is_array() || item.is_object()) {
                        fail(source_path, source, i, "join: array items must be scalar JSON values");
                        break;
                    }
                    if (item_index) output += separator;
                    output += item.is_string() ? item.string : item.dump(0);
                }
                if (!result_.ok) break;
                i = end;
                continue;
            }

            if (function == "input") {
                if (!has_parameters || parameters.size() != 1) { fail(source_path, source, i, "input: expected 1 parameter"); break; }
                std::string resolved, interpolation_error;
                if (!interpolate_parameter(parameters[0], resolved, interpolation_error)) {
                    fail(source_path, source, i, "input: " + interpolation_error); break;
                }
                parameters[0] = std::move(resolved);
                fs::path input_path = parameters[0];
                if (input_path.is_relative()) {
                    // Only resolve against the current source's directory when
                    // that source actually has one (filesystem sources always
                    // do; in-memory sources only if the caller supplied a
                    // logical name with a directory). A bare in-memory source
                    // must never fall back to the process working directory.
                    const fs::path relative_to_source = source_path.parent_path() / input_path;
                    if (!source_path.parent_path().empty() && host_.source_exists(relative_to_source)) {
                        input_path = relative_to_source;
                    } else if (host_.root().empty()) {
                        fail(source_path, source, i, "@input cannot resolve relative path '" + parameters[0] + "' without a project root");
                        break;
                    } else {
                        input_path = host_.root() / input_path;
                    }
                }
                if (!host_.source_exists(input_path)) { fail(source_path, source, i, "@input path does not exist: " + parameters[0]); break; }
                input_path = fs::absolute(input_path).lexically_normal();
                if (std::find(input_stack_.begin(), input_stack_.end(), input_path) != input_stack_.end()) {
                    fail(source_path, source, i, "@input would result in an input loop through " + input_path.generic_string());
                    break;
                }
                input_stack_.push_back(input_path);
                result_.dependencies.insert(host_.relative(input_path));
                const int insertion_code_block_depth = code_block_depth_;
                // The read is the authority (no separate readability probe).
                const auto input_source = host_.read_shared_source(input_path);
                if (input_source.status == nift::HostStatus::Error) { fail(source_path, source, i, input_source.error); break; }
                if (!input_source.content) { fail(source_path, source, i, "input file is not readable"); break; }
                push_variable_scope();
                const auto nested = parse(*input_source.content, input_path, depth + 1);
                pop_variable_scope();
                input_stack_.pop_back();
                if (!nested.ok) break;
                append_indented(output, nested.output, indent, insertion_code_block_depth);
                i = end;
                continue;
            }

            if (function == "path" || function == "pathto" || function == "pathtofile") {
                if (!has_parameters || parameters.size() != 1) { fail(source_path, source, i, "@" + function + " expects exactly one path/name"); break; }
                std::string resolved, interpolation_error;
                if (!interpolate_parameter(parameters[0], resolved, interpolation_error)) {
                    fail(source_path, source, i, function + ": " + interpolation_error); break;
                }
                parameters[0] = std::move(resolved);

                if (!host_.tracked_output_path(parameters[0])) {
                    const fs::path target_path = (host_.root() / parameters[0]).lexically_normal();
                    if (!filesystem::path_within(host_.root(), target_path)) {
                        fail(source_path, source, i, "@" + function + " path must stay inside the Nift project: " + parameters[0]);
                        break;
                    }
                }

                output += path_to(parameters[0], function);
                if (!result_.ok) { if (result_.error.source_file.empty()) fail(source_path, source, i, result_.error.message); break; }
                fs::path requirement;
                if (const auto target = host_.tracked_output_path(parameters[0])) requirement = target->path;
                else requirement = host_.root() / parameters[0];
                result_.reqs.insert(host_.relative(requirement.lexically_normal()));
                i = end;
                continue;
            }

            if (function == "getenv") {
                if (!has_parameters || parameters.size() != 1) { fail(source_path, source, i, "getenv: expected 1 parameter"); break; }
                std::string resolved, interpolation_error;
                if (!interpolate_parameter(parameters[0], resolved, interpolation_error)) {
                    fail(source_path, source, i, "getenv: " + interpolation_error); break;
                }
                parameters[0] = std::move(resolved);
                nift::HostResult env = host_.environment(parameters[0]);
                if (env.status == nift::HostStatus::Found) output += env.value;
                else if (env.status == nift::HostStatus::Error) {
                    fail(source_path, source, i, "getenv: " + env.error);
                    break;
                }
                i = end;
                continue;
            }

            if (function == "ent") {
                if (!has_parameters || parameters.size() != 1) { fail(source_path, source, i, "@ent expects exactly one entity"); break; }
                std::string resolved, interpolation_error;
                if (!interpolate_parameter(parameters[0], resolved, interpolation_error)) {
                    fail(source_path, source, i, "ent: " + interpolation_error); break;
                }
                parameters[0] = std::move(resolved);
                bool known = false;
                output += entity(parameters[0], known);
                if (!known) { fail(source_path, source, i, "do not currently have an entity value for '" + parameters[0] + "'"); break; }
                i = end;
                continue;
            }

            if (function == "markup") {
                std::size_t block_open = end;
                while (block_open < source.size() &&
                       std::isspace(static_cast<unsigned char>(source[block_open]))) ++block_open;
                const bool inline_source = block_open < source.size() && source[block_open] == '{';
                if (!has_parameters || (inline_source ? parameters.size() != 1 : parameters.size() != 2)) {
                    fail(source_path, source, i,
                         inline_source ? "markup: inline syntax is @markup(format){...}"
                                       : "markup: file syntax is @markup(format, path)");
                    break;
                }

                std::string format_name, interpolation_error;
                if (!interpolate_parameter(parameters[0], format_name, interpolation_error)) {
                    fail(source_path, source, i, "markup: " + interpolation_error);
                    break;
                }
                markup::Format format;
                const std::string normalized_format = trim_copy(format_name);
                if (normalized_format == "md" || normalized_format == "markdown") format = markup::Format::Markdown;
                else if (normalized_format == "adoc" || normalized_format == "asciidoc") format = markup::Format::AsciiDoc;
                else if (normalized_format == "rst" || normalized_format == "restructuredtext") format = markup::Format::ReStructuredText;
                else {
                    fail(source_path, source, i, "markup: unknown format '" + normalized_format +
                         "' (expected md, adoc or rst)");
                    break;
                }

                std::string markup_source;
                fs::path markup_identity = source_path;
                std::size_t directive_end = end;
                if (inline_source) {
                    std::size_t block_close = 0;
                    if (!find_balanced(source, block_open, '{', '}', block_close)) {
                        fail(source_path, source, block_open, "@markup block has no matching '}'");
                        break;
                    }
                    const auto body = normalize_control_block_body(
                        source.substr(block_open + 1, block_close - block_open - 1));
                    const auto templated = parse(body.text, source_path, depth + 1);
                    if (!templated.ok) break;
                    markup_source = templated.output;
                    directive_end = block_close + 1;
                } else {
                    std::string resolved_path;
                    if (!interpolate_parameter(parameters[1], resolved_path, interpolation_error)) {
                        fail(source_path, source, i, "markup: " + interpolation_error);
                        break;
                    }
                    fs::path candidate = resolved_path;
                    if (candidate.is_relative()) candidate = host_.root() / candidate;
                    candidate = fs::absolute(candidate).lexically_normal();
                    if (!filesystem::path_within(host_.root().lexically_normal(), candidate)) {
                        fail(source_path, source, i,
                             "markup: path must stay inside the Nift project: " + resolved_path);
                        break;
                    }
                    if (!host_.source_exists(candidate)) {
                        fail(source_path, source, i, "markup: file does not exist: " + resolved_path);
                        break;
                    }
                    if (std::find(input_stack_.begin(), input_stack_.end(), candidate) != input_stack_.end()) {
                        fail(source_path, source, i,
                             "@markup would result in an input loop through " + candidate.generic_string());
                        break;
                    }
                    const auto loaded = host_.read_shared_source(candidate);
                    if (loaded.status == nift::HostStatus::Error) {
                        fail(source_path, source, i, "markup: " + loaded.error);
                        break;
                    }
                    if (!loaded.content) {
                        fail(source_path, source, i, "markup: file is not readable: " + resolved_path);
                        break;
                    }
                    result_.dependencies.insert(host_.relative(candidate));
                    input_stack_.push_back(candidate);
                    const auto templated = parse(*loaded.content, candidate, depth + 1);
                    input_stack_.pop_back();
                    if (!templated.ok) break;
                    markup_source = templated.output;
                    markup_identity = candidate;
                }

                markup::Options options;
                options.allow_raw_html = true;
                options.asciidoc_source_identity = markup_identity.generic_string();
                options.rst_source_identity = markup_identity.generic_string();
                auto resolve_resource = [&](const std::string& including,
                                            const std::string& requested,
                                            std::string& content,
                                            std::string& canonical,
                                            std::string& resolver_error) {
                    fs::path base = fs::path(including).parent_path();
                    fs::path candidate = fs::path(requested);
                    if (candidate.is_relative()) candidate = base / candidate;
                    candidate = fs::absolute(candidate).lexically_normal();
                    if (!filesystem::path_within(host_.root().lexically_normal(), candidate)) {
                        resolver_error = "markup include must stay inside the Nift project: " + requested;
                        return false;
                    }
                    if (!host_.source_exists(candidate)) {
                        resolver_error = "markup include does not exist: " + requested;
                        return false;
                    }
                    const auto loaded = host_.read_shared_source(candidate);
                    if (loaded.status == nift::HostStatus::Error) {
                        resolver_error = loaded.error;
                        return false;
                    }
                    if (!loaded.content) {
                        resolver_error = "markup include is not readable: " + requested;
                        return false;
                    }
                    if (std::find(input_stack_.begin(), input_stack_.end(), candidate) != input_stack_.end()) {
                        resolver_error = "markup include would form a cycle through " + candidate.generic_string();
                        return false;
                    }
                    result_.dependencies.insert(host_.relative(candidate));
                    input_stack_.push_back(candidate);
                    const auto templated = parse(*loaded.content, candidate, depth + 1);
                    input_stack_.pop_back();
                    if (!templated.ok) {
                        resolver_error = result_.error.message;
                        return false;
                    }
                    content = templated.output;
                    canonical = candidate.generic_string();
                    return true;
                };
                options.asciidoc_include_resolver = resolve_resource;
                options.rst_resource_resolver = resolve_resource;
                options.asciidoc_dependency = [&](const std::string& identity) {
                    result_.dependencies.insert(host_.relative(fs::path(identity)));
                };
                options.rst_dependency = options.asciidoc_dependency;
                std::vector<std::string> diagnostics;
                options.asciidoc_diagnostic = [&](const std::string& message) { diagnostics.push_back(message); };
                options.rst_diagnostic = options.asciidoc_diagnostic;

                std::string html, conversion_error;
                if (!markup::convert(format, markup_source, html, conversion_error, options)) {
                    fail(source_path, source, i, "markup: " + conversion_error);
                    break;
                }
                if (!diagnostics.empty()) {
                    fail(source_path, source, i, "markup: " + diagnostics.front());
                    break;
                }
                append_indented(output, html, indent, code_block_depth_);
                i = directive_end;
                continue;
            }

            if (function == "json") {
                std::size_t block_open = end;
                while (block_open < source.size() &&
                       std::isspace(static_cast<unsigned char>(source[block_open]))) ++block_open;
                const bool inline_json = block_open < source.size() && source[block_open] == '{';
                const bool arity_ok = inline_json
                    ? (parameters.size() == 1 || parameters.size() == 2)
                    : (parameters.size() == 2 || parameters.size() == 3);
                if (!has_parameters || !arity_ok) {
                    fail(source_path, source, i,
                         "json: expected @json(name, path), @json(name, schema, path), "
                         "@json(name){...} or @json(name, schema){...}");
                    break;
                }

                std::string binding_name = trim_copy(parameters[0]);
                if (!valid_binding_identifier(binding_name)) {
                    fail(source_path, source, i,
                         "json: name must be an identifier using letters, digits and underscores");
                    break;
                }
                if (reserved_binding_name(binding_name)) {
                    fail(source_path, source, i, "json: name '" + binding_name +
                         "' conflicts with built-in metadata/reserved bindings");
                    break;
                }
                if (host_.is_contract_name(binding_name)) {
                    fail(source_path, source, i, "json: name '" + binding_name +
                         "' conflicts with configured contract namespace");
                    break;
                }
                if (host_.binding(binding_name) || json_bindings_.count(binding_name)) {
                    fail(source_path, source, i, "json: name '" + binding_name + "' is already bound");
                    break;
                }

                std::shared_ptr<const nift::RuntimeValue> document;
                std::string instance_label = "inline JSON";
                std::size_t directive_end = end;
                std::string interpolation_error;
                if (inline_json) {
                    std::size_t block_close = 0;
                    if (!find_balanced(source, block_open, '{', '}', block_close)) {
                        fail(source_path, source, block_open, "@json block has no matching '}'");
                        break;
                    }
                    const auto body = normalize_control_block_body(
                        source.substr(block_open + 1, block_close - block_open - 1));
                    const auto templated = parse(body.text, source_path, depth + 1);
                    if (!templated.ok) break;
                    nift::RuntimeValue parsed;
                    std::string parse_error;
                    if (!parse_runtime_json(templated.output, parsed, parse_error)) {
                        fail(source_path, source, i, "json: failed to parse inline JSON (" + parse_error + ")");
                        break;
                    }
                    document = std::make_shared<const nift::RuntimeValue>(std::move(parsed));
                    directive_end = block_close + 1;
                } else {
                    const std::size_t path_index = parameters.size() - 1;
                    std::string json_path_argument;
                    if (!interpolate_parameter(parameters[path_index], json_path_argument, interpolation_error)) {
                        fail(source_path, source, i, "json: " + interpolation_error);
                        break;
                    }
                    const fs::path host_root = host_.root().lexically_normal();
                    const fs::path json_path = (host_.root() / json_path_argument).lexically_normal();
                    if (!filesystem::path_within(host_root, json_path)) {
                        fail(source_path, source, i,
                             "json: path must stay inside the Nift project: " + json_path_argument);
                        break;
                    }
                    if (!host_.source_exists(json_path)) {
                        fail(source_path, source, i, "json: file does not exist: " + json_path_argument);
                        break;
                    }
                    std::string parse_error;
                    auto runtime_document = host_.read_shared_runtime_json(json_path, parse_error);
                    if (!runtime_document) {
                        fail(source_path, source, i, "json: failed to parse " + json_path_argument +
                             (parse_error.empty() ? "" : " (" + parse_error + ")"));
                        break;
                    }
                    document = std::move(runtime_document);
                    instance_label = json_path_argument;
                    result_.dependencies.insert(host_.relative(json_path));
                }

                const bool has_schema = inline_json ? parameters.size() == 2 : parameters.size() == 3;
                if (has_schema) {
                    const std::size_t schema_index = 1;
                    const bool schema_was_quoted = parameter_quoted.size() > schema_index &&
                                                   parameter_quoted[schema_index];
                    const std::string schema_reference = trim_copy(parameters[schema_index]);
                    std::shared_ptr<const json::Document> schema;
                    std::string schema_label;
                    if (!schema_was_quoted) {
                        // A bare identifier in the schema position is a schema
                        // binding name and must already exist; it must never
                        // fall back to a same-named file.
                        if (!valid_binding_identifier(schema_reference)) {
                            fail(source_path, source, i,
                                 "json: schema name '" + schema_reference +
                                 "' must be an existing binding name or a quoted schema path");
                            break;
                        }
                        std::string binding_error;
                        std::shared_ptr<const nift::RuntimeValue> runtime_schema;
                        if (!resolve_json_value(schema_reference, runtime_schema, binding_error)) {
                            fail(source_path, source, i,
                                 "json: schema name '" + schema_reference +
                                 "' is not bound (quote the argument to use a schema path)");
                            break;
                        }
                        if (!binding_error.empty()) {
                            fail(source_path, source, i, "json: " + binding_error);
                            break;
                        }
                        json::Document schema_document;
                        std::string conversion_error;
                        if (!nift::runtime_to_json(*runtime_schema, schema_document, conversion_error)) {
                            fail(source_path, source, i, "json: " + conversion_error);
                            break;
                        }
                        schema = std::make_shared<const json::Document>(std::move(schema_document));
                        schema_label = schema_reference;
                    } else {
                        std::string schema_path_argument;
                        if (!interpolate_parameter(parameters[schema_index], schema_path_argument,
                                                   interpolation_error)) {
                            fail(source_path, source, i, "json: " + interpolation_error);
                            break;
                        }
                        const fs::path host_root = host_.root().lexically_normal();
                        const fs::path schema_path = (host_.root() / schema_path_argument).lexically_normal();
                        if (!filesystem::path_within(host_root, schema_path)) {
                            fail(source_path, source, i,
                                 "json: schema path must stay inside the Nift project: " + schema_path_argument);
                            break;
                        }
                        if (!host_.source_exists(schema_path)) {
                            fail(source_path, source, i,
                                 "json: schema file does not exist: " + schema_path_argument);
                            break;
                        }
                        std::string schema_parse_error;
                        schema = host_.read_shared_json(schema_path, schema_parse_error);
                        if (!schema) {
                            fail(source_path, source, i, "json: failed to parse schema " + schema_path_argument +
                                 (schema_parse_error.empty() ? "" : " (" + schema_parse_error + ")"));
                            break;
                        }
                        schema_label = schema_path_argument;
                        result_.dependencies.insert(host_.relative(schema_path));
                    }
                    std::string validation_error;
                    json::Document instance_document;
                    std::string conversion_error;
                    if (!nift::runtime_to_json(*document, instance_document, conversion_error)) {
                        fail(source_path, source, i, "json: " + conversion_error);
                        break;
                    }
                    if (!jsonschema::validate(instance_document, *schema, validation_error)) {
                        fail(source_path, source, i, "json: " + instance_label+
                             " does not satisfy schema " + schema_label + " (" + validation_error + ")");
                        break;
                    }
                }

                json_bindings_.emplace(binding_name, std::move(document));
                if (!json_binding_scopes_.empty()) json_binding_scopes_.back().push_back(binding_name);
                i = directive_end;
                continue;
            }

            if (function == "dep") {
                if (!has_parameters || parameters.empty()) { fail(source_path, source, i, "dep: expected parameters"); break; }
                for (auto& dependency : parameters) {
                    std::string resolved, interpolation_error;
                    if (!interpolate_parameter(dependency, resolved, interpolation_error)) {
                        fail(source_path, source, i, "dep: " + interpolation_error);
                        break;
                    }
                    dependency = std::move(resolved);
                    const fs::path dependency_path = (host_.root() / dependency).lexically_normal();
                    if (!filesystem::path_within(host_.root(), dependency_path)) {
                        fail(source_path, source, i, "dep: path must stay inside the Nift project: " + dependency);
                        break;
                    }
                    if (!host_.source_exists(dependency_path)) {
                        fail(source_path, source, i, "failed as dependency does not exist: " + dependency);
                        break;
                    }
                    result_.dependencies.insert(host_.relative(dependency_path));
                }
                if (!result_.ok) break;
                i = end;
                continue;
            }

            output += source.substr(i, end - i);
            i = end;
            continue;
        }

        if (source[i] == '\\' || source[i] == '@' || source[i] == '<' || source[i] == '$') {
            output.push_back(source[i++]);
        } else {
            const std::size_t next_special = source.find_first_of("\\@<$", i);
            std::size_t end = next_special == std::string::npos ? source.size() : next_special;
            if(strict_script_mode_){for(std::size_t p=source.find("import",i);p<end;p=source.find("import",p+6)){const bool left=p==0||(!std::isalnum(static_cast<unsigned char>(source[p-1]))&&source[p-1]!='_');const std::size_t after=p+6;const bool right=after==source.size()||(!std::isalnum(static_cast<unsigned char>(source[after]))&&source[after]!='_');if(left&&right){end=p;break;}if(p==std::string::npos)break;}}
            output.append(source, i, end - i);
            i = end;
        }
    }

    if (result_.ok && code_block_depth_ > base_code_block_depth) {
        fail(source_path, source, open_code_offset, "<pre*> open tag has no following </pre> close tag");
        code_block_depth_ = base_code_block_depth;
    }

    result_.output = output;
    return result_;
}

RenderResult Parser::render_composed(const RenderSource& template_source,
                                     const std::optional<RenderSource>& page_source,
                                     bool require_exactly_one_content) {
    std::string template_content;
    fs::path template_identity;
    if (!template_source.path.empty()) {
        template_identity = fs::absolute(template_source.path).lexically_normal();
        const auto cached = host_.read_shared_source(template_identity);
        if (cached.status == nift::HostStatus::Error) {
            result_.ok = false;
            result_.error = {tracked_info_.name, template_identity, 0, cached.error};
            return result_;
        }
        if (!cached.content) {
            result_.ok = false;
            result_.error = {tracked_info_.name, template_identity, 0, "template file is not readable"};
            return result_;
        }
        template_content = *cached.content;
    } else {
        template_identity = fs::path(template_source.logical_name);
        template_content = template_source.text;
    }

    page_source_ = page_source;
    input_stack_.push_back(template_identity);
    if (!template_source.dependency.empty()) result_.dependencies.insert(template_source.dependency);
    auto result = parse(template_content, template_identity, 0);
    input_stack_.pop_back();

    if (result.ok && require_exactly_one_content && result.content_count != 1) {
        result.ok = false;
        result.error = {tracked_info_.name, template_identity, 0, "templated tracked items must execute exactly one @content; add @content through the template/input graph or omit the tracked template field"};
    }
    return result;
}

RenderResult Parser::render() {
    if (auto it=json_bindings_.find("frontmatter"); it!=json_bindings_.end() && it->second && it->second->is_object() && it->second->has("_error")) { result_.ok=false; result_.error={tracked_info_.name,host_.content_path(tracked_info_),0,(*it->second)["_error"].string}; return result_; }
    const fs::path content_path = host_.content_path(tracked_info_);
    if (tracked_info_.template_path.empty()) {
        // The read is the authority: missing/unreadable content yields nullptr
        // -> the typed content error, with no separate readability probe.
        const auto content_source = host_.read_shared_source(content_path);
        if (content_source.status == nift::HostStatus::Error) {
            result_.ok = false;
            result_.error = {tracked_info_.name, content_path, 0, content_source.error};
            return result_;
        }
        if (!content_source.content) {
            result_.ok = false;
            result_.error = {tracked_info_.name, content_path, 0, "content file is not readable"};
            return result_;
        }
        auto fm = frontmatter::parse_inline(*content_source.content);
        if (!fm.error.empty()) { result_.ok=false; result_.error={tracked_info_.name,content_path,0,fm.error}; return result_; }
        if (tracked_info_.frontmatter && fm.present) { result_.ok=false; result_.error={tracked_info_.name,content_path,0,"multiple front matter sources are not allowed"}; return result_; }
        auto result = parse(fm.body, content_path, 0);
        result.content_used = true;
        result.dependencies.insert(host_.relative(content_path));
        return result;
    }

    const fs::path template_path = host_.root() / tracked_info_.template_path;
    // One existence probe distinguishes "does not exist" from the read's
    // "not readable"; render_composed's read classifies the latter.
    if (!host_.source_exists(template_path)) {
        result_.ok = false;
        result_.error = {tracked_info_.name, template_path, 0, "template file does not exist"};
        return result_;
    }

    RenderSource template_source;
    template_source.path = template_path;
    template_source.dependency = tracked_info_.template_path;
    RenderSource page_render_source;
    page_render_source.path = content_path;
    page_render_source.dependency = host_.relative(content_path);

    pagination_collecting_ = tracked_info_.paginate.has_value();
    auto result = render_composed(template_source, page_render_source, /*require_exactly_one_content=*/true);
    if (!result.ok || !tracked_info_.paginate.has_value()) return result;
    if (result.paginate_count != 1) {
        result.ok = false;
        result.error = {tracked_info_.name, content_path, 0, "paginated tracked items must execute exactly one @paginate"};
        return result;
    }

    const auto& config = *tracked_info_.paginate;
    auto conventional = [&](const char* suffix) {
        return content_path.parent_path() / (content_path.stem().generic_string() + suffix + ".html");
    };
    fs::path pagination_template = config.template_path.has_value()
        ? (host_.root() / *config.template_path).lexically_normal()
        : conventional(".paginate");
    if (!filesystem::path_within(host_.root(), pagination_template) || !host_.source_readable(pagination_template)) {
        result.ok = false;
        result.error = {tracked_info_.name, pagination_template, 0, "pagination template is missing, unreadable, or outside the project"};
        return result;
    }
    fs::path separator;
    if (config.separator_path.has_value()) separator = (host_.root() / *config.separator_path).lexically_normal();
    else {
        const fs::path candidate = conventional(".separator");
        if (host_.source_exists(candidate)) separator = candidate;
    }
    if (!separator.empty() && (!filesystem::path_within(host_.root(), separator) || !host_.source_readable(separator))) {
        result.ok = false;
        result.error = {tracked_info_.name, separator, 0, "pagination separator is unreadable or outside the project"};
        return result;
    }

    result_.dependencies.insert(host_.relative(pagination_template));
    if (!separator.empty()) result_.dependencies.insert(host_.relative(separator));
    const std::string base_output = result.output;
    const std::string marker = "\x1dNIFT_PAGINATE\x1d";
    const std::size_t marker_position = base_output.find(marker);
    const std::size_t total = std::max<std::size_t>(1, (result_.pagination_items.size() + config.items_per_page - 1) / config.items_per_page);
    struct PageRender {
        bool ok = false;
        std::string output;
        BuildError error;
        std::set<std::string> dependencies;
        std::set<std::string> reqs;
    };
    std::vector<PageRender> pages(total);
    const auto page_source = host_.read_shared_source(pagination_template);
    if (page_source.status == nift::HostStatus::Error) {
        result_.ok = false;
        result_.error = {tracked_info_.name, pagination_template, 0, page_source.error};
        return result_;
    }
    if (!page_source.content) {
        result_.ok = false;
        result_.error = {tracked_info_.name, pagination_template, 0, "pagination template is missing, unreadable, or outside the project"};
        return result_;
    }
    // The separator is OPTIONAL only in its absence: NotFound means "no
    // separator", but a host ERROR must fail the render with its diagnostic
    // (Error != NotFound). Found with an empty value is a valid empty separator.
    const std::string* separator_source = nullptr;
    if (!separator.empty()) {
        const auto separator_read = host_.read_shared_source(separator);
        if (separator_read.status == nift::HostStatus::Error) {
            result_.ok = false;
            result_.error = {tracked_info_.name, separator, 0, separator_read.error};
            return result_;
        }
        separator_source = separator_read.content;
    }

    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    std::size_t thread_count = tracked_info_.paginate.has_value()
        ? (host_.build_threads() < 0
            ? static_cast<std::size_t>(-static_cast<long long>(host_.build_threads())) * hardware
            : (host_.build_threads() == 0 ? hardware : static_cast<std::size_t>(host_.build_threads())))
        : 1;
    thread_count = std::max<std::size_t>(1, std::min(thread_count, total));
    std::atomic<std::size_t> next_page{0};

    auto render_page = [&] {
        while (true) {
            const std::size_t page_index = next_page.fetch_add(1);
            if (page_index >= total) break;
            const std::size_t page = page_index + 1;
            Parser page_parser(host_, tracked_info_);
            page_parser.json_bindings_ = json_bindings_;
            page_parser.contract_bindings_ = contract_bindings_;
            page_parser.page_source_ = page_source_;
            page_parser.pagination_collecting_ = false;
            page_parser.pagination_context_active_ = true;
            page_parser.pagination_current_ = page;
            page_parser.pagination_total_ = total;
            page_parser.pagination_current_output_ = host_.pagination_output_path(tracked_info_, page);
            page_parser.result_.content_count = result_.content_count;

            const std::size_t begin = page_index * config.items_per_page;
            const std::size_t finish = std::min(result_.pagination_items.size(), begin + config.items_per_page);
            std::string separator_text;
            if (separator_source && finish > begin + 1) {
                const auto rendered_separator = page_parser.parse(*separator_source, separator, 0);
                if (!rendered_separator.ok) {
                    pages[page_index].error = rendered_separator.error;
                    continue;
                }
                separator_text = rendered_separator.output;
            }
            std::string items;
            for (std::size_t index = begin; index < finish; ++index) {
                if (index != begin) items += separator_text;
                items += result_.pagination_items[index];
            }
            page_parser.pagination_items_text_ = std::move(items);
            const auto rendered_page = page_parser.parse(*page_source.content, pagination_template, 0);
            if (!rendered_page.ok) {
                pages[page_index].error = rendered_page.error;
                continue;
            }
            std::string page_output = base_output;
            page_output.replace(marker_position, marker.size(), rendered_page.output);
            pages[page_index].ok = true;
            pages[page_index].output = std::move(page_output);
            pages[page_index].dependencies = rendered_page.dependencies;
            pages[page_index].reqs = rendered_page.reqs;
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (std::size_t worker = 0; worker < thread_count; ++worker) workers.emplace_back(render_page);
    for (auto& worker : workers) worker.join();

    result_.pagination_outputs.clear();
    result_.pagination_outputs.reserve(total);
    for (std::size_t page_index = 0; page_index < total; ++page_index) {
        if (!pages[page_index].ok) {
            result_.ok = false;
            result_.error = pages[page_index].error;
            return result_;
        }
        result_.dependencies.insert(pages[page_index].dependencies.begin(), pages[page_index].dependencies.end());
        result_.reqs.insert(pages[page_index].reqs.begin(), pages[page_index].reqs.end());
        result_.pagination_outputs.push_back(std::move(pages[page_index].output));
    }
    result_.output = result_.pagination_outputs.front();
    return result_;
}
