#include "Ast.h"
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace nift::ast {
namespace {
struct P {
 const std::string& s; std::size_t p=0; std::string error;
 void ws(){while(p<s.size()&&std::isspace((unsigned char)s[p]))++p;}
 bool take(const std::string& x){ws();if(s.compare(p,x.size(),x)==0){p+=x.size();return true;}return false;}
 std::unique_ptr<Expr> node(Kind k,std::size_t b){auto n=std::make_unique<Expr>();n->kind=k;n->span={b,p};return n;}
 std::unique_ptr<Expr> primary(){ws();auto b=p;if(p>=s.size()){error="expected expression";return{};}
   if(s[p]=='('){++p;auto n=logical_or();ws();if(p>=s.size()||s[p]!=')'){error="expected ')'";return{};}++p;if(n)n->span={b,p};return n;}
   if(s[p]=='\''||s[p]=='\"'){char q=s[p++];std::string v;while(p<s.size()&&s[p]!=q){if(s[p]=='\\'&&p+1<s.size()){char e=s[++p];v+=e=='n'?'\n':e=='t'?'\t':e;}else v+=s[p];++p;}if(p>=s.size()){error="unterminated string";return{};}++p;auto n=node(Kind::Literal,b);n->literal=json::Document(v);return n;}
   if(std::isdigit((unsigned char)s[p])||(s[p]=='.'&&p+1<s.size()&&std::isdigit((unsigned char)s[p+1]))){char* e=nullptr;double v=std::strtod(s.c_str()+p,&e);if(e==s.c_str()+p){error="bad number";return{};}p=(std::size_t)(e-s.c_str());
     // Big integer literals lose precision as doubles (strtod), which breaks
     // comparisons and arithmetic (e.g. 9007199254740993 == 9007199254740992).
     // Nift stores large integers exactly as StrNumber in the legacy evaluator;
     // reject them here so the whole expression falls back to legacy semantics.
     {bool integral=std::isdigit((unsigned char)s[b])||(s[b]=='-'&&b+1<s.size()&&std::isdigit((unsigned char)s[b+1]));if(integral){for(std::size_t z=(s[b]=='-'?b+1:b);z<(std::size_t)(e-s.c_str());++z)if(!std::isdigit((unsigned char)s[z])){integral=false;break;}if(integral){errno=0;char* le=nullptr;long long lv=std::strtoll(s.c_str()+b,&le,10);if(errno==ERANGE||(le==(e))&&(lv>9007199254740992LL||lv<-9007199254740992LL)){error="unsupported large integer literal";return{};}}}}
     auto n=node(Kind::Literal,b);n->literal=json::Document(v);return n;}
   if(std::isalpha((unsigned char)s[p])||s[p]=='_'){++p;while(p<s.size()&&(std::isalnum((unsigned char)s[p])||s[p]=='_'))++p;std::string id=s.substr(b,p-b);ws();if(p<s.size()&&s[p]=='('){int d=0;bool q=false;char qc=0;do{char ch=s[p++];if(q){if(ch=='\\'&&p<s.size())++p;else if(ch==qc)q=false;}else if(ch=='\''||ch=='\"'){q=true;qc=ch;}else if(ch=='(')++d;else if(ch==')')--d;}while(p<s.size()&&d>0);if(d!=0){error="unterminated call";return{};}auto n=node(Kind::Call,b);n->text=s.substr(b,p-b);return n;}auto n=node(Kind::Binding,b);n->name=id;if(id=="true"){n->kind=Kind::Literal;n->literal=json::Document(true);}else if(id=="false"){n->kind=Kind::Literal;n->literal=json::Document(false);}else if(id=="null"){n->kind=Kind::Literal;n->literal=json::Document(nullptr);}return n;}
   error="unsupported primary";return{};
 }
 std::unique_ptr<Expr> postfix(){auto l=primary();if(!l)return{};while(true){ws();if(p<s.size()&&s[p]=='.'){auto b=l->span.begin;++p;ws();auto st=p;if(p>=s.size()||!(std::isalpha((unsigned char)s[p])||s[p]=='_')){error="expected member name";return{};}++p;while(p<s.size()&&(std::isalnum((unsigned char)s[p])||s[p]=='_'))++p;auto n=node(Kind::Member,b);n->name=s.substr(st,p-st);n->left=std::move(l);l=std::move(n);continue;}if(p<s.size()&&s[p]=='['){auto b=l->span.begin;++p;auto idx=logical_or();ws();if(!idx||p>=s.size()||s[p]!=']'){error="expected ']'";return{};}++p;auto n=node(Kind::Index,b);n->left=std::move(l);n->right=std::move(idx);l=std::move(n);continue;}break;}return l;}
 std::unique_ptr<Expr> unary(){ws();auto b=p;if(take("!")){auto r=unary();if(!r)return{};auto n=node(Kind::Unary,b);n->op="!";n->right=std::move(r);return n;}if(take("-")){auto r=unary();if(!r)return{};auto n=node(Kind::Unary,b);n->op="-";n->right=std::move(r);return n;}if(take("+")){auto r=unary();if(!r)return{};auto n=node(Kind::Unary,b);n->op="+";n->right=std::move(r);return n;}return postfix();}
 template<class Next> std::unique_ptr<Expr> chain(Next next,const std::initializer_list<const char*> ops){auto l=(this->*next)();if(!l)return{};while(true){ws();std::string op;for(auto x:ops)if(s.compare(p,std::char_traits<char>::length(x),x)==0){op=x;break;}if(op.empty())break;auto b=l->span.begin;p+=op.size();auto r=(this->*next)();if(!r)return{};auto n=node(Kind::Binary,b);n->op=op;n->left=std::move(l);n->right=std::move(r);l=std::move(n);}return l;}
 std::unique_ptr<Expr> mul(){return chain(&P::unary,{"*","/","%"});}
 std::unique_ptr<Expr> add(){return chain(&P::mul,{"+","-"});}
 std::unique_ptr<Expr> compare(){auto l=add();if(!l)return{};ws();for(auto op:{"==","!=","<=",">=","<",">"}){std::string x=op;if(s.compare(p,x.size(),x)==0){auto b=l->span.begin;p+=x.size();auto r=add();if(!r)return{};auto n=node(Kind::Binary,b);n->op=x;n->left=std::move(l);n->right=std::move(r);return n;}}return l;}
 std::unique_ptr<Expr> logical_and(){auto l=compare();if(!l)return{};while(take("&&")){auto b=l->span.begin;auto r=compare();if(!r)return{};auto n=node(Kind::Logical,b);n->op="&&";n->left=std::move(l);n->right=std::move(r);l=std::move(n);}return l;}
 std::unique_ptr<Expr> logical_or(){auto l=logical_and();if(!l)return{};while(take("||")){auto b=l->span.begin;auto r=logical_and();if(!r)return{};auto n=node(Kind::Logical,b);n->op="||";n->left=std::move(l);n->right=std::move(r);l=std::move(n);}return l;}
};
// Resolve an Index/Member/Binding chain by reference so that collections are
// not deep-copied on every intermediate access (a[i] must not copy all of a).
bool eval_chain(const Expr& e,Context& c,std::shared_ptr<const json::Document>& out,std::string& error){
 if(e.kind==Kind::Binding){if(c.resolve_ref)return c.resolve_ref(e.name,out,error);json::Document v;if(!c.resolve(e.name,v,error))return false;out=std::make_shared<const json::Document>(std::move(v));return true;}
 if(e.kind==Kind::Member){std::shared_ptr<const json::Document> base;if(!eval_chain(*e.left,c,base,error))return false;if(!base->is_object()||!base->has(e.name)){error="value has no member: "+e.name;return false;}out=std::make_shared<const json::Document>((*base)[e.name]);return true;}
 if(e.kind==Kind::Index){std::shared_ptr<const json::Document> base;if(!eval_chain(*e.left,c,base,error))return false;json::Document i;if(!evaluate(*e.right,c,i,error))return false;if(base->is_array()&&i.is_number()&&i.num>=0&&std::trunc(i.num)==i.num&&(std::size_t)i.num<base->array.size()){out=std::make_shared<const json::Document>(base->array[(std::size_t)i.num]);return true;}if(base->is_object()&&i.is_string()&&base->has(i.string)){out=std::make_shared<const json::Document>((*base)[i.string]);return true;}error="invalid index";return false;}
 json::Document v;if(!evaluate(e,c,v,error))return false;out=std::make_shared<const json::Document>(std::move(v));return true;}
bool eq(const json::Document&a,const json::Document&b){if(a.is_number()&&b.is_number())return a.num==b.num;if(a.type!=b.type)return false;if(a.is_null())return true;if(a.is_bool())return a.boolean==b.boolean;if(a.is_string())return a.string==b.string;if(a.is_array()){if(a.array.size()!=b.array.size())return false;for(size_t i=0;i<a.array.size();++i)if(!eq(a.array[i],b.array[i]))return false;return true;}if(a.is_object()){if(a.object.size()!=b.object.size())return false;for(const auto&x:a.object){if(!b.has(x.first)||!eq(x.second,b[x.first]))return false;}return true;}return false;}
}

StatementParseResult parse_statement(const std::string& source){
 auto trim=[](std::string x){auto b=x.find_first_not_of(" \t\r\n");if(b==std::string::npos)return std::string();auto e=x.find_last_not_of(" \t\r\n");return x.substr(b,e-b+1);};
 std::string t=trim(source);auto st=std::make_unique<Stmt>();st->text=t;st->span={0,source.size()};if(t.empty())return{std::move(st),{},false};
 if(t=="break"){st->kind=StmtKind::Break;return{std::move(st),{},true};} if(t=="continue"){st->kind=StmtKind::Continue;return{std::move(st),{},true};}
 if(t.rfind("return",0)==0&&(t.size()==6||std::isspace((unsigned char)t[6])||t[6]=='(')){std::string x=trim(t.substr(6));if(!x.empty()&&x.front()=='('&&x.back()==')')x=trim(x.substr(1,x.size()-2));st->kind=StmtKind::Return;if(!x.empty()){auto r=parse_expression(x);if(!r.supported)return{std::move(st),r.error,false};st->expr=std::move(r.expr);}return{std::move(st),{},true};}

 for(auto kw:{std::string("if"),std::string("while")}){if(t.rfind(kw+"(",0)==0){int d=0;std::size_t close=std::string::npos;for(std::size_t i=kw.size();i<t.size();++i){if(t[i]=='(')++d;else if(t[i]==')'&&--d==0){close=i;break;}}if(close!=std::string::npos){auto r=parse_expression(t.substr(kw.size()+1,close-kw.size()-1));if(r.supported){st->kind=kw=="if"?StmtKind::If:StmtKind::While;st->condition=std::move(r.expr);return{std::move(st),{},true};}}}}
 if(t.rfind("for(",0)==0){int d=0;std::size_t close=std::string::npos;for(std::size_t i=3;i<t.size();++i){if(t[i]=='(')++d;else if(t[i]==')'&&--d==0){close=i;break;}}if(close!=std::string::npos){auto h=trim(t.substr(4,close-4));int dep=0;bool q=false;char qc=0;std::size_t sep=std::string::npos;for(std::size_t i=0;i<h.size();++i){char c=h[i];if(q){if(c=='\\')++i;else if(c==qc)q=false;continue;}if(c=='\''||c=='"'){q=true;qc=c;}else if(c=='('||c=='[')++dep;else if(c==')'||c==']')--dep;else if(c==':'&&!dep){sep=i;break;}}if(sep!=std::string::npos){auto n=trim(h.substr(0,sep));auto r=parse_expression(trim(h.substr(sep+1)));if(!n.empty()&&r.supported){st->kind=StmtKind::For;st->name=n;st->iterable=std::move(r.expr);return{std::move(st),{},true};}}}}
 auto ident=[](const std::string&x){if(x.empty()||!(std::isalpha((unsigned char)x[0])||x[0]=='_'))return false;for(char c:x)if(!(std::isalnum((unsigned char)c)||c=='_'))return false;return true;};
 for(const auto& op:{std::string("+="),std::string("-="),std::string("*="),std::string("/="),std::string("%=")}){auto p=t.find(op);if(p!=std::string::npos){auto n=trim(t.substr(0,p));if(!ident(n))break;auto r=parse_expression(t.substr(p+2));if(!r.supported)return{std::move(st),r.error,false};st->kind=StmtKind::CompoundAssignment;st->name=n;st->op=op;st->expr=std::move(r.expr);return{std::move(st),{},true};}}
 if(t.size()>2&&(t.substr(0,2)=="++"||t.substr(0,2)=="--")){auto n=trim(t.substr(2));if(ident(n)){st->kind=StmtKind::Increment;st->name=n;st->op=t.substr(0,2);return{std::move(st),{},true};}}
 if(t.size()>2&&(t.substr(t.size()-2)=="++"||t.substr(t.size()-2)=="--")){auto n=trim(t.substr(0,t.size()-2));if(ident(n)){st->kind=StmtKind::Increment;st->name=n;st->op=t.substr(t.size()-2);return{std::move(st),{},true};}}
 auto dp=t.find(":=");if(dp!=std::string::npos){auto lhs=trim(t.substr(0,dp));auto sp=lhs.find_last_of(" \t");auto n=sp==std::string::npos?lhs:trim(lhs.substr(sp+1));if(ident(n)){auto r=parse_expression(t.substr(dp+2));if(r.supported){st->kind=StmtKind::Declaration;st->name=n;st->expr=std::move(r.expr);return{std::move(st),{},true};}}}
 for(std::size_t p=0;p<t.size();++p)if(t[p]=='='&&(p==0||std::string("=!<>").find(t[p-1])==std::string::npos)&&(p+1==t.size()||t[p+1]!='=')){auto n=trim(t.substr(0,p));if(ident(n)){auto r=parse_expression(t.substr(p+1));if(r.supported){st->kind=StmtKind::Assignment;st->name=n;st->expr=std::move(r.expr);return{std::move(st),{},true};}}break;}
 auto r=parse_expression(t);if(r.supported){st->kind=StmtKind::Expression;st->expr=std::move(r.expr);return{std::move(st),{},true};}return{std::move(st),r.error,false};
}
ParseResult parse_expression(const std::string& source){P p{source};auto e=p.logical_or();p.ws();if(!e||p.p!=source.size())return{{},p.error.empty()?"unsupported expression":p.error,false};return{std::move(e),{},true};}
bool truthy(const json::Document& d){if(d.is_bool())return d.boolean;if(d.is_null())return false;if(d.is_number())return d.num!=0;if(d.is_string())return !d.string.empty();if(d.is_array())return !d.array.empty();if(d.is_object())return !d.object.empty();return false;}
bool evaluate(const Expr& e,Context& c,json::Document& out,std::string& error){
 if(e.kind==Kind::Literal){out=e.literal;return true;}if(e.kind==Kind::Binding)return c.resolve(e.name,out,error);if(e.kind==Kind::Call||e.kind==Kind::Legacy)return c.legacy(e.text,out,error);
 if(e.kind==Kind::Member){std::shared_ptr<const json::Document> b;if(!eval_chain(*e.left,c,b,error))return false;if(!b->is_object()||!b->has(e.name)){error="value has no member: "+e.name;return false;}out=(*b)[e.name];return true;}
 if(e.kind==Kind::Index){std::shared_ptr<const json::Document> b;if(!eval_chain(*e.left,c,b,error))return false;json::Document i;if(!evaluate(*e.right,c,i,error))return false;if(b->is_array()&&i.is_number()&&i.num>=0&&std::trunc(i.num)==i.num&&(std::size_t)i.num<b->array.size()){out=b->array[(std::size_t)i.num];return true;}if(b->is_object()&&i.is_string()&&b->has(i.string)){out=(*b)[i.string];return true;}error="invalid index";return false;}
 if(e.kind==Kind::Unary){json::Document r;if(!evaluate(*e.right,c,r,error))return false;if(e.op=="!"){out=json::Document(!truthy(r));return true;}if(!r.is_number()){error="unary arithmetic operators require a numeric operand";return false;}out=json::Document(e.op=="-"?-r.num:r.num);return true;}
 if(e.kind==Kind::Logical){json::Document l;if(!evaluate(*e.left,c,l,error))return false;if(e.op=="&&"&&!truthy(l)){out=json::Document(false);return true;}if(e.op=="||"&&truthy(l)){out=json::Document(true);return true;}json::Document r;if(!evaluate(*e.right,c,r,error))return false;out=json::Document(truthy(r));return true;}
 if(e.kind==Kind::Binary){json::Document l,r;if(!evaluate(*e.left,c,l,error)||!evaluate(*e.right,c,r,error))return false;const auto&o=e.op;if(o=="=="||o=="!="){bool v=eq(l,r);out=json::Document(o=="=="?v:!v);return true;}if(o=="<"||o=="<="||o==">"||o==">="){if(l.is_number()&&r.is_number()){bool v=o=="<"?l.num<r.num:o=="<="?l.num<=r.num:o==">"?l.num>r.num:l.num>=r.num;out=json::Document(v);return true;}if(l.is_string()&&r.is_string()){bool v=o=="<"?l.string<r.string:o=="<="?l.string<=r.string:o==">"?l.string>r.string:l.string>=r.string;out=json::Document(v);return true;}error="ordering comparisons require two numbers or two strings";return false;}
   if(o=="+"&&(l.is_string()||r.is_string())){if(!c.render){error="string rendering unavailable";return false;}out=json::Document(c.render(l)+c.render(r));return true;}if(!l.is_number()||!r.is_number()){error="arithmetic operators require numeric operands";return false;}if(o=="+")out=json::Document(l.num+r.num);else if(o=="-")out=json::Document(l.num-r.num);else if(o=="*")out=json::Document(l.num*r.num);else if(o=="/"){if(r.num==0){error="division by zero";return false;}out=json::Document(l.num/r.num);}else if(o=="%"){if(r.num==0){error="modulo by zero";return false;}out=json::Document(std::fmod(l.num,r.num));}else return false;return true;}
 error="unsupported AST node";return false;
}
} // namespace nift::ast
