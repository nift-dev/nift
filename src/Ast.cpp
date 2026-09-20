#include "Ast.h"
#include <cctype>
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
   if(std::isdigit((unsigned char)s[p])||(s[p]=='.'&&p+1<s.size()&&std::isdigit((unsigned char)s[p+1]))){char* e=nullptr;double v=std::strtod(s.c_str()+p,&e);if(e==s.c_str()+p){error="bad number";return{};}p=(std::size_t)(e-s.c_str());auto n=node(Kind::Literal,b);n->literal=json::Document(v);return n;}
   if(std::isalpha((unsigned char)s[p])||s[p]=='_'){++p;while(p<s.size()&&(std::isalnum((unsigned char)s[p])||s[p]=='_'))++p;std::string id=s.substr(b,p-b);auto n=node(Kind::Binding,b);n->name=id;if(id=="true"){n->kind=Kind::Literal;n->literal=json::Document(true);}else if(id=="false"){n->kind=Kind::Literal;n->literal=json::Document(false);}else if(id=="null"){n->kind=Kind::Literal;n->literal=json::Document(nullptr);}return n;}
   error="unsupported primary";return{};
 }
 std::unique_ptr<Expr> unary(){ws();auto b=p;if(take("!")){auto r=unary();if(!r)return{};auto n=node(Kind::Unary,b);n->op="!";n->right=std::move(r);return n;}if(take("-")){auto r=unary();if(!r)return{};auto n=node(Kind::Unary,b);n->op="-";n->right=std::move(r);return n;}if(take("+")){auto r=unary();if(!r)return{};auto n=node(Kind::Unary,b);n->op="+";n->right=std::move(r);return n;}return primary();}
 template<class Next> std::unique_ptr<Expr> chain(Next next,const std::initializer_list<const char*> ops){auto l=(this->*next)();if(!l)return{};while(true){ws();std::string op;for(auto x:ops)if(s.compare(p,std::char_traits<char>::length(x),x)==0){op=x;break;}if(op.empty())break;auto b=l->span.begin;p+=op.size();auto r=(this->*next)();if(!r)return{};auto n=node(Kind::Binary,b);n->op=op;n->left=std::move(l);n->right=std::move(r);l=std::move(n);}return l;}
 std::unique_ptr<Expr> mul(){return chain(&P::unary,{"*","/","%"});}
 std::unique_ptr<Expr> add(){return chain(&P::mul,{"+","-"});}
 std::unique_ptr<Expr> compare(){auto l=add();if(!l)return{};ws();for(auto op:{"==","!=","<=",">=","<",">"}){std::string x=op;if(s.compare(p,x.size(),x)==0){auto b=l->span.begin;p+=x.size();auto r=add();if(!r)return{};auto n=node(Kind::Binary,b);n->op=x;n->left=std::move(l);n->right=std::move(r);return n;}}return l;}
 std::unique_ptr<Expr> logical_and(){auto l=compare();if(!l)return{};while(take("&&")){auto b=l->span.begin;auto r=compare();if(!r)return{};auto n=node(Kind::Logical,b);n->op="&&";n->left=std::move(l);n->right=std::move(r);l=std::move(n);}return l;}
 std::unique_ptr<Expr> logical_or(){auto l=logical_and();if(!l)return{};while(take("||")){auto b=l->span.begin;auto r=logical_and();if(!r)return{};auto n=node(Kind::Logical,b);n->op="||";n->left=std::move(l);n->right=std::move(r);l=std::move(n);}return l;}
};
bool eq(const json::Document&a,const json::Document&b){if(a.is_number()&&b.is_number())return a.num==b.num;if(a.type!=b.type)return false;if(a.is_null())return true;if(a.is_bool())return a.boolean==b.boolean;if(a.is_string())return a.string==b.string;if(a.is_array()){if(a.array.size()!=b.array.size())return false;for(size_t i=0;i<a.array.size();++i)if(!eq(a.array[i],b.array[i]))return false;return true;}if(a.is_object()){if(a.object.size()!=b.object.size())return false;for(const auto&x:a.object){if(!b.has(x.first)||!eq(x.second,b[x.first]))return false;}return true;}return false;}
}
ParseResult parse_expression(const std::string& source){P p{source};auto e=p.logical_or();p.ws();if(!e||p.p!=source.size())return{{},p.error.empty()?"unsupported expression":p.error,false};return{std::move(e),{},true};}
bool truthy(const json::Document& d){if(d.is_bool())return d.boolean;if(d.is_null())return false;if(d.is_number())return d.num!=0;if(d.is_string())return !d.string.empty();if(d.is_array())return !d.array.empty();if(d.is_object())return !d.object.empty();return false;}
bool evaluate(const Expr& e,Context& c,json::Document& out,std::string& error){
 if(e.kind==Kind::Literal){out=e.literal;return true;}if(e.kind==Kind::Binding)return c.resolve(e.name,out,error);if(e.kind==Kind::Legacy)return c.legacy(e.text,out,error);
 if(e.kind==Kind::Unary){json::Document r;if(!evaluate(*e.right,c,r,error))return false;if(e.op=="!"){out=json::Document(!truthy(r));return true;}if(!r.is_number()){error="unary arithmetic operators require a numeric operand";return false;}out=json::Document(e.op=="-"?-r.num:r.num);return true;}
 if(e.kind==Kind::Logical){json::Document l;if(!evaluate(*e.left,c,l,error))return false;if(e.op=="&&"&&!truthy(l)){out=json::Document(false);return true;}if(e.op=="||"&&truthy(l)){out=json::Document(true);return true;}json::Document r;if(!evaluate(*e.right,c,r,error))return false;out=json::Document(truthy(r));return true;}
 if(e.kind==Kind::Binary){json::Document l,r;if(!evaluate(*e.left,c,l,error)||!evaluate(*e.right,c,r,error))return false;const auto&o=e.op;if(o=="=="||o=="!="){bool v=eq(l,r);out=json::Document(o=="=="?v:!v);return true;}if(o=="<"||o=="<="||o==">"||o==">="){if(l.is_number()&&r.is_number()){bool v=o=="<"?l.num<r.num:o=="<="?l.num<=r.num:o==">"?l.num>r.num:l.num>=r.num;out=json::Document(v);return true;}if(l.is_string()&&r.is_string()){bool v=o=="<"?l.string<r.string:o=="<="?l.string<=r.string:o==">"?l.string>r.string:l.string>=r.string;out=json::Document(v);return true;}error="ordering comparisons require two numbers or two strings";return false;}
   if(o=="+"&&(l.is_string()||r.is_string())){if(!c.render){error="string rendering unavailable";return false;}out=json::Document(c.render(l)+c.render(r));return true;}if(!l.is_number()||!r.is_number()){error="arithmetic operators require numeric operands";return false;}if(o=="+")out=json::Document(l.num+r.num);else if(o=="-")out=json::Document(l.num-r.num);else if(o=="*")out=json::Document(l.num*r.num);else if(o=="/"){if(r.num==0){error="division by zero";return false;}out=json::Document(l.num/r.num);}else if(o=="%"){if(r.num==0){error="modulo by zero";return false;}out=json::Document(std::fmod(l.num,r.num));}else return false;return true;}
 error="unsupported AST node";return false;
}
} // namespace nift::ast
