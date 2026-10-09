from pathlib import Path
p=Path(__file__).resolve().parent
s=(p/'ParserExpression.cpp').read_text();h=(p/'Parser.h').read_text();t=(p/'ParserTemplate.cpp').read_text()
h=h.replace('Stat, Copy, Move, FileMethod','Stat, Copy, Move, FileMethod, FileFactory')
s=s.replace('const std::string operation=kind==PreparedFilesystemOperation::Kind::Stat?"stat":moving?"move":"copy";', 'const std::string operation=kind==PreparedFilesystemOperation::Kind::FileFactory?"file":kind==PreparedFilesystemOperation::Kind::Stat?"stat":moving?"move":"copy";',1)
needle='            if(kind==PreparedFilesystemOperation::Kind::Stat) {'
insert='''            if(kind==PreparedFilesystemOperation::Kind::FileFactory){
                nift::RuntimeValue path_value;if(!evaluate_operand(0,path_value)||!path_value.is_string()){error="file: expected string path";return false;}
                auto path=resolve_path(path_value.string);if(!validate(path))return false;
                return make_file_value(std::move(path),out,error);
            }
'''
assert needle in s;s=s.replace(needle,insert+needle,1)
s=s.replace('method!="cp"&&method!="stat")return {};','method!="cp"&&method!="stat"&&method!="file")return {};',1)
s=s.replace('(method=="stat"?args.size()!=1:args.size()<2)','((method=="stat"||method=="file")?args.size()!=1:args.size()<2)',1)
s=s.replace('recipe->kind=!receiver.empty()?PreparedFilesystemOperation::Kind::FileMethod:method=="stat"?', 'recipe->kind=!receiver.empty()?PreparedFilesystemOperation::Kind::FileMethod:method=="file"?PreparedFilesystemOperation::Kind::FileFactory:method=="stat"?',1)
old='if(call_args("file",args,q)){fs::path p;if(args.size()!=1||!checked_path("file",args,q,0,p))return false;return make_file_value(std::move(p),out,error);}'
new='if(call_args("file",args,q)){if(args.size()!=1)return false;return execute_filesystem_operation(PreparedFilesystemOperation::Kind::FileFactory,args.size(),[&](std::size_t i,nift::RuntimeValue& value){return arg_value(args,q,i,value);},out,error);}'
assert old in s;s=s.replace(old,new,1)
old='if(x.rfind("move(",0)==0||x.rfind("mv(",0)==0||x.rfind("copy(",0)==0||x.rfind("cp(",0)==0||x.rfind("stat(",0)==0)'
assert old in t;t=t.replace(old,old[:-1]+'||x.rfind("file(",0)==0)',1)
for name,data in [('Parser.h',h),('ParserExpression.cpp',s),('ParserTemplate.cpp',t)]:(p/name).write_text(data)
print('File factories share canonical path policy and fresh instance creation')
