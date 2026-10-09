from pathlib import Path
p=Path('.build/cp410-implementation/filesystem');f=p/'Parser.h';s=f.read_text();s=s.replace('''    struct PreparedFilesystemOperation {
        std::string method;''','''    struct PreparedFilesystemOperation {
        enum class Kind { Stat, Copy, Move };
        Kind kind=Kind::Stat;
        std::string method;''',1)
needle='''    std::shared_ptr<const PreparedFilesystemOperation> prepare_filesystem_operation''';pos=s.index(needle);s=s[:pos]+'''    bool execute_filesystem_operation(PreparedFilesystemOperation::Kind kind, std::size_t operand_count,
        const std::function<bool(std::size_t,nift::RuntimeValue&)>& operand,
        nift::RuntimeValue& out, std::string& error);
'''+s[pos:];f.write_text(s)
f=p/'ParserExpression.cpp';s=f.read_text();s=s.replace('''recipe->method=method;''','''recipe->method=method;recipe->kind=method=="stat"?PreparedFilesystemOperation::Kind::Stat:(method=="move"||method=="mv")?PreparedFilesystemOperation::Kind::Move:PreparedFilesystemOperation::Kind::Copy;''',1)
a=s.index('''        if (recipe && depth==0) {''');b=s.index('''        auto find_binding =''',a);block=s[a:b]
backend_begin=block.index('''            auto resolve_path=''');backend=block[backend_begin:];backend=backend[:backend.rfind('''        }''')];backend=backend.replace('if(name=="stat")','if(kind==PreparedFilesystemOperation::Kind::Stat)').replace('recipe->operands.size()','operand_count')
backend='''bool Parser::execute_filesystem_operation(PreparedFilesystemOperation::Kind kind,std::size_t operand_count,
    const std::function<bool(std::size_t,nift::RuntimeValue&)>& operand,nift::RuntimeValue& out,std::string& error) {
    const bool moving=kind==PreparedFilesystemOperation::Kind::Move;
    const std::string operation=kind==PreparedFilesystemOperation::Kind::Stat?"stat":moving?"move":"copy";
'''+backend+'''}

'''
fast=block[:backend_begin];fast=fast.replace('''            const auto& name=recipe->method;
            const bool moving=name=="move"||name=="mv";
            const std::string operation=name=="stat"?"stat":moving?"move":"copy";
''','');fast+='''            return execute_filesystem_operation(recipe->kind,recipe->operands.size(),operand,out,error);
        }
''';s=s[:a]+fast+s[b:];pos=s.index('std::shared_ptr<const nift::ast::Expr> Parser::prepare_pure_string_plan');s=s[:pos]+backend+s[pos:]
# Both canonical legacy dispatch and prepared recipes now use this one backend.
start=s.index('''            if(call_args("stat",args,q)){''');end=s.index('\n',start);s=s[:start]+'''            if(call_args("stat",args,q)){if(args.size()!=1)return false;return execute_filesystem_operation(PreparedFilesystemOperation::Kind::Stat,args.size(),[&](std::size_t i,nift::RuntimeValue& value){return arg_value(args,q,i,value);},out,error);}'''+s[end:]
start=s.index('''            if(call_args("copy",args,q)||call_args("cp",args,q)||call_args("move",args,q)||call_args("mv",args,q)){''');end=s.index('\n',start);old=s[start:end];marker='''else{std::vector<fs::path> sources;''';a=old.index(marker);old=old[:a]+'''else{return execute_filesystem_operation(mv?PreparedFilesystemOperation::Kind::Move:PreparedFilesystemOperation::Kind::Copy,args.size(),[&](std::size_t i,nift::RuntimeValue& value){return arg_value(args,q,i,value);},out,error);}}''';s=s[:start]+old+s[end:];f.write_text(s)
print('Consolidated canonical and prepared stat/move/copy execution into one backend')
