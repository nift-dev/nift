from pathlib import Path
p=Path('.build/cp410-implementation/filesystem')
f=p/'Parser.h';s=f.read_text();s=s.replace('        bool quoted=false;','        bool quoted=false;\n        std::shared_ptr<const nift::ast::Expr> pure_plan;',1);needle='    struct PreparedFilesystemOperation {';s=s.replace(needle,'''    static std::shared_ptr<const nift::ast::Expr> prepare_pure_string_plan(const std::string& source);
    bool evaluate_pure_string_plan(const nift::ast::Expr& expression, nift::RuntimeValue& out);
'''+needle,1);f.write_text(s)
f=p/'ParserExpression.cpp';s=f.read_text();pos=s.index('std::shared_ptr<const Parser::PreparedFilesystemOperation>');code='''std::shared_ptr<const nift::ast::Expr> Parser::prepare_pure_string_plan(const std::string& source) {
    // Conservative canonical string-only grammar, with bounded structure.
    // Escapes and effectful/unknown nodes retain the full legacy evaluator.
    if(source.size()>4096||source.find('\\\\')!=std::string::npos)return {};
    auto parsed=nift::ast::parse_expression(source);if(!parsed.supported||!parsed.expr)return {};
    std::size_t nodes=0;
    std::function<int(const nift::ast::Expr&,unsigned)> type;
    type=[&](const nift::ast::Expr& e,unsigned depth)->int{
        if(++nodes>64||depth>16)return 0;
        using nift::ast::Kind;
        if(e.kind==Kind::Literal)return e.literal.is_string()&&e.literal.string.find('\\x1f')==std::string::npos?1:0;
        if(e.kind==Kind::Binding)return 1;
        if(e.kind==Kind::Binary&&e.op=="+"&&e.left&&e.right)
            return type(*e.left,depth+1)==1&&type(*e.right,depth+1)==1?1:0;
        if(e.kind!=Kind::Call||!e.left||e.left->kind!=Kind::Member||!e.left->left)return 0;
        const auto& method=e.left->name;
        if(e.text.find("."+method+"(")==std::string::npos)return 0;
        if(method=="split"&&e.items.size()==1&&e.items[0]->kind==Kind::Literal&&e.items[0]->literal.is_string()&&!e.items[0]->literal.string.empty())
            return type(*e.left->left,depth+1)==1?2:0;
        if((method=="first"||method=="last")&&e.items.empty())return type(*e.left->left,depth+1)==2?1:0;
        return 0;
    };
    if(type(*parsed.expr,0)==0)return {};
    return std::shared_ptr<const nift::ast::Expr>(std::move(parsed.expr));
}

bool Parser::evaluate_pure_string_plan(const nift::ast::Expr& expression,nift::RuntimeValue& out) {
    std::unordered_map<std::string,std::pair<std::shared_ptr<const nift::RuntimeValue>,VariableBinding*>> inputs;
    std::function<bool(const nift::ast::Expr&)> eligible;
    eligible=[&](const nift::ast::Expr& e){
        if(e.kind==nift::ast::Kind::Binding){
            if(inputs.count(e.name))return true;
            for(auto scope=variable_scopes_.rbegin();scope!=variable_scopes_.rend();++scope){
                auto found=scope->find(e.name);if(found==scope->end())continue;
                auto& binding=found->second;
                std::shared_ptr<nift::RuntimeValue> value;
                if(binding.is_location_ref()){
                    auto root=*binding.ref_root_slot;auto* member=binding.resolve_location();
                    if(root&&member)value=std::shared_ptr<nift::RuntimeValue>(std::move(root),member);
                }else value=binding.slot?*binding.slot:binding.value;
                if(!value||!value->is_string()||value->string.find('\\x1f')!=std::string::npos)return false;
                inputs.emplace(e.name,std::make_pair(std::move(value),&binding));return true;
            }return false;
        }
        if(e.kind==nift::ast::Kind::Call)return eligible(*e.left->left);
        return (!e.left||eligible(*e.left))&&(!e.right||eligible(*e.right));
    };
    // Unsupported is decided before evaluating any operand. This tree cannot
    // call user code or change a slot while these operation-local owners live.
    if(!eligible(expression))return false;
    std::function<nift::RuntimeValue(const nift::ast::Expr&)> execute;
    execute=[&](const nift::ast::Expr& e)->nift::RuntimeValue{
        if(e.kind==nift::ast::Kind::Literal)return e.literal;
        if(e.kind==nift::ast::Kind::Binding){auto* binding=inputs.at(e.name).second;binding->sync();return *binding->value;}
        if(e.kind==nift::ast::Kind::Binary){auto left=execute(*e.left);auto right=execute(*e.right);return nift::RuntimeValue(left.string+right.string);}
        const auto& receiver=*e.left->left;
        auto value=execute(receiver);
        if(e.left->name=="split"){
            nift::RuntimeValue result=nift::RuntimeValue::make_array();const auto& delimiter=e.items[0]->literal.string;
            std::size_t pos=0;while(true){const auto at=value.string.find(delimiter,pos);if(at==std::string::npos){result.array.emplace_back(value.string.substr(pos));break;}result.array.emplace_back(value.string.substr(pos,at-pos));pos=at+delimiter.size();}return result;
        }
        return e.left->name=="first"?value.array.front():value.array.back();
    };
    out=execute(expression);return true;
}

''';s=s[:pos]+code+s[pos:];s=s.replace('recipe->operands.push_back({args[i],range.first,range.second,quoted[i]});','recipe->operands.push_back({args[i],range.first,range.second,quoted[i],quoted[i]?nullptr:prepare_pure_string_plan(args[i])});',1)
s=s.replace('''                return eval(nift::detail::SourceText(arg.text,text.view.slice(arg.begin,arg.length)),value,depth+1);''','''                if(arg.pure_plan&&evaluate_pure_string_plan(*arg.pure_plan,value))return true;
                return eval(nift::detail::SourceText(arg.text,text.view.slice(arg.begin,arg.length)),value,depth+1);''',1);f.write_text(s)
f=p/'ParserTemplate.cpp';s=f.read_text();s=s.replace('''        std::unordered_map<std::string,std::shared_ptr<const PreparedFilesystemOperation>> prepared_filesystem_recipes;''','''        std::unordered_map<std::string,std::shared_ptr<const PreparedFilesystemOperation>> prepared_filesystem_recipes;
        std::unordered_map<std::string,std::shared_ptr<const nift::ast::Expr>> prepared_pure_string_plans;''',1)
needle='''    return evaluate_expression(x,out,e,prepared_view->slice(span.begin,span.end-span.begin));};''';assert s.count(needle)==1;s=s.replace(needle,'''    if(x.find(".split(")==std::string::npos&&x.find(".first(")==std::string::npos&&x.find(".last(")==std::string::npos)
        return evaluate_expression(x,out,e,prepared_view->slice(span.begin,span.end-span.begin));
    auto pure=prepared_pure_string_plans.find(x);
    if(pure==prepared_pure_string_plans.end())pure=prepared_pure_string_plans.emplace(x,prepare_pure_string_plan(x)).first;
    if(pure->second&&evaluate_pure_string_plan(*pure->second,out)){last_expression_mutation_=false;return true;}
    return evaluate_expression(x,out,e,prepared_view->slice(span.begin,span.end-span.begin));};''',1);f.write_text(s)
print('Prepared bounded pure canonical string plans without changing effectful fallback')
