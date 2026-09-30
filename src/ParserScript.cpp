#include "Parser.h"
#include "ParserHelpers.h"
#include "JsonFile.h"
#include "Console.h"
#include "FileSystem.h"

#include <nift/context.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>

namespace fs = std::filesystem;

using nift::detail::valid_binding_identifier;
using nift::detail::strip_presentation_chain;
using nift::detail::nift_binding_type;

bool Parser::translate_function_program(const std::string& source, std::string& translated, std::string& error) const {
    std::function<bool(const std::string&, std::string&)> convert;
    convert = [&](const std::string& in, std::string& out) -> bool {
        std::size_t i=0;
        auto boundary=[&](std::size_t p,const std::string& kw){return in.compare(p,kw.size(),kw)==0 && (p+kw.size()==in.size() || (!std::isalnum((unsigned char)in[p+kw.size()]) && in[p+kw.size()]!='_'));};
        while(i<in.size()) {
            while(i<in.size() && std::isspace((unsigned char)in[i])) ++i;
            if(i>=in.size()) break;
            if(boundary(i,"fn")) {
                std::size_t p=i+2; while(p<in.size()&&std::isspace((unsigned char)in[p]))++p; bool async=false;
                if(p+7<=in.size() && in.compare(p,7,"[async]")==0){async=true;p+=7;while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;}
                std::size_t pc=0;
                if(p>=in.size()||in[p]!='('||!find_balanced(in,p,'(',')',pc)){error="fn requires '(name(args))'";return false;}
                std::size_t bo=pc+1; while(bo<in.size()&&std::isspace((unsigned char)in[bo]))++bo; std::size_t bc=0;
                if(bo>=in.size()||in[bo]!='{'||!find_balanced(in,bo,'{','}',bc)){error="fn requires a block";return false;}
                out += async?"@fn[async](":"@fn("; out+=in.substr(p+1,pc-p-1)+"){"+in.substr(bo+1,bc-bo-1)+"}"; i=bc+1; continue;
            }
            if(boundary(i,"enum")) {
                std::size_t p=i+4;while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;std::size_t ns=p;while(p<in.size()&&(std::isalnum((unsigned char)in[p])||in[p]=='_'))++p;std::string name=in.substr(ns,p-ns);while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;std::size_t bc=0;
                if(name.empty()||p>=in.size()||in[p]!='{'||!find_balanced(in,p,'{','}',bc)){error="enum requires 'Name { members }'";return false;}out+="@enum("+name+"){"+in.substr(p+1,bc-p-1)+"}";i=bc+1;continue;
            }
            if(boundary(i,"struct")) {
                std::size_t p=i+6; while(p<in.size()&&std::isspace((unsigned char)in[p]))++p; std::size_t pc=0;
                if(p>=in.size()||in[p]!='('||!find_balanced(in,p,'(',')',pc)){error="struct requires '(name)'";return false;}
                std::size_t bo=pc+1; while(bo<in.size()&&std::isspace((unsigned char)in[bo]))++bo; std::size_t bc=0;
                if(bo>=in.size()||in[bo]!='{'||!find_balanced(in,bo,'{','}',bc)){error="struct requires a block";return false;}
                out += "@struct("+in.substr(p+1,pc-p-1)+"){"+in.substr(bo+1,bc-bo-1)+"}"; i=bc+1; continue;
            }
            if(boundary(i,"export")) {
                std::size_t p=i+6; while(p<in.size()&&std::isspace((unsigned char)in[p]))++p; std::size_t pc=0;
                if(p>=in.size()||in[p]!='('||!find_balanced(in,p,'(',')',pc)){error="export requires '(binding)'";return false;}
                out += "@__export("+in.substr(p+1,pc-p-1)+")"; i=pc+1; if(i<in.size()&&in[i]==';')++i; continue;
            }
            if(boundary(i,"import")) {
                std::size_t p=i+6; while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;
                if(p<in.size()&&in[p]=='('){const auto source_lines=std::count(in.begin(),in.begin()+i,'\n');const auto output_lines=std::count(out.begin(),out.end(),'\n');if(output_lines<source_lines)out.append(source_lines-output_lines,'\n');const auto line_start=in.rfind('\n',i);const auto indent_start=line_start==std::string::npos?0:line_start+1;if(std::all_of(in.begin()+indent_start,in.begin()+i,[](char c){return c==' '||c=='\t';}))out+=in.substr(indent_start,i-indent_start);std::size_t pc=0;if(!find_balanced(in,p,'(',')',pc)){out+=in.substr(i);return true;}out+=in.substr(i,pc-i+1);i=pc+1;if(i<in.size()&&in[i]==';')++i;continue;}
            }
            // A single-line @// comment must be consumed to end-of-line BEFORE
            // statement splitting, so a ';' inside the comment cannot turn the
            // rest of the comment into a statement.
            if (in.compare(i, 3, "@//") == 0) {
                std::size_t line_end = in.find('\n', i);
                out += '\n';
                i = (line_end == std::string::npos) ? in.size() : line_end;
                continue;
            }
            // Script-land single-line comment without the template '@' prefix.
            if (in.compare(i, 2, "//") == 0) {
                std::size_t line_end = in.find('\n', i);
                out += '\n';
                i = (line_end == std::string::npos) ? in.size() : line_end;
                continue;
            }
            if (in.compare(i, 3, "@/*") == 0) {
                std::size_t block_end = in.find("*/", i + 3);
                if (block_end == std::string::npos) { error = "open comment '@/*' has no close '*/'"; return false; }
                i = block_end + 2;
                continue;
            }
            // Script-land block comment without the template '@' prefix.
            if (in.compare(i, 2, "/*") == 0) {
                std::size_t block_end = in.find("*/", i + 2);
                if (block_end == std::string::npos) { error = "open comment '/*' has no close '*/'"; return false; }
                const auto line_count=std::count(in.begin()+i,in.begin()+block_end+2,'\n');
                if(line_count)out.append(line_count,'\n');else out+=' ';
                i = block_end + 2;
                continue;
            }
            if(boundary(i,"while")) {
                std::size_t p=i+5;while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;std::size_t pc=0;
                if(p>=in.size()||in[p]!='('||!find_balanced(in,p,'(',')',pc)){error="function while requires '(...)'";return false;}
                std::size_t bo=pc+1;while(bo<in.size()&&std::isspace((unsigned char)in[bo]))++bo;std::size_t bc=0;
                if(bo>=in.size()||in[bo]!='{'||!find_balanced(in,bo,'{','}',bc)){error="function while requires a block";return false;}
                std::string body;if(!convert(in.substr(bo+1,bc-bo-1),body))return false;out+="@while("+in.substr(p+1,pc-p-1)+"){"+body+"}";i=bc+1;continue;
            }
            if(boundary(i,"for")) {
                std::size_t p=i+3; while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;
                std::size_t pc=0; if(p>=in.size()||in[p]!='('||!find_balanced(in,p,'(',')',pc)){error="function for requires '(...)'";return false;}
                std::size_t bo=pc+1;while(bo<in.size()&&std::isspace((unsigned char)in[bo]))++bo;std::size_t bc=0;
                if(bo>=in.size()||in[bo]!='{'||!find_balanced(in,bo,'{','}',bc)){error="function for requires a block";return false;}
                std::string body;if(!convert(in.substr(bo+1,bc-bo-1),body))return false;
                out += "@for("+in.substr(p+1,pc-p-1)+"){"+body+"}";i=bc+1;continue;
            }
            if(boundary(i,"if")) {
                std::size_t p=i+2; while(p<in.size()&&std::isspace((unsigned char)in[p]))++p;
                if(p>=in.size()||in[p]!='('){error="function if requires '(...)'";return false;}
                std::size_t pc=0; if(!find_balanced(in,p,'(',')',pc)){error="function if has no matching ')'";return false;}
                std::size_t bo=pc+1; while(bo<in.size()&&std::isspace((unsigned char)in[bo]))++bo;
                std::size_t bc=0; if(bo>=in.size()||in[bo]!='{'||!find_balanced(in,bo,'{','}',bc)){error="function if requires a block";return false;}
                std::string body; if(!convert(in.substr(bo+1,bc-bo-1),body))return false;
                out += "@if("+in.substr(p+1,pc-p-1)+"){"+body+"}"; i=bc+1;
                while(true){std::size_t e=i;while(e<in.size()&&std::isspace((unsigned char)in[e]))++e;if(!boundary(e,"else"))break;e+=4;while(e<in.size()&&std::isspace((unsigned char)in[e]))++e;
                    if(boundary(e,"if")){std::size_t q=e+2;while(q<in.size()&&std::isspace((unsigned char)in[q]))++q;std::size_t qc=0;if(q>=in.size()||in[q]!='('||!find_balanced(in,q,'(',')',qc)){error="function else if malformed";return false;}std::size_t eb=qc+1;while(eb<in.size()&&std::isspace((unsigned char)in[eb]))++eb;std::size_t ec=0;if(eb>=in.size()||in[eb]!='{'||!find_balanced(in,eb,'{','}',ec)){error="function else if requires block";return false;}std::string body2;if(!convert(in.substr(eb+1,ec-eb-1),body2))return false;out+=" else if("+in.substr(q+1,qc-q-1)+"){"+body2+"}";i=ec+1;continue;}
                    std::size_t ec=0;if(e>=in.size()||in[e]!='{'||!find_balanced(in,e,'{','}',ec)){error="function else requires block";return false;}std::string body2;if(!convert(in.substr(e+1,ec-e-1),body2))return false;out+=" else {"+body2+"}";i=ec+1;break;}
                continue;
            }
            if(boundary(i,"return")) {
                std::size_t e=i+6; bool quoted=false;char quote=0;
                while(e<in.size()&&in[e]!='\n'&&in[e]!=';' ) { char c=in[e]; if(quoted){if(c=='\\'&&e+1<in.size())++e;else if(c==quote)quoted=false;}else if(c=='\''||c=='"'){quoted=true;quote=c;}++e; }
                std::string expr=trim_copy(in.substr(i+6,e-(i+6))); out += expr.empty()?"@__bare_return()":"@return("+expr+")"; i=e<in.size()?e+1:e; continue;
            }
            if(boundary(i,"break")) { std::size_t e=i+5; while(e<in.size()&&std::isspace((unsigned char)in[e])&&in[e]!='\n')++e; if(e==in.size()||in[e]==';'||in[e]=='\n') { out += "break"; i=e<in.size()?e+1:e; continue; } }
            if(boundary(i,"continue")) { std::size_t e=i+8; while(e<in.size()&&std::isspace((unsigned char)in[e])&&in[e]!='\n')++e; if(e==in.size()||in[e]==';'||in[e]=='\n') { out += "continue"; i=e<in.size()?e+1:e; continue; } }
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
                    out += "@//" + stmt.substr(2) + "\n";
                } else if(stmt[0]=='@'||stmt[0]=='$'){
                    out+=stmt;
                    // A verbatim single-line comment has no terminating newline
                    // once inter-statement whitespace is stripped; emit one so it
                    // cannot swallow the following statement.
                    if(stmt.rfind("@//",0)==0) out+='\n';
                } else {
                    // `await future` is a language expression even though it is
                    // a multi-token statement; never route it through shell-style
                    // external command dispatch.
                    if(stmt.rfind("await ",0)==0){ out += "$["+stmt+"]"; if(i<in.size())++i; continue; }
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
                    if(cmd_like&&!saw_delim&&pp==0&&bb==0&&cc2==0) out += "@__nift_cmd(" + stmt + ")";
                    else out+="$["+stmt+"]";
                }
            }
            if(i<in.size())++i;
        }
        return true;
    };
    translated.clear(); return convert(source,translated);
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
    if(!ok)return false;
    if(!nift::runtime_number_to_i64(result,out_value)){error="callback result must be an integer";return false;}return true;
}

bool Parser::invoke_callable(const std::string& name, const std::vector<nift::RuntimeValue>& args, nift::RuntimeValue& value, std::string& error) {
    if(!valid_binding_identifier(name)){error="invalid callable name";return false;}if(variable_scopes_.empty())variable_scopes_.emplace_back();std::vector<std::string> names;names.reserve(args.size());for(size_t i=0;i<args.size();++i){std::string n="__nift_host_arg_"+std::to_string(i);auto sp=std::make_shared<nift::RuntimeValue>(args[i]);variable_scopes_.back()[n]=VariableBinding{sp,nift_binding_type(*sp),false,false};names.push_back(n);}std::string expr=name+"(";for(size_t i=0;i<names.size();++i){if(i)expr+=",";expr+=names[i];}expr+=")";bool ok=evaluate_expression(expr,value,error);for(const auto& n:names)variable_scopes_.back().erase(n);return ok;
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

RenderResult Parser::run_script(const std::string& source, const fs::path& source_path) {
    result_ = RenderResult{};
    variable_scopes_.clear(); variable_scopes_.emplace_back();
    install_script_invocation_bindings();
    callables_.clear(); structs_.clear(); requested_exports_.clear(); pending_control_={};
    in_import_program_=false; standalone_script_host_=true; strict_script_mode_=true; function_call_depth_=1;
    auto rr=execute_native_program(source,source_path,0);
    function_call_depth_=0; strict_script_mode_=false;
    rr.output.clear();
    if(rr.ok && pending_control_.kind==ControlFlow::Return && pending_control_.value) {
        const auto& v=*pending_control_.value;
        if(v.is_bytes()||v.is_array()||v.is_object()||(v.is_string()&&v.string.rfind("\x1fnift:",0)==0)) { rr.ok=false; rr.error.message="script return value is not directly renderable"; }
        else rr.output=render_expression_value(v);
    }
    pending_control_={};
    std::string resource_error;
    if(!finalize_script_resources(resource_error) && rr.ok){rr.ok=false;rr.error.message=resource_error;}
    return rr;
}

bool Parser::run_embedded_script(const std::string& source, const fs::path& source_path, nift::RuntimeValue& value, std::string& error) {
    result_=RenderResult{};variable_scopes_.clear();variable_scopes_.emplace_back();install_script_invocation_bindings();callables_.clear();structs_.clear();requested_exports_.clear();pending_control_={};in_import_program_=false;standalone_script_host_=false;strict_script_mode_=true;function_call_depth_=1;
    auto rr=execute_native_program(source,source_path,0);function_call_depth_=0;strict_script_mode_=false;
    if(!rr.ok){error=rr.error.message;pending_control_={};return false;}
    value=nift::RuntimeValue(nullptr);if(pending_control_.kind==ControlFlow::Return&&pending_control_.value)value=*pending_control_.value;pending_control_={};std::string resource_error;if(!finalize_script_resources(resource_error)){error=resource_error;return false;}return true;
}

RenderResult Parser::run_statement(const std::string& source, const fs::path& source_path) {
    if(variable_scopes_.empty()) { variable_scopes_.emplace_back(); install_script_invocation_bindings(); }
    standalone_script_host_=true; strict_script_mode_=true; result_ = RenderResult{}; pending_control_={}; function_call_depth_=1;
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
        if(evaluate_expression(t,v,error)){
            RenderResult rr; std::string shown;
            // REPL commands commonly return null (for example cd()) and an empty
            // string has nothing useful to inspect. Keep both silent rather than
            // displaying `null` or an empty quoted string.
            if(v.is_null() || (v.is_string() && v.string.empty())) {
                function_call_depth_=0; strict_script_mode_=false; return rr;
            }
            // Presentation modifiers are composable and order-independent. The evaluator
            // has already serialized their original value into an ANSI-free string; here
            // the REPL only decides whether that representation should be highlighted.
            std::string presentation; bool presentation_method=false, highlight=false;
            { bool p=false,h=false; if(strip_presentation_chain(t,presentation,p,h)){presentation_method=true;highlight=h;} }
            if(presentation_method&&v.is_string()) shown=v.string;
            else if(!serialize_value(v,false,shown,error)){rr.ok=false;rr.error.message=error;function_call_depth_=0;strict_script_mode_=false;return rr;}
            if(highlight && console::stdout_colour_enabled()) shown=console::highlight_nift_value(shown);
            rr.output=shown; function_call_depth_=0; strict_script_mode_=false; return rr;
        }
        // If direct evaluation fails, let the native statement path provide the
        // canonical diagnostic; it may be a valid statement form not recognized above.
    }
    auto rr=execute_native_program(source,source_path,0); function_call_depth_=0; strict_script_mode_=false; pending_control_={}; return rr;
}

void Parser::reset_script_control() { pending_control_={}; result_=RenderResult{}; }

RenderResult Parser::execute_native_program(const std::string& source, const fs::path& source_path, int depth) {
    std::string program, error;
    if (!translate_function_program(source, program, error)) {
        RenderResult failed; failed.ok=false; failed.error.message=error; return failed;
    }
    auto rr=parse(program, source_path, depth);
    if(!rr.ok&&rr.error.line>0&&rr.error.message.rfind("import",0)==0){
        auto line_at=[](const std::string& text,std::size_t line){std::size_t begin=0;for(std::size_t n=1;n<line;++n){begin=text.find('\n',begin);if(begin==std::string::npos)return std::string{};++begin;}const auto end=text.find('\n',begin);return text.substr(begin,end==std::string::npos?std::string::npos:end-begin);};
        auto imports=[](const std::string& line){std::vector<std::size_t> found;bool quoted=false;char quote=0;for(std::size_t p=0;p<line.size();++p){const char c=line[p];if(quoted){if(c=='\\')++p;else if(c==quote)quoted=false;continue;}if(c=='\''||c=='"'){quoted=true;quote=c;continue;}if(line.compare(p,6,"import")!=0)continue;const bool left=p==0||(!std::isalnum(static_cast<unsigned char>(line[p-1]))&&line[p-1]!='_');std::size_t q=p+6;const bool right=q==line.size()||(!std::isalnum(static_cast<unsigned char>(line[q]))&&line[q]!='_');while(q<line.size()&&std::isspace(static_cast<unsigned char>(line[q])))++q;if(left&&right&&q<line.size()&&line[q]=='(')found.push_back(p);}return found;};
        const std::string original_line=line_at(source,rr.error.line);const auto original_imports=imports(original_line);const auto translated_imports=imports(rr.error.source_line);
        if(!original_imports.empty()){std::size_t occurrence=0;for(std::size_t n=0;n<translated_imports.size();++n)if(translated_imports[n]+1<=rr.error.column)occurrence=n;occurrence=std::min(occurrence,original_imports.size()-1);const auto position=original_imports[occurrence];rr.error.column=position+1;rr.error.source_line=original_line;std::size_t open=position+6;while(open<original_line.size()&&std::isspace(static_cast<unsigned char>(original_line[open])))++open;std::size_t close=0;rr.error.source_length=open<original_line.size()&&find_balanced(original_line,open,'(',')',close)?close-position+1:6;}
    }
    return rr;
}

bool Parser::execute_import_file(const std::string& argument, const fs::path& caller_path, int depth, bool legacy_syntax, std::string& error) {
    fs::path path=argument;
    const bool package_name = argument.find('/') == std::string::npos && argument.find('\\') == std::string::npos && fs::path(argument).extension().empty();
    if (package_name && standalone_script_host_) {
        fs::path root = fs::current_path()/".nift"/"packages"/argument;
        json::Document manifest; std::string me;
        if (load_json_file(root/"manifest.json", manifest, me) && manifest.is_object() && manifest.has("entry") && manifest["entry"].is_string()) path=root/manifest["entry"].string;
        else { error="package is not installed or has invalid manifest: "+argument; return false; }
    } else if(path.is_relative()) {
        if(standalone_script_host_ && caller_path == fs::path("<nift-sh>")) path=fs::current_path()/path;
        fs::path local=caller_path.parent_path()/path;
        if(!caller_path.parent_path().empty() && host_.source_exists(local)) path=local;
        else path=host_.root()/path;
    }
    path=fs::absolute(path).lexically_normal();
    if(!standalone_script_host_ && !host_.root().empty() && !filesystem::path_within(fs::absolute(host_.root()).lexically_normal(),path)){error="path must stay inside the Nift project";return false;}
    if(std::find(input_stack_.begin(),input_stack_.end(),path)!=input_stack_.end()){error="script import cycle through "+path.generic_string();return false;}
    auto src=host_.read_shared_source(path); if(src.status==nift::HostStatus::Error||!src.content){error=src.error.empty()?"script is not readable":src.error;return false;}

    auto saved_scopes=std::move(variable_scopes_); auto saved_callables=std::move(callables_); auto saved_structs=std::move(structs_);
    auto saved_exports=std::move(requested_exports_); const bool saved_import=in_import_program_; auto saved_control=pending_control_; const bool saved_strict=strict_script_mode_;
    variable_scopes_.clear(); variable_scopes_.emplace_back(); callables_.clear(); structs_.clear(); requested_exports_.clear(); pending_control_={}; in_import_program_=true; strict_script_mode_=true;
    std::unordered_set<std::string> pre_import_files; for(const auto& kv:file_instances_)pre_import_files.insert(kv.first);
    input_stack_.push_back(path); result_.dependencies.insert(host_.relative(path)); ++function_call_depth_;
    auto rr=execute_native_program(*src.content,path,depth+1);
    --function_call_depth_; input_stack_.pop_back();
    if(rr.ok){for(auto& kv:file_instances_)if(!pre_import_files.count(kv.first)&&kv.second->open){auto f=kv.second;f->open=false;f->dirty=false;f->mode.clear();f->working.clear();f->saved.clear();f->cursor=0;rr.ok=false;rr.error.message="managed file left open at "+std::string(legacy_syntax?"@import":"import")+" completion: "+f->path.generic_string();break;}}
    auto isolated_scope=std::move(variable_scopes_.back()); auto isolated_callables=std::move(callables_); auto isolated_structs=std::move(structs_); auto exports=requested_exports_; auto completion=pending_control_;
    variable_scopes_=std::move(saved_scopes); callables_=std::move(saved_callables); structs_=std::move(saved_structs); requested_exports_=std::move(saved_exports); in_import_program_=saved_import; pending_control_=saved_control; strict_script_mode_=saved_strict;
    if(!rr.ok){error=rr.error.message;return false;}
    if(completion.kind==ControlFlow::Return && completion.value){error="return with a value is not allowed in "+std::string(legacy_syntax?"@import":"import");return false;}

    std::unordered_map<std::string,VariableBinding> vars;
    std::unordered_map<std::string,Callable> funcs;
    std::unordered_map<std::string,StructDefinition> types;
    for(const auto& name:exports){
        bool collision=false; for(auto it=variable_scopes_.rbegin();it!=variable_scopes_.rend();++it) if(it->count(name)){collision=true;break;}
        if(collision||callables_.count(name)||structs_.count(name)){error="export collides with existing binding: "+name;return false;}
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
        error="export names no existing binding: "+name;return false;
    }
    // Exported callables and struct methods may reference the module's private
    // callables and top-level bindings. Attach a shared module environment so
    // those references resolve while the module stays isolated from the importer.
    if (!funcs.empty() || !types.empty()) {
        auto module_env = std::make_shared<ModuleEnv>();
        module_env->callables = isolated_callables;
        module_env->vars = isolated_scope;
        for (auto& kv : funcs) kv.second.module_env = module_env;
        for (auto& kv : types) {
            kv.second.module_env = module_env;
            for (auto& m : kv.second.methods) m.second.callable.module_env = module_env;
        }
    }
    // Install only after the complete export set has validated.
    auto& dst=variable_scopes_.back(); for(auto& kv:vars)dst.emplace(kv.first,std::move(kv.second));
    for(auto& kv:funcs) callables_.emplace(kv.first,std::move(kv.second));
    for(auto& kv:types) structs_.emplace(kv.first,std::move(kv.second));
    return true;
}
