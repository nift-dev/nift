#include "Parser.h"
#include "ParserHelpers.h"
#include "JsonFile.h"
#include "PackageGraphLock.h"
#include "PackageMetadata.h"
#include "PackageTransaction.h"
#include "Console.h"
#include "FileSystem.h"

#include <nift/context.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <exception>
#include <functional>
#include <set>
#include <type_traits>

namespace fs = std::filesystem;

using nift::detail::valid_binding_identifier;
using nift::detail::strip_presentation_chain;
using nift::detail::nift_binding_type;

namespace {
fs::path stable_import_identity(const fs::path& path) {
    std::error_code ec;
    fs::path identity=fs::weakly_canonical(fs::absolute(path).lexically_normal(),ec);
    if(ec)identity=fs::absolute(path).lexically_normal();
#ifdef _WIN32
    auto native=identity.native();
    std::transform(native.begin(),native.end(),native.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
    identity=fs::path(std::move(native));
#endif
    return identity;
}
}

bool Parser::translate_function_program(const std::string& source, std::string& translated, std::string& error, nift::detail::SourceView input_view, nift::detail::SourceView* output_view, nift::detail::DiagnosticOrigin* failure_origin) const {
    using nift::detail::SourceView;
    if(!input_view) input_view=SourceView::identity({},source);
    std::function<bool(const std::string&, std::string&, SourceView, SourceView&)> convert;
    convert = [&](const std::string& in, std::string& out, SourceView view, SourceView& translated_view) -> bool {
        std::size_t i=0;
        nift::detail::SourceBuilder mapped(view);
        auto generated=[&](std::string_view text,std::size_t anchor,std::size_t length=1){mapped.generated(text,anchor,length);};
        auto copied=[&](std::size_t start,std::size_t length){mapped.copy(in,start,length);};
        auto nested=[&](const std::string& text,const SourceView& child){mapped.append(text,child);};
        auto finish=[&](){auto completed=std::move(mapped).finish();out=std::move(completed.text);translated_view=std::move(completed.view);return true;};
        auto translation_failure=[&](){if(failure_origin)*failure_origin=view.locate(i);return false;};
        auto source_position=[&](std::size_t offset){auto position=view.locate(offset);return std::pair<std::size_t,std::size_t>{position.line,position.column};};
        auto boundary=[&](std::size_t p,const std::string& kw){return in.compare(p,kw.size(),kw)==0 && (p+kw.size()==in.size() || (!std::isalnum((unsigned char)in[p+kw.size()]) && in[p+kw.size()]!='_'));};
        while(i<in.size()) {
            while(i<in.size() && std::isspace((unsigned char)in[i])) ++i;
            if(i>=in.size()) break;
            if(boundary(i,"fn")) {
                std::size_t p=i+2; while(p<in.size()&&std::isspace((unsigned char)in[p]))++p; bool async=false;
                if(p+7<=in.size() && in.compare(p,7,"[async]")==0){async=true;p+=7;while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;}
                std::size_t pc=0;
                if(p>=in.size()||in[p]!='('||!find_balanced(in,p,'(',')',pc)){error="fn requires '(name(args))'";return translation_failure();}
                std::size_t bo=pc+1; while(bo<in.size()&&std::isspace((unsigned char)in[bo]))++bo; std::size_t bc=0;
                if(bo>=in.size()||in[bo]!='{'||!find_balanced(in,bo,'{','}',bc)){error="fn requires a block";return translation_failure();}
                std::string body;SourceView body_view;if(!convert(in.substr(bo+1,bc-bo-1),body,view.slice(bo+1,bc-bo-1),body_view))return false;
                generated(async?"@fn[async](":"@fn(",i);copied(p+1,pc-p-1);generated("){",bo);nested(body,body_view);generated("}",bc); i=bc+1; continue;
            }
            if(boundary(i,"enum")) {
                std::size_t p=i+4;while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;std::size_t ns=p;while(p<in.size()&&(std::isalnum((unsigned char)in[p])||in[p]=='_'))++p;std::string name=in.substr(ns,p-ns);while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;std::size_t bc=0;
                if(name.empty()||p>=in.size()||in[p]!='{'||!find_balanced(in,p,'{','}',bc)){error="enum requires 'Name { members }'";return translation_failure();}generated("@enum(",i);copied(ns,name.size());generated("){",p);copied(p+1,bc-p-1);generated("}",bc);i=bc+1;continue;
            }
            if(boundary(i,"struct")) {
                std::size_t p=i+6; while(p<in.size()&&std::isspace((unsigned char)in[p]))++p; std::size_t pc=0;
                if(p>=in.size()||in[p]!='('||!find_balanced(in,p,'(',')',pc)){error="struct requires '(name)'";return translation_failure();}
                std::size_t bo=pc+1; while(bo<in.size()&&std::isspace((unsigned char)in[bo]))++bo; std::size_t bc=0;
                if(bo>=in.size()||in[bo]!='{'||!find_balanced(in,bo,'{','}',bc)){error="struct requires a block";return translation_failure();}
                generated("@struct(",i);copied(p+1,pc-p-1);generated("){",bo);copied(bo+1,bc-bo-1);generated("}",bc); i=bc+1; continue;
            }
            if(boundary(i,"export")) {
                std::size_t p=i+6; while(p<in.size()&&std::isspace((unsigned char)in[p]))++p; std::size_t pc=0;
                if(p>=in.size()||in[p]!='('||!find_balanced(in,p,'(',')',pc)){error="export requires '(binding)'";return translation_failure();}
                generated("@__export(",i);copied(p+1,pc-p-1);generated(")",pc); i=pc+1; if(i<in.size()&&in[i]==';')++i; continue;
            }
            if(boundary(i,"import")) {
                std::size_t p=i+6; while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;
                if(p<in.size()&&in[p]=='('){const auto source_lines=std::count(in.begin(),in.begin()+i,'\n');const auto output_lines=std::count(mapped.text().begin(),mapped.text().end(),'\n');if(output_lines<source_lines)generated(std::string(source_lines-output_lines,'\n'),i);const auto line_start=in.rfind('\n',i);const auto indent_start=line_start==std::string::npos?0:line_start+1;if(std::all_of(in.begin()+indent_start,in.begin()+i,[](char c){return c==' '||c=='\t';}))copied(indent_start,i-indent_start);std::size_t pc=0;if(!find_balanced(in,p,'(',')',pc)){copied(i,in.size()-i);return finish();}copied(i,pc-i+1);i=pc+1;if(i<in.size()&&in[i]==';')++i;continue;}
            }
            // A single-line @// comment must be consumed to end-of-line BEFORE
            // statement splitting, so a ';' inside the comment cannot turn the
            // rest of the comment into a statement.
            if (in.compare(i, 3, "@//") == 0) {
                std::size_t line_end = in.find('\n', i);
                generated("\n",i);
                i = (line_end == std::string::npos) ? in.size() : line_end;
                continue;
            }
            // Script-land single-line comment without the template '@' prefix.
            if (in.compare(i, 2, "//") == 0) {
                std::size_t line_end = in.find('\n', i);
                generated("\n",i);
                i = (line_end == std::string::npos) ? in.size() : line_end;
                continue;
            }
            if (in.compare(i, 3, "@/*") == 0) {
                std::size_t block_end = in.find("*/", i + 3);
                if (block_end == std::string::npos) { error = "open comment '@/*' has no close '*/'"; return translation_failure(); }
                i = block_end + 2;
                continue;
            }
            // Script-land block comment without the template '@' prefix.
            if (in.compare(i, 2, "/*") == 0) {
                std::size_t block_end = in.find("*/", i + 2);
                if (block_end == std::string::npos) { error = "open comment '/*' has no close '*/'"; return translation_failure(); }
                const auto line_count=std::count(in.begin()+i,in.begin()+block_end+2,'\n');
                if(line_count)generated(std::string(line_count,'\n'),i);else generated(" ",i);
                i = block_end + 2;
                continue;
            }
            if(boundary(i,"while")) {
                std::size_t p=i+5;while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;std::size_t pc=0;
                if(p>=in.size()||in[p]!='('||!find_balanced(in,p,'(',')',pc)){error="function while requires '(...)'";return translation_failure();}
                std::size_t bo=pc+1;while(bo<in.size()&&std::isspace((unsigned char)in[bo]))++bo;std::size_t bc=0;
                if(bo>=in.size()||in[bo]!='{'||!find_balanced(in,bo,'{','}',bc)){error="function while requires a block";return translation_failure();}
                std::string body;SourceView body_view;if(!convert(in.substr(bo+1,bc-bo-1),body,view.slice(bo+1,bc-bo-1),body_view))return false;generated("@while(",i);copied(p+1,pc-p-1);generated("){",bo);nested(body,body_view);generated("}",bc);i=bc+1;continue;
            }
            if(boundary(i,"try")) {
                std::size_t bo=i+3;while(bo<in.size()&&std::isspace((unsigned char)in[bo]))++bo;
                if(bo<in.size()&&in[bo]=='{'){
                    std::size_t bc=0;if(!find_balanced(in,bo,'{','}',bc)){error="try requires a balanced block";return translation_failure();}
                    std::size_t cp=bc+1;while(cp<in.size()&&std::isspace((unsigned char)in[cp]))++cp;
                    if(!boundary(cp,"catch")){error="try requires catch(identifier)";return translation_failure();}
                    cp+=5;while(cp<in.size()&&std::isspace((unsigned char)in[cp]))++cp;std::size_t cc=0;
                    if(cp>=in.size()||in[cp]!='('||!find_balanced(in,cp,'(',')',cc)){error="catch requires '(identifier)'";return translation_failure();}
                    const std::string binding=trim_copy(in.substr(cp+1,cc-cp-1));
                    if(!valid_binding_identifier(binding)){error="catch requires one valid identifier";return translation_failure();}
                    std::size_t cbo=cc+1;while(cbo<in.size()&&std::isspace((unsigned char)in[cbo]))++cbo;std::size_t cbc=0;
                    if(cbo>=in.size()||in[cbo]!='{'||!find_balanced(in,cbo,'{','}',cbc)){error="catch(identifier) requires a block";return translation_failure();}
                    std::string try_body,catch_body;SourceView try_view;SourceView catch_view;if(!convert(in.substr(bo+1,bc-bo-1),try_body,view.slice(bo+1,bc-bo-1),try_view)||!convert(in.substr(cbo+1,cbc-cbo-1),catch_body,view.slice(cbo+1,cbc-cbo-1),catch_view))return false;
                    generated("@__try(",i);copied(cp+1+in.substr(cp+1,cc-cp-1).find_first_not_of(" \t\r\n"),binding.size());generated("){",bo);nested(try_body,try_view);generated("}{",cbo);nested(catch_body,catch_view);generated("}",cbc);i=cbc+1;continue;
                }
            }
            if(boundary(i,"for")) {
                std::size_t p=i+3; while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;
                std::size_t pc=0; if(p>=in.size()||in[p]!='('||!find_balanced(in,p,'(',')',pc)){error="function for requires '(...)'";return translation_failure();}
                std::size_t bo=pc+1;while(bo<in.size()&&std::isspace((unsigned char)in[bo]))++bo;std::size_t bc=0;
                if(bo>=in.size()||in[bo]!='{'||!find_balanced(in,bo,'{','}',bc)){error="function for requires a block";return translation_failure();}
                std::string body;SourceView body_view;if(!convert(in.substr(bo+1,bc-bo-1),body,view.slice(bo+1,bc-bo-1),body_view))return false;
                generated("@for(",i);copied(p+1,pc-p-1);generated("){",bo);nested(body,body_view);generated("}",bc);i=bc+1;continue;
            }
            if(boundary(i,"if")) {
                std::size_t p=i+2; while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;
                if(p>=in.size()||in[p]!='('){error="function if requires '(...)'";return translation_failure();}
                std::size_t pc=0; if(!find_balanced(in,p,'(',')',pc)){error="function if has no matching ')'";return translation_failure();}
                std::size_t bo=pc+1; while(bo<in.size()&&std::isspace((unsigned char)in[bo]))++bo;
                std::size_t bc=0; if(bo>=in.size()||in[bo]!='{'||!find_balanced(in,bo,'{','}',bc)){error="function if requires a block";return translation_failure();}
                std::string body;SourceView body_view; if(!convert(in.substr(bo+1,bc-bo-1),body,view.slice(bo+1,bc-bo-1),body_view))return false;
                generated("@if(",i);copied(p+1,pc-p-1);generated("){",bo);nested(body,body_view);generated("}",bc); i=bc+1;
                while(true){std::size_t e=i;while(e<in.size()&&std::isspace((unsigned char)in[e]))++e;if(!boundary(e,"else"))break;e+=4;while(e<in.size()&&std::isspace((unsigned char)in[e]))++e;
                    if(boundary(e,"if")){std::size_t q=e+2;while(q<in.size()&&std::isspace((unsigned char)in[q]))++q;std::size_t qc=0;if(q>=in.size()||in[q]!='('||!find_balanced(in,q,'(',')',qc)){error="function else if malformed";return translation_failure();}std::size_t eb=qc+1;while(eb<in.size()&&std::isspace((unsigned char)in[eb]))++eb;std::size_t ec=0;if(eb>=in.size()||in[eb]!='{'||!find_balanced(in,eb,'{','}',ec)){error="function else if requires block";return translation_failure();}std::string body2;SourceView body2_view;if(!convert(in.substr(eb+1,ec-eb-1),body2,view.slice(eb+1,ec-eb-1),body2_view))return false;generated(" else if(",e);copied(q+1,qc-q-1);generated("){",eb);nested(body2,body2_view);generated("}",ec);i=ec+1;continue;}
                    std::size_t ec=0;if(e>=in.size()||in[e]!='{'||!find_balanced(in,e,'{','}',ec)){error="function else requires block";return translation_failure();}std::string body2;SourceView body2_view;if(!convert(in.substr(e+1,ec-e-1),body2,view.slice(e+1,ec-e-1),body2_view))return false;generated(" else {",e);nested(body2,body2_view);generated("}",ec);i=ec+1;break;}
                continue;
            }
            if(boundary(i,"return")) {
                std::size_t e=i+6; bool quoted=false;char quote=0;
                while(e<in.size()&&in[e]!='\n'&&in[e]!=';' ) { char c=in[e]; if(quoted){if(c=='\\'&&e+1<in.size())++e;else if(c==quote)quoted=false;}else if(c=='\''||c=='"'){quoted=true;quote=c;}++e; }
                std::string expr=trim_copy(in.substr(i+6,e-(i+6))); if(expr.empty())generated("@__bare_return()",i);else{generated("@return(",i);copied(i+6+in.substr(i+6,e-i-6).find_first_not_of(" \t\r\n"),expr.size());generated(")",e);}  i=e<in.size()?e+1:e; continue;
            }
            if(boundary(i,"throw") && i+5<in.size() && std::isspace((unsigned char)in[i+5]) && trim_copy(in.substr(i+5)).rfind(":=",0)!=0 && trim_copy(in.substr(i+5)).rfind("=",0)!=0 && trim_copy(in.substr(i+5)).rfind("+=",0)!=0 && trim_copy(in.substr(i+5)).rfind("-=",0)!=0 && trim_copy(in.substr(i+5)).rfind("*=",0)!=0 && trim_copy(in.substr(i+5)).rfind("/=",0)!=0 && trim_copy(in.substr(i+5)).rfind("%=",0)!=0) {
                std::size_t e=i+6;bool quoted=false;char quote=0;int par=0,br=0,bc=0;
                for(;e<in.size();++e){char c=in[e];if(quoted){if(c=='\\'&&e+1<in.size())++e;else if(c==quote)quoted=false;continue;}if(c=='\''||c=='"'){quoted=true;quote=c;continue;}if(c=='(')++par;else if(c==')')--par;else if(c=='[')++br;else if(c==']')--br;else if(c=='{')++bc;else if(c=='}')--bc;if(!par&&!br&&!bc&&(c==';'||c=='\n'))break;}
                const std::string expr=trim_copy(in.substr(i+5,e-(i+5)));if(expr.empty()){error="throw requires an Error expression";return translation_failure();}
                const auto throw_position=source_position(i);generated("@__throw("+std::to_string(throw_position.first)+","+std::to_string(throw_position.second)+",",i);copied(i+5+in.substr(i+5,e-i-5).find_first_not_of(" \t\r\n"),expr.size());generated(")",e);i=e<in.size()?e+1:e;continue;
            }
            if(boundary(i,"break")) { std::size_t e=i+5; while(e<in.size()&&std::isspace((unsigned char)in[e])&&in[e]!='\n')++e; if(e==in.size()||in[e]==';'||in[e]=='\n') { copied(i,5); i=e<in.size()?e+1:e; continue; } }
            if(boundary(i,"continue")) { std::size_t e=i+8; while(e<in.size()&&std::isspace((unsigned char)in[e])&&in[e]!='\n')++e; if(e==in.size()||in[e]==';'||in[e]=='\n') { copied(i,8); i=e<in.size()?e+1:e; continue; } }
            std::size_t start=i; bool quoted=false;char quote=0;int par=0,br=0,bc=0;
            for(;i<in.size();++i){char c=in[i];if(quoted){if(c=='\\'&&i+1<in.size())++i;else if(c==quote)quoted=false;continue;}if(c=='\''||c=='"'){quoted=true;quote=c;continue;}if(c=='(')++par;else if(c==')')--par;else if(c=='[')++br;else if(c==']')--br;else if(c=='{')++bc;else if(c=='}')--bc;if(!par&&!br&&!bc&&(c==';'||c=='\n'))break;}
            std::string stmt=trim_copy(in.substr(start,i-start));
            if(!stmt.empty()){
                // Template-grammar statements (legacy @-directives and $[...]
                // values) are already parseable directly and would otherwise be
                // double-wrapped as $[@...]/$[$...] and routed through the full
                // expression evaluator. Pass them through verbatim; only bare
                // v4.2 function-program statements need the $[...] wrapper.
                if(stmt.rfind("#!",0)==0){
                    // Only a leading file shebang is stripped by CLI source
                    // loading. Elsewhere the same marker remains inert comment
                    // text; never reinterpret it as an external path command.
                    generated("@//",start);copied(start+2,stmt.size()-2);generated("\n",i);
                } else if(stmt[0]=='@'||stmt[0]=='$'){
                    copied(start,stmt.size());
                    // A verbatim single-line comment has no terminating newline
                    // once inter-statement whitespace is stripped; emit one so it
                    // cannot swallow the following statement.
                    if(stmt.rfind("@//",0)==0) generated("\n",i);
                } else {
                    // `await future` is a language expression even though it is
                    // a multi-token statement; never route it through shell-style
                    // external command dispatch.
                    if(stmt.rfind("await ",0)==0){ generated("$[",start,stmt.size());copied(start,stmt.size());generated("]",i); if(i<in.size())++i; continue; }
                    // Executable-path command style: a statement whose first
                    // token is a filesystem path (./x, ../x, /x, dir/x) runs the
                    // executable as an ordinary external process (executable .f
                    // scripts, .sh, .py, binaries). This is process execution,
                    // never in-process Nift evaluation.
                    std::size_t f0=stmt.find_first_of(" \t");
                    const std::string first=f0==std::string::npos?stmt:stmt.substr(0,f0);
                    const bool path_first = (first.rfind("./",0)==0||first.rfind("../",0)==0||(!first.empty()&&first[0]=='/')||first.find('/')!=std::string::npos);
                    // Command-style also covers a plain word + arguments
                    // (git status, gh repo view, printf one): a multi-token
                    // line without Nift syntax cannot be a single expression,
                    // so it is ordinary external process execution. A single
                    // bare token stays an expression (bindings have precedence).
                    const bool multi_token = f0 != std::string::npos;
                    // Reject Nift declaration/assignment forms: x := 5, x = 5,
                    // x=5, ./x = 5. A '=' inside a later command argument
                    // (printf a=b, git log --format=%H) stays a command.
                    bool assignment_form = first.find('=')!=std::string::npos;
                    if(!assignment_form && f0!=std::string::npos){
                        const std::string rest=trim_copy(stmt.substr(f0));
                        assignment_form = rest.rfind(":=",0)==0 || (rest.rfind("=",0)==0 && rest.size()>1 && rest[1]!='=');
                    }
                    // Only a plain command line (no unquoted parens/brackets/
                    // braces/assignment) is external command style. This keeps
                    // ./tool(args) call attempts and `./x = ...` out of the
                    // process path so the shell's callable-first routing still
                    // works.
                    bool cmd_like=(path_first||multi_token)&&!assignment_form; bool qq=false;char qc=0;int pp=0,bb=0,cc2=0;bool saw_delim=false;
                    for(std::size_t k=0;k<stmt.size()&&cmd_like;++k){char c=stmt[k];if(qq){if(c=='\\')++k;else if(c==qc)qq=false;continue;}if(c=='\''||c=='"'){qq=true;qc=c;continue;}if(c=='('){++pp;saw_delim=true;}else if(c==')'){--pp;saw_delim=true;}else if(c=='['){++bb;saw_delim=true;}else if(c==']'){--bb;saw_delim=true;}else if(c=='{'){++cc2;saw_delim=true;}else if(c=='}'){--cc2;saw_delim=true;}else if((c=='='||c==':')&&pp==0&&bb==0&&cc2==0){cmd_like=false;}}
                    if(cmd_like&&!saw_delim&&pp==0&&bb==0&&cc2==0) {generated("@__nift_cmd(",start,stmt.size());copied(start,stmt.size());generated(")",i);}
                    else {generated("$[",start,stmt.size());copied(start,stmt.size());generated("]",i);}
                }
            }
            if(i<in.size())++i;
        }
        return finish();
    };
    translated.clear();SourceView translated_view;const bool ok=convert(source,translated,input_view,translated_view);if(output_view)*output_view=std::move(translated_view);return ok;
}

Parser::StatementState Parser::statement_state(const std::string& raw) const {
    const std::string source = trim_copy(raw);
    if (source.empty()) return StatementState::Complete;
    // Structural completeness: a quoted string and every (parenthesis, bracket
    // or brace) must be closed by the end of the submitted text. This mirrors
    // the delimiter conventions used by the function-program translator
    // (quote-aware, with no comment grammar in statement land). An unmatched
    // closing delimiter is malformed rather than merely incomplete.
    bool quoted = false; char qc = 0; int par = 0, br = 0, bc = 0;
    for (std::size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        if (quoted) { if (c == '\\' && i + 1 < source.size()) ++i; else if (c == qc) quoted = false; continue; }
        if (c == '\'' || c == '"') { quoted = true; qc = c; continue; }
        if (c == '(') ++par; else if (c == ')') --par;
        else if (c == '[') ++br; else if (c == ']') --br;
        else if (c == '{') ++bc; else if (c == '}') --bc;
        if (par < 0 || br < 0 || bc < 0) return StatementState::Invalid;
    }
    if (quoted || par != 0 || br != 0 || bc != 0) return StatementState::Incomplete;
    // Balanced input must still translate into a valid statement program;
    // otherwise it is invalid (a complete but malformed form).
    std::string program, error;
    if (!translate_function_program(source, program, error)) return StatementState::Invalid;
    return StatementState::Complete;
}

std::vector<std::string> Parser::shell_completions(const std::string& prefix) const {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto& kv : callables_) if (kv.first.rfind(prefix, 0) == 0 && seen.insert(kv.first).second) out.push_back(kv.first);
    for (const auto& kv : structs_) if (kv.first.rfind(prefix, 0) == 0 && seen.insert(kv.first).second) out.push_back(kv.first);
    for (auto scope = variable_scopes_.rbegin(); scope != variable_scopes_.rend(); ++scope)
        for (const auto& kv : *scope) if (kv.first.rfind(prefix, 0) == 0 && seen.insert(kv.first).second) out.push_back(kv.first);
    return out;
}

bool Parser::invoke_ffi_callback_i64(const std::string& callable_tag, std::int64_t arg, std::int64_t& out_value, std::string& error) {
    if (variable_scopes_.empty()) variable_scopes_.emplace_back();
    const std::string name="__nift_ffi_callback_internal"; auto old=variable_scopes_.back().find(name); std::optional<VariableBinding> saved; if(old!=variable_scopes_.back().end()) saved=old->second;
    auto sp=std::make_shared<nift::RuntimeValue>(callable_tag); variable_scopes_.back()[name]=VariableBinding{sp,nift_binding_type(*sp),false,false};
    nift::RuntimeValue result; bool ok=evaluate_expression(name+"("+std::to_string(arg)+")",result,error);
    if(saved) variable_scopes_.back()[name]=*saved; else variable_scopes_.back().erase(name);
    if(!ok){append_diagnostic_frame(nift::detail::DiagnosticFrameKind::Callback,"ffi callback");return false;}
    if(!nift::runtime_number_to_i64(result,out_value)){error="callback result must be an integer";return false;}return true;
}

bool Parser::invoke_callable(const std::string& name, const std::vector<nift::RuntimeValue>& args, nift::RuntimeValue& value, std::string& error, nift::detail::Diagnostic* diagnostic) {
    active_diagnostic_.reset();active_recoverable_.reset();if(!valid_binding_identifier(name)){error="invalid callable name";return false;}if(variable_scopes_.empty())variable_scopes_.emplace_back();std::vector<std::string> names;names.reserve(args.size());for(size_t i=0;i<args.size();++i){std::string n="__nift_host_arg_"+std::to_string(i);auto sp=std::make_shared<nift::RuntimeValue>(args[i]);variable_scopes_.back()[n]=VariableBinding{sp,nift_binding_type(*sp),false,false};names.push_back(n);}std::string expr=name+"(";for(size_t i=0;i<names.size();++i){if(i)expr+=",";expr+=names[i];}expr+=")";bool ok=evaluate_expression(expr,value,error);for(const auto& n:names)variable_scopes_.back().erase(n);if(!ok&&diagnostic&&active_diagnostic_)*diagnostic=*active_diagnostic_;return ok;
}

void Parser::set_script_invocation(std::string cmd, std::vector<std::string> args) {
    script_cmd_ = std::move(cmd);
    script_args_ = std::move(args);
    install_script_invocation_bindings();
}

void Parser::install_script_invocation_bindings() {
    if (variable_scopes_.empty()) variable_scopes_.emplace_back();
    auto cmd = std::make_shared<nift::RuntimeValue>(script_cmd_);
    VariableBinding cmd_binding{cmd, nift_binding_type(*cmd), false, true};
    cmd_binding.is_script_invocation = true;
    variable_scopes_.front()["cmd"] = std::move(cmd_binding);

    auto arr = std::make_shared<nift::RuntimeValue>(nift::RuntimeValue::make_array());
    for (const auto& a : script_args_) arr->array.emplace_back(a);
    VariableBinding args_binding{arr, nift_binding_type(*arr), false, true};
    args_binding.is_script_invocation = true;
    variable_scopes_.front()["args"] = std::move(args_binding);
}

RenderResult Parser::run_script(const std::string& source, const fs::path& source_path, bool project_build) {
    result_ = RenderResult{};
    active_diagnostic_.reset();
    active_recoverable_.reset();
    variable_scopes_.clear(); variable_scopes_.emplace_back(); timer_instances_.clear();
    install_script_invocation_bindings();
    callables_.clear(); structs_.clear(); requested_exports_.clear(); pending_control_={};
    active_module_env_.reset(); loading_module_env_.reset(); saved_lexical_scopes_.clear(); module_envs_.clear(); next_module_identity_=1;
    in_import_program_=false; standalone_script_host_=!project_build; resource_path_authority_.enforce_project_root=project_build; resource_path_authority_.enforce_filesystem_root=!project_build; strict_script_mode_=true; function_call_depth_=1;
    const std::string identity=source_path.generic_string();
    const auto provenance=!source_path.empty()&&!(identity.size()>=2&&identity.front()=='<'&&identity.back()=='>')
        ? SourceProvenance::FileBacked : SourceProvenance::InMemory;
    auto rr=execute_native_program(source,source_path,0,provenance,false);
    function_call_depth_=0; strict_script_mode_=false;
    rr.output.clear();
    if(rr.ok && pending_control_.kind==ControlFlow::Return && pending_control_.value) {
        const auto& v=*pending_control_.value;
        if(v.is_error()||v.is_timer()||v.is_bytes()||v.is_array()||v.is_object()||(v.is_string()&&v.string.rfind("\x1fnift:",0)==0&&v.string.rfind("\x1fnift:timer:",0)!=0)) { rr.ok=false; rr.error.message="script return value is not directly renderable";rr.diagnostic=nift::detail::make_diagnostic(nift::detail::DiagnosticCode::NativeUnsupportedValue,rr.error.message); }
        else rr.output=render_expression_value(v);
    }
    pending_control_={};
    std::string resource_error;
    if(!finalize_script_resources(resource_error) && rr.ok){rr.ok=false;rr.error.message=resource_error;rr.diagnostic=nift::detail::make_diagnostic(nift::detail::DiagnosticCode::NativeResourceLifecycle,resource_error);}
    return rr;
}

bool Parser::run_embedded_script(const std::string& source, const fs::path& source_path, nift::RuntimeValue& value, std::string& error, nift::detail::Diagnostic* diagnostic) {
    result_=RenderResult{};active_diagnostic_.reset();active_recoverable_.reset();variable_scopes_.clear();variable_scopes_.emplace_back();timer_instances_.clear();install_script_invocation_bindings();callables_.clear();structs_.clear();requested_exports_.clear();pending_control_={};active_module_env_.reset();loading_module_env_.reset();saved_lexical_scopes_.clear();module_envs_.clear();next_module_identity_=1;in_import_program_=false;standalone_script_host_=false;resource_path_authority_.enforce_project_root=true;resource_path_authority_.enforce_filesystem_root=false;strict_script_mode_=true;function_call_depth_=1;
    const std::uint64_t timer_checkpoint=begin_timer_operation();
    auto rr=execute_native_program(source,source_path,0,SourceProvenance::InMemory,false);function_call_depth_=0;strict_script_mode_=false;
    if(!rr.ok){error=rr.error.message;if(diagnostic&&rr.diagnostic)*diagnostic=*rr.diagnostic;pending_control_={};std::string ignored_resource_error;finalize_script_resources(ignored_resource_error);finish_timer_operation(timer_checkpoint);return false;}
    value=nift::RuntimeValue(nullptr);if(pending_control_.kind==ControlFlow::Return&&pending_control_.value)value=*pending_control_.value;pending_control_={};const bool timer_result=contains_timer_resource(value);const bool error_result=contains_error_resource(value);std::string resource_error;const bool resources_ok=finalize_script_resources(resource_error);finish_timer_operation(timer_checkpoint);if(timer_result){error="embedded script cannot return a timer value";if(!resources_ok)error+="; "+resource_error;return false;}if(error_result){error="embedded script cannot return an Error value";if(!resources_ok)error+="; "+resource_error;return false;}if(!resources_ok){error=resource_error;return false;}return true;
}

RenderResult Parser::run_statement(const std::string& source, const fs::path& source_path) {
    const std::uint64_t file_checkpoint=begin_file_operation();
    struct TimerOperationGuard {
        Parser& parser;
        std::uint64_t checkpoint;
        bool success = false;
        ~TimerOperationGuard() {
            if (success) parser.finish_timer_operation(checkpoint);
            else parser.rollback_timer_operation(checkpoint);
        }
    } timer_operation{*this,begin_timer_operation()};
    if(variable_scopes_.empty()) { variable_scopes_.emplace_back(); install_script_invocation_bindings(); }
    active_diagnostic_.reset();active_recoverable_.reset();standalone_script_host_=true; resource_path_authority_.enforce_project_root=false; resource_path_authority_.enforce_filesystem_root=true; strict_script_mode_=true; result_ = RenderResult{}; pending_control_={}; function_call_depth_=1;
    const std::string identity=source_path.generic_string();const auto provenance=!source_path.empty()&&!(identity.size()>=2&&identity.front()=='<'&&identity.back()=='>')?SourceProvenance::FileBacked:SourceProvenance::InMemory;
    const std::string t=trim_copy(source);
    // A REPL is also an inspector: a single expression is evaluated directly so
    // arrays/collections/structs can be safely displayed without routing through
    // template rendering (which intentionally rejects compound values).
    std::size_t import_open=6; while(import_open<t.size()&&std::isspace(static_cast<unsigned char>(t[import_open])))++import_open;
    const bool import_statement=t.rfind("import",0)==0&&(t.size()==6||(!std::isalnum(static_cast<unsigned char>(t[6]))&&t[6]!='_'))&&import_open<t.size()&&t[import_open]=='(';
    const bool declaration = t.rfind("fn(",0)==0 || t.rfind("struct(",0)==0 || t.rfind("if(",0)==0 ||
        t.rfind("for(",0)==0 || t.rfind("while(",0)==0 || t.rfind("return",0)==0 || t.rfind("export(",0)==0 || import_statement;
    bool assignment=false; bool quoted=false; char qc=0; int par=0,br=0,bc=0;
    for(std::size_t i=0;i<t.size();++i){char c=t[i];if(quoted){if(c=='\\')++i;else if(c==qc)quoted=false;continue;}if(c=='\''||c=='"'){quoted=true;qc=c;continue;}if(c=='(')++par;else if(c==')')--par;else if(c=='[')++br;else if(c==']')--br;else if(c=='{')++bc;else if(c=='}')--bc;else if(!par&&!br&&!bc&&c=='='){char a=i?t[i-1]:0,b=i+1<t.size()?t[i+1]:0;if(a!='='&&a!='!'&&a!='<'&&a!='>'&&b!='='){assignment=true;break;}}}
    if(!declaration&&!assignment&&!t.empty()){
        nift::RuntimeValue v; std::string error;
        source_context_stack_.push_back(SourceContext{source_path,provenance});
        struct SourceContextGuard {
            std::vector<SourceContext>& stack;
            ~SourceContextGuard() { stack.pop_back(); }
        } source_context_guard{source_context_stack_};
        const bool evaluated=evaluate_expression(t,v,error);
        if(evaluated){
            RenderResult rr; std::string shown;
            // REPL commands commonly return null (for example cd()) and an empty
            // string has nothing useful to inspect. Keep both silent rather than
            // displaying `null` or an empty quoted string.
            if(v.is_null() || (v.is_string() && v.string.empty())) {
                timer_operation.success=true;function_call_depth_=0;strict_script_mode_=false;return rr;
            }
            // Presentation modifiers are composable and order-independent. The evaluator
            // has already serialized their original value into an ANSI-free string; here
            // the REPL only decides whether that representation should be highlighted.
            std::string presentation; bool presentation_method=false, highlight=false;
            { bool p=false,h=false; if(strip_presentation_chain(t,presentation,p,h)){presentation_method=true;highlight=h;} }
            if(presentation_method&&v.is_string()) shown=v.string;
            else if(!serialize_value(v,false,shown,error)){rr.ok=false;rr.error.message=error;rollback_file_operation(file_checkpoint);function_call_depth_=0;strict_script_mode_=false;return rr;}
            if(highlight && console::stdout_colour_enabled()) shown=console::highlight_nift_value(shown);
            rr.output=shown;timer_operation.success=true;function_call_depth_=0;strict_script_mode_=false;return rr;
        }
        // If direct evaluation fails, let the native statement path provide the
        // canonical diagnostic; it may be a valid statement form not recognized above.
    }
    auto rr=execute_native_program(source,source_path,0,provenance);if(!rr.ok)rollback_file_operation(file_checkpoint);timer_operation.success=rr.ok;function_call_depth_=0;strict_script_mode_=false;pending_control_={};return rr;
}

void Parser::reset_script_control() { pending_control_={}; active_recoverable_.reset(); result_=RenderResult{}; }

RenderResult Parser::execute_native_program(const std::string& source, const fs::path& source_path, int depth,
                                            SourceProvenance source_provenance, bool rollback_files_on_failure, nift::detail::SourceView view) {
    const std::uint64_t file_checkpoint = begin_file_operation();
    if(!view)view=nift::detail::SourceView::identity(source_path,source);
    std::string program, error;nift::detail::SourceView program_view;nift::detail::DiagnosticOrigin translation_origin;
    if (!translate_function_program(source, program, error,view,&program_view,&translation_origin)) {
        RenderResult failed; failed.ok=false; failed.error.message=error;failed.error.source_file=translation_origin.source;failed.error.line=translation_origin.line;failed.error.column=translation_origin.column;failed.error.source_length=translation_origin.source_length;failed.error.source_line=translation_origin.source_line;failed.diagnostic=nift::detail::make_diagnostic(nift::detail::DiagnosticCode::NativeTranslationError,error,std::move(translation_origin));if(rollback_files_on_failure)rollback_file_operation(file_checkpoint);return failed;
    }
    RenderResult rr;
    try {
        rr=parse(program, source_path, depth, source_provenance,program_view);
    } catch (const std::exception& exception) {
        rr.ok=false;
        rr.error.message=exception.what();
        rr.diagnostic=nift::detail::make_diagnostic(nift::detail::DiagnosticCode::InternalUnexpectedException,rr.error.message);
    } catch (...) {
        rr.ok=false;
        rr.error.message="script evaluation failed";
        rr.diagnostic=nift::detail::make_diagnostic(nift::detail::DiagnosticCode::InternalUnexpectedException,rr.error.message);
    }
    if(!rr.ok){if(!rr.diagnostic)rr.diagnostic=nift::detail::make_diagnostic(nift::detail::DiagnosticCode::InternalLegacyFailure,rr.error.message);if(rr.diagnostic->origin.source.empty()){rr.diagnostic->origin.source=rr.error.source_file;rr.diagnostic->origin.line=rr.error.line;rr.diagnostic->origin.column=rr.error.column;rr.diagnostic->origin.source_length=rr.error.source_length;rr.diagnostic->origin.source_line=rr.error.source_line;}}
    if (!rr.ok && rollback_files_on_failure) rollback_file_operation(file_checkpoint);
    return rr;
}

bool Parser::execute_import_file(const std::string& argument, const fs::path& caller_path, int depth, bool legacy_syntax, std::string& error) {
    std::string normalized_argument=argument;
    std::replace(normalized_argument.begin(),normalized_argument.end(),'\\','/');
    fs::path path=normalized_argument;
    std::unique_ptr<PackageTransaction> package_reader;
    std::shared_ptr<const PackageProvenance> package_provenance;
    const bool package_name = argument.find('/') == std::string::npos && argument.find('\\') == std::string::npos && fs::path(argument).extension().empty();
    const bool relative_file_import=!package_name&&path.is_relative();
    const auto caller_module=active_module_env_ ? active_module_env_ : loading_module_env_;
    fs::path package_root=caller_module ? caller_module->package_root : fs::path{};
    if (package_name && standalone_script_host_) {
        if (!filesystem::valid_package_name(argument)) { error="invalid package name: "+argument; return false; }
        const fs::path project = fs::absolute(fs::current_path()).lexically_normal();
        package_reader=std::make_unique<PackageTransaction>(project);if(!package_reader->acquire_read(error))return false;
        const fs::path packages = fs::absolute(project/".nift"/"packages").lexically_normal();
        std::error_code package_ec, project_ec;
        const fs::path canonical_project = fs::weakly_canonical(project,project_ec);
        const fs::path canonical_packages = fs::weakly_canonical(packages,package_ec);
        if (project_ec || package_ec || canonical_packages != (canonical_project/".nift"/"packages").lexically_normal()) { error="package store must stay inside the project"; return false; }
        fs::path root = (packages/argument).lexically_normal();
        if (root.parent_path() != packages) { error="package path escapes .nift/packages: "+argument; return false; }
        package_metadata::Manifest manifest;
        // Distinguish "not installed" (manifest absent) from malformed metadata.
        if(!filesystem::file_exists(root/"manifest.json")){const std::string message="package is not installed: "+argument;fail_recoverable(nift::detail::DiagnosticCode::PackageNotInstalled,message,error);if(active_diagnostic_)active_diagnostic_->frames.push_back({nift::detail::DiagnosticFrameKind::Import,root.generic_string(),{},legacy_syntax?"@import: ":"import: "});return false;}
        if(!package_metadata::load_manifest(root/"manifest.json",true,manifest,error)){fail_fatal(nift::detail::DiagnosticCode::PackageManifestInvalid,"invalid package manifest: "+argument+": "+error,error);if(active_diagnostic_)active_diagnostic_->frames.push_back({nift::detail::DiagnosticFrameKind::Import,root.generic_string(),{},legacy_syntax?"@import: ":"import: "});return false;}
        path=(root/manifest.entry).lexically_normal();
        if(!filesystem::path_within(root,path)){fail_fatal(nift::detail::DiagnosticCode::PackageManifestInvalid,"package entry escapes the package directory: "+manifest.entry,error);if(active_diagnostic_)active_diagnostic_->frames.push_back({nift::detail::DiagnosticFrameKind::Import,root.generic_string(),{},legacy_syntax?"@import: ":"import: "});return false;}
        if (manifest.name != argument) { error="installed package name does not match import: "+argument; return false; }
        std::string locked_source,locked_commit;bool locked_found=false;
        if(!package_graph::lock_node_identity(project/".nift"/"packages.lock.json",argument,locked_source,locked_commit,locked_found,error)||!locked_found){if(error.empty())error="package lock is missing dependency: "+argument;return false;}
        package_provenance=std::make_shared<const PackageProvenance>(PackageProvenance{
            project,root,argument,locked_source,locked_commit});
        if(locked_commit!="local"){
            const auto head=filesystem::read_file_checked(root/".git"/"HEAD");std::string revision=head.value_or(std::string());
            while(!revision.empty()&&(revision.back()=='\n'||revision.back()=='\r'))revision.pop_back();
            if(revision!=locked_commit){error="installed package revision does not match package lock: "+argument;return false;}
        }
        package_root=root;
    } else if(path.is_relative()) {
        if(standalone_script_host_ && caller_path == fs::path("<nift-sh>")) path=fs::current_path()/path;
        else if(caller_module) path=caller_module->import_base/path;
        else if(!caller_path.parent_path().empty()) path=caller_path.parent_path()/path;
        else path=host_.root()/path;
    }
    path=fs::absolute(path).lexically_normal();
    if(relative_file_import&&!package_root.empty()){
        package_provenance=caller_module?caller_module->package_provenance:nullptr;
        if(!package_provenance){error="package-relative import has no frozen package provenance";return false;}
        if(!acquire_package_read(package_provenance,package_reader,error))return false;
        if(!filesystem::path_within(package_root,path)){error="package-relative import escapes package root: "+path.generic_string();return false;}
    }
    if(!standalone_script_host_ && !resource_path_authority_.project_root.empty() && !filesystem::path_within(resource_path_authority_.project_root,path)){error="path must stay inside the Nift project";return false;}
    const fs::path import_identity=stable_import_identity(path);
    bool cycle=std::find(input_stack_.begin(),input_stack_.end(),import_identity)!=input_stack_.end();
    if(!cycle)for(const auto& source:source_context_stack_)if(!source.path().empty()&&source.provenance==SourceProvenance::FileBacked&&stable_import_identity(source.path())==import_identity){cycle=true;break;}
    if(cycle){error="script import cycle through "+path.generic_string();return false;}
    auto src=host_.read_shared_source(path); if(src.status==nift::HostStatus::Error||!src.content){const std::string message=src.error.empty()?"script is not readable: "+path.generic_string():src.error+": "+path.generic_string();if(src.status==nift::HostStatus::Error)fail_fatal(nift::detail::DiagnosticCode::HostProviderError,message,error);else{fail_recoverable(package_provenance?nift::detail::DiagnosticCode::PackageImportSourceUnreadable:nift::detail::DiagnosticCode::IoImportSourceUnreadable,message,error);}if(active_diagnostic_)active_diagnostic_->frames.push_back({nift::detail::DiagnosticFrameKind::Import,path.generic_string(),{},legacy_syntax?"@import: ":"import: "});return false;}
    package_reader.reset();

    const auto module_identity_checkpoint=next_module_identity_;
    const auto lambda_identity_checkpoint=next_lambda_instance_id_;
    const auto struct_identity_checkpoint=next_struct_instance_id_;
    const auto runtime_temporary_checkpoint=next_runtime_temporary_id_;
    const auto timer_identity_checkpoint=next_timer_instance_id_;
    const auto ffi_library_checkpoint=next_ffi_library_id_;
    const auto ffi_pointer_checkpoint=next_ffi_pointer_id_;
    const auto ffi_buffer_checkpoint=next_ffi_buffer_id_;
    const auto ffi_callback_checkpoint=next_ffi_callback_id_;
    const auto collection_checkpoint=next_collection_instance_id_;
    const auto stream_checkpoint=next_stream_instance_id_;
    const auto file_identity_checkpoint=next_file_instance_id_;
    const auto command_checkpoint=next_command_instance_id_;
    const auto owned_threads_checkpoint=owned_thread_instances_.size();
    const auto owned_asyncs_checkpoint=owned_async_instances_.size();
    auto snapshot_keys=[](const auto& values){using Key=typename std::decay_t<decltype(values)>::key_type;std::unordered_set<Key> keys;for(const auto& entry:values)keys.insert(entry.first);return keys;};
    std::unordered_set<std::uint64_t> existing_modules;for(const auto& entry:module_envs_)existing_modules.insert(entry.first);
    std::unordered_set<std::string> existing_lambdas;for(const auto& entry:lambda_instances_)existing_lambdas.insert(entry.first);
    std::unordered_set<std::string> existing_structs;for(const auto& entry:struct_instances_)existing_structs.insert(entry.first);
    const auto existing_mutexes=snapshot_keys(mutex_instances_);
    const auto existing_threads=snapshot_keys(thread_instances_);
    const auto existing_asyncs=snapshot_keys(async_instances_);
    const auto existing_atomics=snapshot_keys(atomic_instances_);
    const auto existing_timers=snapshot_keys(timer_instances_);
    const auto existing_ffi_libraries=snapshot_keys(ffi_libraries_);
    const auto existing_ffi_pointers=snapshot_keys(ffi_pointers_);
    const auto existing_ffi_buffers=snapshot_keys(ffi_buffers_);
    const auto existing_ffi_callbacks=snapshot_keys(ffi_callbacks_i64_);
    const auto existing_collections=snapshot_keys(collection_instances_);
    const auto existing_streams=snapshot_keys(stream_instances_);
    const auto existing_commands=snapshot_keys(command_instances_);
    const std::uint64_t file_checkpoint=begin_file_operation();
    std::unordered_set<const Callable*> existing_prepared;for(const auto& entry:prepared_callables_)existing_prepared.insert(entry.first);
    bool import_committed=false;
    auto rollback=std::unique_ptr<void,std::function<void(void*)>>(reinterpret_cast<void*>(1),[&](void*){
        if(import_committed)return;
        for(auto it=module_envs_.begin();it!=module_envs_.end();)if(!existing_modules.count(it->first))it=module_envs_.erase(it);else ++it;
        for(auto it=lambda_instances_.begin();it!=lambda_instances_.end();)if(!existing_lambdas.count(it->first))it=lambda_instances_.erase(it);else ++it;
        for(auto it=struct_instances_.begin();it!=struct_instances_.end();)if(!existing_structs.count(it->first))it=struct_instances_.erase(it);else ++it;
        auto erase_new=[](auto& values,const auto& existing){for(auto it=values.begin();it!=values.end();)if(!existing.count(it->first))it=values.erase(it);else ++it;};
        erase_new(mutex_instances_,existing_mutexes);erase_new(atomic_instances_,existing_atomics);erase_new(timer_instances_,existing_timers);
        erase_new(ffi_libraries_,existing_ffi_libraries);erase_new(ffi_pointers_,existing_ffi_pointers);erase_new(ffi_buffers_,existing_ffi_buffers);erase_new(ffi_callbacks_i64_,existing_ffi_callbacks);
        erase_new(collection_instances_,existing_collections);erase_new(stream_instances_,existing_streams);erase_new(command_instances_,existing_commands);
        rollback_file_operation(file_checkpoint);
        for(auto it=prepared_callables_.begin();it!=prepared_callables_.end();)if(!existing_prepared.count(it->first))it=prepared_callables_.erase(it);else ++it;
        next_module_identity_=module_identity_checkpoint;next_lambda_instance_id_=lambda_identity_checkpoint;next_struct_instance_id_=struct_identity_checkpoint;next_runtime_temporary_id_=runtime_temporary_checkpoint;next_timer_instance_id_=timer_identity_checkpoint;next_ffi_library_id_=ffi_library_checkpoint;next_ffi_pointer_id_=ffi_pointer_checkpoint;next_ffi_buffer_id_=ffi_buffer_checkpoint;next_ffi_callback_id_=ffi_callback_checkpoint;next_collection_instance_id_=collection_checkpoint;next_stream_instance_id_=stream_checkpoint;next_file_instance_id_=file_identity_checkpoint;next_command_instance_id_=command_checkpoint;
    });

    auto saved_scopes=std::move(variable_scopes_); auto saved_callables=std::move(callables_); auto saved_structs=std::move(structs_);
    auto saved_exports=std::move(requested_exports_); const bool saved_import=in_import_program_; auto saved_control=pending_control_; const bool saved_strict=strict_script_mode_;
    auto saved_active=std::move(active_module_env_); auto saved_loading=std::move(loading_module_env_);
    auto module_env=std::make_shared<ModuleEnv>();module_env->identity=next_module_identity_++;module_env->source_path=path;module_env->source_provenance=SourceProvenance::FileBacked;module_env->import_base=path.parent_path();module_env->package_root=package_root;module_env->package_provenance=package_provenance;
    variable_scopes_.clear(); variable_scopes_.emplace_back(); callables_.clear(); structs_.clear(); requested_exports_.clear(); pending_control_={}; in_import_program_=true; strict_script_mode_=true;
    active_module_env_.reset();loading_module_env_=module_env;
    std::unordered_set<std::string> pre_import_files; for(const auto& kv:file_instances_)pre_import_files.insert(kv.first);
    input_stack_.push_back(import_identity); result_.dependencies.insert(host_.relative(path)); ++function_call_depth_;
    auto rr=execute_native_program(*src.content,path,depth+1,SourceProvenance::FileBacked);
    --function_call_depth_; input_stack_.pop_back();
    const bool import_created_workers=owned_thread_instances_.size()>owned_threads_checkpoint||owned_async_instances_.size()>owned_asyncs_checkpoint;
    auto defer_import_cleanup=[&]{
        if(import_committed)return;
        import_committed=true;
        deferred_worker_cleanups_.push_back([this,existing_modules,existing_lambdas,existing_structs,existing_mutexes,existing_threads,existing_asyncs,existing_atomics,existing_timers,existing_ffi_libraries,existing_ffi_pointers,existing_ffi_buffers,existing_ffi_callbacks,existing_collections,existing_streams,existing_commands,existing_prepared,file_checkpoint,module_identity_checkpoint,lambda_identity_checkpoint,struct_identity_checkpoint,runtime_temporary_checkpoint,timer_identity_checkpoint,ffi_library_checkpoint,ffi_pointer_checkpoint,ffi_buffer_checkpoint,ffi_callback_checkpoint,collection_checkpoint,stream_checkpoint,file_identity_checkpoint,command_checkpoint]{
            auto erase_new=[](auto& values,const auto& existing){for(auto it=values.begin();it!=values.end();)if(!existing.count(it->first))it=values.erase(it);else ++it;};
            erase_new(module_envs_,existing_modules);erase_new(lambda_instances_,existing_lambdas);erase_new(struct_instances_,existing_structs);erase_new(mutex_instances_,existing_mutexes);erase_new(thread_instances_,existing_threads);erase_new(async_instances_,existing_asyncs);erase_new(atomic_instances_,existing_atomics);erase_new(timer_instances_,existing_timers);erase_new(ffi_libraries_,existing_ffi_libraries);erase_new(ffi_pointers_,existing_ffi_pointers);erase_new(ffi_buffers_,existing_ffi_buffers);erase_new(ffi_callbacks_i64_,existing_ffi_callbacks);erase_new(collection_instances_,existing_collections);erase_new(stream_instances_,existing_streams);erase_new(command_instances_,existing_commands);
            rollback_file_operation(file_checkpoint);for(auto it=prepared_callables_.begin();it!=prepared_callables_.end();)if(!existing_prepared.count(it->first))it=prepared_callables_.erase(it);else ++it;
            next_module_identity_=module_identity_checkpoint;next_lambda_instance_id_=lambda_identity_checkpoint;next_struct_instance_id_=struct_identity_checkpoint;next_runtime_temporary_id_=runtime_temporary_checkpoint;next_timer_instance_id_=timer_identity_checkpoint;next_ffi_library_id_=ffi_library_checkpoint;next_ffi_pointer_id_=ffi_pointer_checkpoint;next_ffi_buffer_id_=ffi_buffer_checkpoint;next_ffi_callback_id_=ffi_callback_checkpoint;next_collection_instance_id_=collection_checkpoint;next_stream_instance_id_=stream_checkpoint;next_file_instance_id_=file_identity_checkpoint;next_command_instance_id_=command_checkpoint;
        });
    };
    auto fail_worker_import=[&](const std::string& detail,nift::detail::DiagnosticOrigin origin={}){
        active_recoverable_.reset();
        auto diagnostic=nift::detail::make_diagnostic(nift::detail::DiagnosticCode::InternalImportLifecycle,"failed import created worker resources: "+detail,std::move(origin));
        diagnostic.frames.push_back({nift::detail::DiagnosticFrameKind::Import,path.generic_string(),{},legacy_syntax?"@import: ":"import: "});
        error=diagnostic.message;active_diagnostic_=std::move(diagnostic);defer_import_cleanup();
    };
    if(rr.ok){for(auto& kv:file_instances_)if(!pre_import_files.count(kv.first)&&kv.second->open){auto f=kv.second;f->open=false;f->dirty=false;f->mode.clear();f->working.clear();f->saved.clear();f->cursor=0;rr.ok=false;rr.error.message="managed file left open at "+std::string(legacy_syntax?"@import":"import")+" completion: "+f->path.generic_string();break;}}
    auto isolated_scope=std::move(variable_scopes_.back()); auto isolated_callables=std::move(callables_); auto isolated_structs=std::move(structs_); auto exports=requested_exports_; auto completion=pending_control_;
    variable_scopes_=std::move(saved_scopes); callables_=std::move(saved_callables); structs_=std::move(saved_structs); requested_exports_=std::move(saved_exports); in_import_program_=saved_import; pending_control_=saved_control; strict_script_mode_=saved_strict;active_module_env_=std::move(saved_active);loading_module_env_=std::move(saved_loading);
    if(!rr.ok){if(import_created_workers){fail_worker_import(rr.error.message,rr.diagnostic?rr.diagnostic->origin:nift::detail::DiagnosticOrigin{});return false;}error=rr.error.message;if(rr.diagnostic){auto diagnostic=*rr.diagnostic;diagnostic.frames.push_back({nift::detail::DiagnosticFrameKind::Import,path.generic_string(),{},legacy_syntax?"@import: ":"import: "});active_diagnostic_=std::move(diagnostic);}return false;}
    if(completion.kind==ControlFlow::Return && completion.value){const std::string detail="return with a value is not allowed in "+std::string(legacy_syntax?"@import":"import");if(import_created_workers){fail_worker_import(detail);return false;}error=detail;return false;}

    std::unordered_map<std::string,VariableBinding> vars;
    std::unordered_map<std::string,Callable> funcs;
    std::unordered_map<std::string,StructDefinition> types;
    const auto destination_module=active_module_env_;
    for(const auto& name:exports){
        bool collision=destination_module&&(destination_module->vars.count(name)||destination_module->callables.count(name));
        if(!destination_module)for(auto it=variable_scopes_.rbegin();it!=variable_scopes_.rend();++it)if(it->count(name)){collision=true;break;}
        if(collision||(!destination_module&&(callables_.count(name)||structs_.count(name)))){const std::string detail="export collides with existing binding: "+name;if(import_created_workers){fail_worker_import(detail);return false;}error=detail;return false;}
        auto vi=isolated_scope.find(name); if(vi!=isolated_scope.end()){
            // Exporting a struct instance must also make its type resolvable in
            // the importer so module-style values such as vips.resize(...) work.
            if(vi->second.value && vi->second.value->is_string() && vi->second.value->string.rfind("\x1fnift:struct:",0)==0){
                auto ii=struct_instances_.find(vi->second.value->string.substr(13));
                if(ii!=struct_instances_.end()){
                    auto sit=isolated_structs.find(ii->second->type_name);
                    if(sit!=isolated_structs.end() && !types.count(ii->second->type_name)) types.emplace(ii->second->type_name, sit->second);
                }
            }
            vars.emplace(name,vi->second);continue;
        }
        auto fi=isolated_callables.find(name); if(fi!=isolated_callables.end()){funcs.emplace(name,fi->second);continue;}
        auto si=isolated_structs.find(name); if(si!=isolated_structs.end()){types.emplace(name,si->second);continue;}
        const std::string detail="export names no existing binding: "+name;
        if(import_created_workers){fail_worker_import(detail);return false;}
        error=detail;return false;
    }
    // Exported callables and struct methods may reference the module's private
    // callables and top-level bindings. Attach a shared module environment so
    // those references resolve while the module stays isolated from the importer.
    module_env->callables = isolated_callables;
    module_env->vars = isolated_scope;
    // Every facade the module keeps (exported or private) dispatches through
    // the StructDefinition stored on its instance. Attach the module env to
    // those definitions so their methods resolve the module's private
    // bindings even when the instance is used cross-package.
    for(auto& sv : module_env->vars){
        const auto& vb=sv.second;
        if(vb.value && vb.value->is_string() && vb.value->string.rfind("\x1fnift:struct:",0)==0){
            auto ii=struct_instances_.find(vb.value->string.substr(13));
            if(ii!=struct_instances_.end() && ii->second->definition){
                if(!ii->second->definition->module_env) ii->second->definition->module_env = module_env;
                for(auto& m : ii->second->definition->methods) if(!m.second.callable.module_env)m.second.callable.module_env = module_env;
            }
        }
    }
    for(auto& callable:module_env->callables)if(callable.second.module_env==module_env)callable.second.module_env.reset();
    if (!funcs.empty() || !types.empty()) {
        for (auto& kv : funcs) if(!kv.second.module_env)kv.second.module_env = module_env;
        for (auto& kv : types) {
            if(!kv.second.module_env)kv.second.module_env = module_env;
            for (auto& m : kv.second.methods) if(!m.second.callable.module_env)m.second.callable.module_env = kv.second.module_env;
        }
    }
    module_envs_[module_env->identity]=module_env;
    // Install only after the complete export set has validated.
    if(destination_module){
        if(!types.empty()){const std::string detail="struct exports cannot be imported from an active module callable";if(import_created_workers){fail_worker_import(detail);return false;}module_envs_.erase(module_env->identity);error=detail;return false;}
        for(auto& kv:vars){destination_module->vars.emplace(kv.first,kv.second);if(variable_scopes_.size()>1)variable_scopes_[1].emplace(kv.first,std::move(kv.second));}
        for(auto& kv:funcs)destination_module->callables.emplace(kv.first,std::move(kv.second));
    }else{
        auto& dst=variable_scopes_.back(); for(auto& kv:vars)dst.emplace(kv.first,std::move(kv.second));
        for(auto& kv:funcs) callables_.emplace(kv.first,std::move(kv.second));
        for(auto& kv:types) structs_.emplace(kv.first,std::move(kv.second));
    }
    import_committed=true;
    return true;
}
