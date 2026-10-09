from pathlib import Path
p=Path('.build/cp410-implementation/strings');f=p/'ParserExpression.cpp';s=f.read_text();needle='''    auto parsed=large?nift::ast::parse_literal_payload_expression(source):nift::ast::parse_expression(source);'''
pre='''    if(large){
        // Bound nodes before the AST can duplicate a large literal into every
        // chained call's source text or recursively fold/destruct that chain.
        std::size_t tokens=0,nesting=0;
        for(std::size_t i=0;i<source.size();){
            const unsigned char c=(unsigned char)source[i];
            if(std::isspace(c)){++i;continue;}
            if(++tokens>64)return {};
            if(c=='\\''||c=='"'){const char quote=source[i++];while(i<source.size()&&source[i]!=quote)++i;if(i==source.size())return {};++i;continue;}
            if(std::isalpha(c)||c=='_'){++i;while(i<source.size()&&(std::isalnum((unsigned char)source[i])||source[i]=='_'))++i;continue;}
            if(c=='('||c=='['||c=='{'){if(++nesting>16)return {};}
            else if((c==')'||c==']'||c=='}')&&nesting)--nesting;
            ++i;
        }
    }
'''
assert needle in s
# The separate AST entry itself enforces this before constructing nodes.
f=p/'Ast.cpp';ast=f.read_text();guard=pre.replace('    if(large){','    if(literal_payload){').replace('return {};','return{{},"literal-payload expression exceeds structural limits",false};')
guard=guard.replace("while(i<source.size()&&source[i]!=quote)++i;", "while(i<source.size()&&source[i]!=quote){if(source[i]=='\\\\'&&i+1<source.size())i+=2;else ++i;}")
needle='static ParseResult parse_bounded_expression(const std::string& source,bool literal_payload){\n'
assert needle in ast;ast=ast.replace(needle,needle+guard,1);f.write_text(ast)
f=p/'ParserExpression.cpp';s=s.replace('large_string_plans_.clear();large_string_plan_bytes_=0;', 'large_string_plans_.clear();large_string_plan_bytes_=0;found=large_string_plans_.end();',1);f.write_text(s)
f=p/'cache-guard.cpp';guard=f.read_text();needle='        assert(!Parser::prepare_pure_string_plan(chain,true));'
if 'assert(!nift::ast::parse_literal_payload_expression(chain).supported);' not in guard:guard=guard.replace(needle,needle+'\n        assert(!nift::ast::parse_literal_payload_expression(chain).supported);')
f.write_text(guard)
print('Large literal AST token/nesting preflight added before construction')
