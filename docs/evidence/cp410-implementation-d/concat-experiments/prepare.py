from pathlib import Path
p=Path(__file__).resolve().parent
h=(p/'ParserHelpers.h').read_text();cpp=(p/'ParserHelpers.cpp').read_text();s=(p/'ParserExpression.cpp').read_text();ph=(p/'Parser.h').read_text();t=(p/'ParserTemplate.cpp').read_text()
h=h.replace('#include <memory>','#include <memory>\n#include <iosfwd>')
h=h.replace('namespace nift::detail {','namespace nift::detail {\n\nNIFT_PARSER_HELPER_HIDDEN std::string read_stream_owned(std::istream& input);',1)
helper='''namespace nift::detail {
std::string read_stream_owned(std::istream& input){
    // Keep the same streambuf transfer and stream error handling as the old
    // ostringstream path. Append directly to the returned owner, avoiding
    // str()'s full copy. The input filebuf retains its bounded native buffer.
    class OwnedBytes final:public std::streambuf {
    public:
        std::string bytes;
    protected:
        std::streamsize xsputn(const char* data,std::streamsize size) override {
            bytes.append(data,static_cast<std::size_t>(size));return size;
        }
        int_type overflow(int_type value) override {
            if(!traits_type::eq_int_type(value,traits_type::eof()))bytes.push_back(traits_type::to_char_type(value));
            return traits_type::not_eof(value);
        }
    } buffer;
    std::ostream output(&buffer);output<<input.rdbuf();return std::move(buffer.bytes);
}
}

'''
pos=cpp.index('namespace ');cpp=cpp[:pos]+helper+cpp[pos:]
s=s.replace('std::ostringstream ss;ss<<in.rdbuf();data=ss.str();','data=nift::detail::read_stream_owned(in);',1)
old='if(call_args("open",args,q)){fs::path p;if(args.size()!=1||!checked_path("open",args,q,0,p))return false;std::error_code ec;if(fs::is_directory(p,ec)){error="open: path is a directory";return false;}std::ifstream f(p,std::ios::binary);if(!f)return fail_recoverable(nift::detail::DiagnosticCode::IoOpenFailed,"open: cannot open path",error);std::ostringstream ss;ss<<f.rdbuf();out=nift::RuntimeValue(ss.str());return true;}'
new='if(call_args("open",args,q)){if(args.size()!=1)return false;return execute_filesystem_operation(PreparedFilesystemOperation::Kind::ReadText,args.size(),[&](std::size_t i,nift::RuntimeValue& value){return arg_value(args,q,i,value);},out,error);}'
assert old in s;s=s.replace(old,new,1)
ph=ph.replace('FileMethod, FileFactory','FileMethod, FileFactory, ReadText')
s=s.replace('const std::string operation=kind==PreparedFilesystemOperation::Kind::FileFactory?"file":','const std::string operation=kind==PreparedFilesystemOperation::Kind::ReadText?"open":kind==PreparedFilesystemOperation::Kind::FileFactory?"file":',1)
needle='            if(kind==PreparedFilesystemOperation::Kind::FileFactory){'
read='''            if(kind==PreparedFilesystemOperation::Kind::ReadText){
                nift::RuntimeValue value;if(!evaluate_operand(0,value)||!value.is_string()){error="open: expected string path";return false;}
                auto path=resolve_path(value.string);if(!validate(path))return false;std::error_code ec;
                if(fs::is_directory(path,ec)){error="open: path is a directory";return false;}
                std::ifstream input(path,std::ios::binary);if(!input)return fail_recoverable(nift::detail::DiagnosticCode::IoOpenFailed,"open: cannot open path",error);
                out=nift::RuntimeValue(nift::detail::read_stream_owned(input));return true;
            }
'''
assert needle in s;s=s.replace(needle,read+needle,1)
s=s.replace('method!="stat"&&method!="file")','method!="stat"&&method!="file"&&method!="open")',1)
s=s.replace('(method=="stat"||method=="file")','(method=="stat"||method=="file"||method=="open")',1)
s=s.replace('method=="file"?PreparedFilesystemOperation::Kind::FileFactory:', 'method=="open"?PreparedFilesystemOperation::Kind::ReadText:method=="file"?PreparedFilesystemOperation::Kind::FileFactory:',1)
s=s.replace('bool candidate=text.rfind("file(",0)==0;','bool candidate=text.rfind("file(",0)==0||text.rfind("open(",0)==0;',1)
s=s.replace('operation_recipe_owner->kind==PreparedFilesystemOperation::Kind::FileFactory||','operation_recipe_owner->kind==PreparedFilesystemOperation::Kind::ReadText||operation_recipe_owner->kind==PreparedFilesystemOperation::Kind::FileFactory||',1)
t=t.replace('||x.rfind("file(",0)==0)', '||x.rfind("file(",0)==0||x.rfind("open(",0)==0)',1)
for text,name in [(s,'ParserExpression.cpp'),(t,'ParserTemplate.cpp')]:
 text=text.replace('d=render_expression_value(v);if(', 'd=v.is_string()?std::move(v.string):render_expression_value(v);if(')
 if name=='ParserExpression.cpp':s=text
 else:t=text
for name,data in [('ParserHelpers.h',h),('ParserHelpers.cpp',cpp),('ParserExpression.cpp',s),('Parser.h',ph),('ParserTemplate.cpp',t)]:
 (p/name).write_text(data)
 target=p/'prototype/src'/name
 if target.read_text()!=data:target.write_text(data)
print('Shared owned stream sink and source-aware open recipes; ordinary writes move validated strings')
