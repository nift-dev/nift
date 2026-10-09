from pathlib import Path
import re
p=Path('.build/cp410-implementation/strings');base=Path('.build/cp410-implementation/filesystem')
for name in ['Parser.h','ParserExpression.cpp','ParserTemplate.cpp','ParserHelpers.h']:(p/name).write_text((base/name).read_text())
for name in ['Ast.cpp','Ast.h']:(p/name).write_text((Path('src')/name).read_text())
f=p/'Ast.h';s=f.read_text().replace('ParseResult parse_expression(const std::string& source);','ParseResult parse_expression(const std::string& source);\n// Separate canonical pure-plan entry; ordinary preparation limits stay fixed.\nParseResult parse_literal_payload_expression(const std::string& source);');f.write_text(s)
f=p/'Ast.cpp';s=f.read_text().replace('ParseResult parse_expression(const std::string& source){','static ParseResult parse_bounded_expression(const std::string& source,bool literal_payload){',1)
s=s.replace('{std::size_t depth=0,maxdepth=0;bool q=false;', '{std::size_t depth=0,maxdepth=0,structural=0;bool q=false;',1)
s=s.replace("if(c=='\\''||c=='\"'){q=true;qc=c;continue;}if(c=='('","++structural;if(c=='\\''||c=='\"'){q=true;qc=c;continue;}if(c=='('",1)
s=s.replace('if(source.size()>8192||maxdepth>256)', 'if((literal_payload?(source.size()>131072||structural>8192):source.size()>8192)||maxdepth>256)',1)
pos=s.index('TemplateParseResult parse_template(');s=s[:pos]+'''ParseResult parse_expression(const std::string& source){return parse_bounded_expression(source,false);}
ParseResult parse_literal_payload_expression(const std::string& source){return parse_bounded_expression(source,true);}
'''+s[pos:];f.write_text(s)
f=p/'Parser.h';s=f.read_text().replace('prepare_pure_string_plan(const std::string& source);','prepare_pure_string_plan(const std::string& source, bool large=false);')
needle='    std::vector<std::unordered_map<std::string, VariableBinding>> variable_scopes_;'
s=s.replace(needle,needle+'''
    // Bounded syntax-only cache. No values, binding descriptors or source views.
    std::unordered_map<std::string,std::shared_ptr<const nift::ast::Expr>> large_string_plans_;
    std::size_t large_string_plan_bytes_=0;
''');f.write_text(s)
f=p/'ParserExpression.cpp';s=f.read_text().replace('prepare_pure_string_plan(const std::string& source) {','prepare_pure_string_plan(const std::string& source,bool large) {',1).replace('if(source.size()>4096||source.find', 'if(source.size()>(large?131072:4096)||source.find',1).replace('auto parsed=nift::ast::parse_expression(source);if(!parsed.supported', 'auto parsed=large?nift::ast::parse_literal_payload_expression(source):nift::ast::parse_expression(source);if(!parsed.supported',1)
needle='''        if((method=="first"||method=="last")&&e.items.empty())return type(*e.left->left,depth+1)==2?1:0;'''
s=s.replace('''        const auto& method=e.left->name;
        if(e.text.find''','''        const auto& method=e.left->name;
        // AST argument construction omits empty comma segments; canonical
        // parsing retains them. Validate exact canonical argument shape.
        const auto open=e.left->span.end-e.span.begin;
        auto at=open;while(at<e.text.size()&&std::isspace((unsigned char)e.text[at]))++at;
        if(at>=e.text.size()||e.text[at]!='('||e.text.back()!=')')return 0;
        bool args_ok=false;std::vector<bool> quotes;
        auto args=parse_parameters(nift::detail::SourceText(e.text,nift::detail::SourceView::identity({},e.text)).substr(at+1,e.text.size()-at-2),args_ok,&quotes);
        if(!args_ok||args.size()!=e.items.size())return 0;
        if(e.text.find''',1)
s=s.replace(needle,needle+'''
        if(large&&(method=="to_upper"||method=="to_lower"||method=="trim"||method=="trim_start"||method=="trim_end")&&e.items.empty())return type(*e.left->left,depth+1)==1?1:0;
        if(large&&method=="replace"&&e.items.size()==2&&e.items[0]->kind==Kind::Literal&&e.items[1]->kind==Kind::Literal&&e.items[0]->literal.is_string()&&e.items[1]->literal.is_string()&&!e.items[0]->literal.string.empty()&&e.items[0]->literal.string.find('\\x1f')==std::string::npos&&e.items[1]->literal.string.find('\\x1f')==std::string::npos)return type(*e.left->left,depth+1)==1?1:0;
''')
needle='''        return e.left->name=="first"?value.array.front():value.array.back();'''
s=s.replace(needle,'''        const auto& method=e.left->name;
        if(method=="replace")return nift::RuntimeValue(nift::detail::replace_string_linear(value.string,e.items[0]->literal.string,e.items[1]->literal.string));
        if(method=="to_upper"||method=="to_lower"){for(char& c:value.string)c=(char)(method=="to_lower"?std::tolower((unsigned char)c):std::toupper((unsigned char)c));return value;}
        if(method=="trim"||method=="trim_start"||method=="trim_end"){std::size_t a=0,b=value.string.size();if(method!="trim_end")while(a<b&&std::isspace((unsigned char)value.string[a]))++a;if(method!="trim_start")while(b>a&&std::isspace((unsigned char)value.string[b-1]))--b;return nift::RuntimeValue(value.string.substr(a,b-a));}
'''+needle)
# Reuse the retained builder, moving it to a single shared private helper.
a=s.index('if(a.string.empty()){error="replace: old string must not be empty";return false;}std::string r;');b=s.index('out=nift::RuntimeValue(std::move(r));return true;',a)+len('out=nift::RuntimeValue(std::move(r));return true;')
old=s[a:b];builder=old[old.index('std::string r;'):old.index('out=nift::RuntimeValue')].replace('a.string','pattern').replace('b.string','replacement')
builder=re.sub(r'\bstr\b','source',builder)
s=s[:a]+'''if(a.string.empty()){error="replace: old string must not be empty";return false;}out=nift::RuntimeValue(nift::detail::replace_string_linear(str,a.string,b.string));return true;'''+s[b:]
helper='''namespace nift { namespace detail {
static std::string replace_string_linear(const std::string& source,const std::string& pattern,const std::string& replacement){
'''+builder+'''return r;
}
}}
'''
pos=s.index('bool Parser::execute_filesystem_operation(');s=s[:pos]+helper+s[pos:]
needle='''    last_expression_mutation_ = false;
    auto resolve_direct'''
cache='''    last_expression_mutation_ = false;
    // This runs in canonical evaluation only. It cannot change whether a body
    // prepares, so speculative factory evaluation counts remain unchanged.
    if(expression.size()>8192&&expression.size()<=131072&&expression.find(".replace(")!=std::string::npos){
        auto found=large_string_plans_.find(expression);
        if(found==large_string_plans_.end()){
            auto plan=prepare_pure_string_plan(expression,true);
            std::size_t bytes=expression.capacity()+sizeof(decltype(large_string_plans_)::value_type)+4*sizeof(void*);
            std::function<void(const nift::ast::Expr&)> account=[&](const nift::ast::Expr& e){bytes+=sizeof(e)+e.text.capacity()+e.name.capacity()+e.op.capacity()+e.literal.string.capacity()+e.params.capacity()*sizeof(std::string)+e.items.capacity()*sizeof(std::unique_ptr<nift::ast::Expr>);for(const auto& x:e.params)bytes+=x.capacity();if(e.left)account(*e.left);if(e.right)account(*e.right);for(const auto& x:e.items)account(*x);};
            if(plan)account(*plan);
            if(large_string_plans_.size()>=16||large_string_plan_bytes_+bytes>4*1024*1024){large_string_plans_.clear();large_string_plan_bytes_=0;}
            if(bytes<=4*1024*1024){large_string_plan_bytes_+=bytes;found=large_string_plans_.emplace(expression,std::move(plan)).first;}
        }
        if(found!=large_string_plans_.end()&&found->second&&evaluate_pure_string_plan(*found->second,value))return true;
    }
    auto resolve_direct'''
assert needle in s
block=cache[cache.index('    // This runs'):cache.rindex('    auto resolve_direct')]
block=re.sub(r'\bexpression\b','text',block)
block=re.sub(r'\bvalue\b','out',block)
needle='        if(depth>96){error="expression nesting exceeds parser limit";return false;}\n'
assert needle in s;s=s.replace(needle,needle+block,1);f.write_text(s)
print('Private bounded canonical large-string prototype; ordinary preparation unchanged')
