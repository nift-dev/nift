from pathlib import Path
p=Path('.build/cp410-implementation/strings');f=p/'ParserExpression.cpp';s=f.read_text()
a=s.index('    // This runs in canonical evaluation only.');b=s.index('\n        if (recipe && depth==0)',a)
block=s[a:b];lookup=block[block.index('        auto found='):block.index('        if(found!=large_string_plans_')]
# One syntax-cache lookup for both RHS expression and plain assignment.
lookup=lookup.replace('text','source').replace('e.source','e.text')
helper='''    auto large_plan=[&](const std::string& source)->std::shared_ptr<const nift::ast::Expr>{
        if(source.size()<=8192||source.size()>131072||source.find(".replace(")==std::string::npos)return {};
'''+lookup+'''        return found==large_string_plans_.end()?nullptr:found->second;
    };
'''
pos=s.index('    std::function<bool(const nift::detail::SourceText&, nift::RuntimeValue&, int)> eval;');s=s[:pos]+helper+s[pos:]
a=s.index('    // This runs in canonical evaluation only.');b=s.index('\n        if (recipe && depth==0)',a);s=s[:a]+'''        if(auto plan=large_plan(text);plan&&evaluate_pure_string_plan(*plan,out))return true;
'''+s[b:]
# Extract the existing canonical plain-assignment body verbatim, then share it.
a=s.index('            if (!valid_binding_identifier(name)) { error = "assignment requires an identifier before');b=s.index('\n        }\n\n        // Split at',a);body=s[a:b]
s=s[:a]+'''            return assign_plain_canonical(name,p);
'''+s[b:]
pos=s.index('        auto structural_equal =')
assign='''        auto assign_plain_canonical=[&](const std::string& name,std::size_t p)->bool {
'''+body+'''\n        };
        // Only a canonical plain assignment with a certified pure string RHS
        // skips dispatch scans. Binding validation/evaluation/mutation use the
        // very same body as the compatibility branch below.
        if(text.size()>8192&&text.size()<=131072){
            std::size_t end=0;while(end<text.size()&&(std::isalnum((unsigned char)text[end])||text[end]=='_'))++end;
            auto at=end;while(at<text.size()&&std::isspace((unsigned char)text[at]))++at;
            if(end&&at+1<text.size()&&text[at]=='='&&text[at+1]!='='&&text[at+1]!='>'){
                const std::string name=text.substr(0,end);
                if(valid_binding_identifier(name)&&large_plan(text.substr(at+1)))return assign_plain_canonical(name,at);
            }
        }
'''
s=s[:pos]+assign+s[pos:];f.write_text(s)
print('Shared exact canonical plain assignment; pure large-RHS dispatch classification')
