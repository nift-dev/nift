from pathlib import Path
p=Path(__file__).resolve().parent
s=(p/'ParserExpression.cpp').read_text();h=(p/'Parser.h').read_text()
start=s.index('                    if(base.is_string() && base.string.rfind("\\x1fnift:file:",0)==0) {')
end=s.index('                    if(base.is_string() && base.string.rfind("\\x1fnift:",0)!=0)',start)
block=s[start:end];inner=block[block.index('\n')+1:block.rfind('                    }')]
inner=inner.replace('args.size()','operand_count').replace('args.empty()','(operand_count==0)').replace('eval(tok,v,depth+1)','evaluate_token(tok,v)')
method='''bool Parser::execute_file_method(const nift::RuntimeValue& base,const std::string& method,std::size_t operand_count,
    const std::function<bool(std::size_t,nift::RuntimeValue&)>& eval_arg,
    const std::function<bool(const std::string&,nift::RuntimeValue&)>& evaluate_token,
    nift::RuntimeValue& out,std::string& error,bool& handled) {
    handled=true;
    auto no_args=[&](){if(operand_count){error=method+": expected no arguments";return false;}return true;};
'''+inner+'''    handled=false;return false;
}

'''
s=s[:start]+'''                    if(base.is_string() && base.string.rfind("\\x1fnift:file:",0)==0) {
                        bool handled=false;
                        bool result=execute_file_method(base,method,args.size(),eval_arg,[&](const std::string& token,nift::RuntimeValue& value){return eval(token,value,depth+1);},out,error,handled);
                        if(handled)return result;
                    }
'''+s[end:]
pos=s.index('bool Parser::execute_filesystem_operation(');s=s[:pos]+method+s[pos:]
needle='    bool execute_filesystem_operation('
h=h.replace(needle,'''    bool execute_file_method(const nift::RuntimeValue& base,const std::string& method,std::size_t operand_count,
        const std::function<bool(std::size_t,nift::RuntimeValue&)>& operand,
        const std::function<bool(const std::string&,nift::RuntimeValue&)>& evaluate_token,
        nift::RuntimeValue& out,std::string& error,bool& handled);
'''+needle)
(p/'ParserExpression.cpp').write_text(s);(p/'Parser.h').write_text(h)
print('Extracted one canonical FileValue backend; prepared native write remains separate')
