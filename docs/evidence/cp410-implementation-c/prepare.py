from pathlib import Path
p=Path('.build/cp410-implementation/filesystem')
for name in ('Parser.h','ParserExpression.cpp','ParserTemplate.cpp','ParserHelpers.h'): (p/name).write_text((Path('src')/name).read_text())
f=p/'Parser.h';s=f.read_text();needle='    bool evaluate_expression_impl(const std::string& expression, nift::RuntimeValue& value, std::string& error, nift::detail::SourceView view);';assert needle in s
s=s.replace(needle,'''    struct PreparedFilesystemOperand {
        std::string text;
        std::size_t begin=0, length=0;
        bool quoted=false;
    };
    struct PreparedFilesystemOperation {
        std::string method;
        std::vector<PreparedFilesystemOperand> operands;
    };
    std::shared_ptr<const PreparedFilesystemOperation> prepare_filesystem_operation(const std::string& source) const;
    bool evaluate_filesystem_expression(const std::string& expression, nift::RuntimeValue& value, std::string& error, nift::detail::SourceView view, const PreparedFilesystemOperation* recipe);
    bool evaluate_expression_impl(const std::string& expression, nift::RuntimeValue& value, std::string& error, nift::detail::SourceView view, const PreparedFilesystemOperation* recipe=nullptr);''');f.write_text(s)
f=p/'ParserExpression.cpp';s=f.read_text();pos=s.index('bool Parser::evaluate_expression(')
prepare='''std::shared_ptr<const Parser::PreparedFilesystemOperation> Parser::prepare_filesystem_operation(const std::string& source) const {
    const auto open=source.find('(');
    if (open==std::string::npos || source.empty() || source.back()!=')') return {};
    const auto method=source.substr(0,open);
    if (method!="move" && method!="mv" && method!="copy" && method!="cp" && method!="stat") return {};
    std::size_t close=0;
    if (!find_balanced(source,open,'(',')',close) || close!=source.size()-1) return {};
    auto identity=nift::detail::SourceView::identity({},source);
    bool ok=false;std::vector<bool> quoted;
    auto args=parse_parameters(nift::detail::SourceText(source,identity).substr(open+1,source.size()-open-2),ok,&quoted);
    if (!ok || (method=="stat" ? args.size()!=1 : args.size()<2)) return {};
    auto recipe=std::make_shared<PreparedFilesystemOperation>();recipe->method=method;
    for (std::size_t i=0;i<args.size();++i) {
        if (!quoted[i] && trim_copy(args[i]).rfind("...",0)==0) return {};
        const auto range=args[i].view.original_range(0,args[i].size());
        recipe->operands.push_back({args[i],range.first,range.second,quoted[i]});
    }
    return recipe;
}

'''
s=s[:pos]+prepare+s[pos:]
needle='''    const std::uint64_t file_checkpoint = begin_file_operation();''';pos=s.index(needle,s.index('bool Parser::evaluate_expression('));s=s[:pos]+'''    return evaluate_filesystem_expression(expression,value,error,std::move(view),nullptr);
}

bool Parser::evaluate_filesystem_expression(const std::string& expression,nift::RuntimeValue& value,std::string& error,nift::detail::SourceView view,const PreparedFilesystemOperation* recipe) {
'''+s[pos:];s=s.replace('evaluate_expression_impl(expression, value, error, std::move(view))','evaluate_expression_impl(expression, value, error, std::move(view), recipe)',1)
s=s.replace('bool Parser::evaluate_expression_impl(const std::string& expression, nift::RuntimeValue& value, std::string& error, nift::detail::SourceView view) {','bool Parser::evaluate_expression_impl(const std::string& expression, nift::RuntimeValue& value, std::string& error, nift::detail::SourceView view, const PreparedFilesystemOperation* recipe) {',1)
# Use the very same recursive legacy evaluator for the canonical evaluation
# phase. No AST re-evaluation, speculative values, or extra operand wrappers.
anchor='''        auto find_binding = [&](const std::string& name) -> VariableBinding* {''';pos=s.index(anchor,s.index('std::function<bool(const nift::detail::SourceText&'))
fast='''        if (recipe && depth==0) {
            const auto& name=recipe->method;
            const bool moving=name=="move"||name=="mv";
            const std::string operation=name=="stat"?"stat":moving?"move":"copy";
            auto operand=[&](std::size_t i,nift::RuntimeValue& value) {
                const auto& arg=recipe->operands[i];
                if (arg.quoted) { value=nift::RuntimeValue(arg.text);return true; }
                return eval(nift::detail::SourceText(arg.text,text.view.slice(arg.begin,arg.length)),value,depth+1);
            };
            auto resolve_path=[&](const std::string& raw)->fs::path{std::string expanded=raw;if(standalone_script_host_&&!expanded.empty()&&expanded[0]=='~'&&(expanded.size()==1||expanded[1]=='/'||expanded[1]=='\\\\')){const char* home=std::getenv("HOME");
#ifdef _WIN32
if(!home)home=std::getenv("USERPROFILE");
#endif
if(home)expanded=std::string(home)+expanded.substr(1);}fs::path path(expanded);if(path.is_relative())path=(standalone_script_host_?fs::current_path():host_.root())/path;return fs::absolute(path).lexically_normal();};
            auto validate=[&](const fs::path& path) {
                if(!standalone_script_host_&&!resource_path_authority_.project_root.empty()&&!filesystem::path_within(resource_path_authority_.project_root,path)){error=operation+": path must stay inside the Nift project";return false;}
                if(!nift_fs_root_allowed(path,resource_path_authority_.enforce_filesystem_root?resource_path_authority_.filesystem_root:fs::path{},error)){error=operation+": "+error;return false;}return true;
            };
            if(name=="stat") {
                nift::RuntimeValue path_value;if(!operand(0,path_value)||!path_value.is_string()){error="stat: expected string path";return false;}
                auto path=resolve_path(path_value.string);if(!validate(path))return false;
                const auto info=inspect_path(path);if(info.error)return fail_recoverable(nift::detail::DiagnosticCode::IoMetadataFailed,"stat: "+info.error_message,error);
                out=nift::RuntimeValue::make_object();out["exists"]=nift::RuntimeValue(info.exists);
                if(info.exists){out["type"]=nift::RuntimeValue(info.type);if(info.type=="file")out["size"]=nift::RuntimeValue(static_cast<double>(info.size));}return true;
            }
            auto paths=[&](std::size_t begin,std::size_t end,std::vector<fs::path>& result) {
                for(std::size_t i=begin;i<end;++i){nift::RuntimeValue value;if(!operand(i,value))return false;std::vector<std::string> raws;
                    if(value.is_string())raws.push_back(value.string);else if(value.is_array()){for(const auto& x:value.array){if(!x.is_string()){error=operation+": path arrays must contain strings";return false;}raws.push_back(x.string);}}else{error=operation+": expected string path or array of paths";return false;}
                    for(const auto& raw:raws){auto path=resolve_path(raw);if(!validate(path))return false;if(glob_has_magic(raw)){auto matches=glob_expand(path);result.insert(result.end(),matches.begin(),matches.end());}else result.push_back(std::move(path));}
                }return true;
            };
            std::vector<fs::path> sources;if(!paths(0,recipe->operands.size()-1,sources))return false;
            if(sources.empty()){error=operation+": glob matched no source files";return false;}
            std::vector<fs::path> destinations;if(!paths(recipe->operands.size()-1,recipe->operands.size(),destinations)||destinations.size()!=1){error=operation+": destination must be one path";return false;}
            const auto& destination=destinations.front();std::error_code destination_error;const bool destination_directory=fs::is_directory(destination,destination_error);
            if(sources.size()>1&&!destination_directory){error=operation+": destination must be an existing directory for multiple sources";return false;}
            for(const auto& source:sources){auto target=destination_directory?destination/source.filename():destination;std::error_code ec;if(moving)fs::rename(source,target,ec);else fs::copy_file(source,target,fs::copy_options::overwrite_existing,ec);if(ec)return fail_recoverable(moving?nift::detail::DiagnosticCode::IoMoveFailed:nift::detail::DiagnosticCode::IoCopyFailed,operation+": "+ec.message(),error);}
            out=nift::RuntimeValue(nullptr);return true;
        }
'''
s=s[:pos]+fast+s[pos:];f.write_text(s)
f=p/'ParserTemplate.cpp';s=f.read_text();needle='''        std::optional<nift::ast::Context> prepared_context_;''';s=s.replace(needle,needle+'''\n        std::unordered_map<std::string,std::shared_ptr<const PreparedFilesystemOperation>> prepared_filesystem_recipes;''',1)
needle='''c.legacy_with_span=[&](const std::string& x,nift::ast::SourceSpan span,nift::RuntimeValue& out,std::string& e){return evaluate_expression(x,out,e,prepared_view->slice(span.begin,span.end-span.begin));};'''
assert s.count(needle)==1;s=s.replace(needle,'''c.legacy_with_span=[&](const std::string& x,nift::ast::SourceSpan span,nift::RuntimeValue& out,std::string& e){
    if(x.rfind("move(",0)==0||x.rfind("mv(",0)==0||x.rfind("copy(",0)==0||x.rfind("cp(",0)==0||x.rfind("stat(",0)==0){
        auto found=prepared_filesystem_recipes.find(x);
        if(found==prepared_filesystem_recipes.end())found=prepared_filesystem_recipes.emplace(x,prepare_filesystem_operation(x)).first;
        if(found->second)return evaluate_filesystem_expression(x,out,e,prepared_view->slice(span.begin,span.end-span.begin),found->second.get());
    }
    return evaluate_expression(x,out,e,prepared_view->slice(span.begin,span.end-span.begin));};''',1);f.write_text(s)
print('Prepared private source-aware canonical-fallback recipe')
