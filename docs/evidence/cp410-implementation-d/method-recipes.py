from pathlib import Path
p=Path(__file__).resolve().parent
s=(p/'ParserExpression.cpp').read_text();h=(p/'Parser.h').read_text();t=(p/'ParserTemplate.cpp').read_text()
h=h.replace('enum class Kind { Stat, Copy, Move };','enum class Kind { Stat, Copy, Move, FileMethod };')
h=h.replace('        Kind kind=Kind::Stat;','        Kind kind=Kind::Stat;\n        std::string receiver;')
start=s.index('    const auto method=source.substr(0,open);',s.index('Parser::prepare_filesystem_operation'))
end=s.index('    for (std::size_t i=0;i<args.size();++i)',start)
s=s[:start]+'''    auto method=source.substr(0,open);std::string receiver;
    const auto dot=method.find('.');
    if(dot!=std::string::npos){receiver=method.substr(0,dot);method=method.substr(dot+1);
        if(!valid_binding_identifier(receiver))return {};
        static const std::unordered_set<std::string> methods={"open","close","save","revert","modified","tell","seek","eof","read","read_all","read_bytes","read_all_bytes","read_line","read_val","write","write_line","write_val","flush","replace","replace_once","insert","insert_before","insert_after","prepend","append","path","exists","cat","copy","move","remove"};
        if(!methods.count(method))return {};
    }else if(method!="move"&&method!="mv"&&method!="copy"&&method!="cp"&&method!="stat")return {};
    std::size_t close=0;
    if(!find_balanced(source,open,'(',')',close)||close!=source.size()-1)return {};
    auto identity=nift::detail::SourceView::identity({},source);
    bool ok=false;std::vector<bool> quoted;
    auto args=parse_parameters(nift::detail::SourceText(source,identity).substr(open+1,source.size()-open-2),ok,&quoted);
    if(!ok|| (receiver.empty()&&(method=="stat"?args.size()!=1:args.size()<2)))return {};
    auto recipe=std::make_shared<PreparedFilesystemOperation>();recipe->method=method;recipe->receiver=receiver;
    recipe->kind=!receiver.empty()?PreparedFilesystemOperation::Kind::FileMethod:method=="stat"?PreparedFilesystemOperation::Kind::Stat:(method=="move"||method=="mv")?PreparedFilesystemOperation::Kind::Move:PreparedFilesystemOperation::Kind::Copy;
'''+s[end:]
old='''        if (recipe && depth==0) {
            auto operand='''
new='''        bool file_recipe_eligible=false;
        if(recipe&&depth==0&&recipe->kind==PreparedFilesystemOperation::Kind::FileMethod){
            // Inspect the same logical slot without sync or evaluation. A
            // non-FileValue follows the untouched canonical method route.
            for(auto scope=variable_scopes_.rbegin();scope!=variable_scopes_.rend();++scope){
                auto found=scope->find(recipe->receiver);if(found==scope->end())continue;
                auto& binding=found->second;std::shared_ptr<nift::RuntimeValue> owner;
                if(binding.is_location_ref()){
                    if(binding.ref_root_slot){auto root=*binding.ref_root_slot;auto* value=binding.resolve_location();if(root&&value)owner=std::shared_ptr<nift::RuntimeValue>(std::move(root),value);}
                }else owner=binding.slot?*binding.slot:binding.value;
                file_recipe_eligible=owner&&owner->is_string()&&owner->string.rfind("\\x1fnift:file:",0)==0;break;
            }
        }
        if (recipe && depth==0 && (recipe->kind!=PreparedFilesystemOperation::Kind::FileMethod||file_recipe_eligible)) {
            auto operand='''
assert old in s;s=s.replace(old,new,1)
old='''            return execute_filesystem_operation(recipe->kind,recipe->operands.size(),operand,out,error);'''
new='''            if(recipe->kind==PreparedFilesystemOperation::Kind::FileMethod){
                nift::RuntimeValue base;
                if(!eval(nift::detail::SourceText(recipe->receiver,text.view.slice(0,recipe->receiver.size())),base,depth+1))return false;
                bool handled=false;
                return execute_file_method(base,recipe->method,recipe->operands.size(),operand,[&](const std::string& token,nift::RuntimeValue& value){return eval(token,value,depth+1);},out,error,handled);
            }
            return execute_filesystem_operation(recipe->kind,recipe->operands.size(),operand,out,error);'''
assert old in s;s=s.replace(old,new,1)
needle='        auto ast_context=[&]() -> nift::ast::Context&'
helper='''        auto file_method_recipe=[&](const std::string& x,nift::RuntimeValue& out,std::string& error,nift::detail::SourceView view,bool& used){
            used=false;
            if(x.find('.')==std::string::npos||x.empty()||x.back()!=')')return false;
            auto found=prepared_filesystem_recipes.find(x);
            if(found==prepared_filesystem_recipes.end())found=prepared_filesystem_recipes.emplace(x,prepare_filesystem_operation(x)).first;
            if(!found->second||found->second->kind!=PreparedFilesystemOperation::Kind::FileMethod)return false;
            used=true;return evaluate_filesystem_expression(x,out,error,std::move(view),found->second.get());
        };
'''
assert needle in t;t=t.replace(needle,helper+needle,1)
old='c.legacy=[&](const std::string& x,nift::RuntimeValue& out,std::string& e){return evaluate_expression(x,out,e,prepared_view->slice(prepared_statement_span.begin,x.size()));};'
new='c.legacy=[&](const std::string& x,nift::RuntimeValue& out,std::string& e){auto view=prepared_view->slice(prepared_statement_span.begin,x.size());bool used=false;const bool result=file_method_recipe(x,out,e,view,used);if(used)return result;return evaluate_expression(x,out,e,view);};'
assert old in t;t=t.replace(old,new,1)
needle='c.legacy_with_span=[&](const std::string& x,nift::ast::SourceSpan span,nift::RuntimeValue& out,std::string& e){'
assert needle in t;t=t.replace(needle,needle+'\n    bool used=false;const bool result=file_method_recipe(x,out,e,prepared_view->slice(span.begin,span.end-span.begin),used);if(used)return result;',1)
for name,data in [('Parser.h',h),('ParserExpression.cpp',s),('ParserTemplate.cpp',t)]:(p/name).write_text(data)
print('Prepared canonical FileValue method recipes; all dynamic operands remain lazy')
