#pragma once
#include "Json.h"
#include "Types.h"
#include "FileSystem.h"
#include <filesystem>
#include <sstream>
#include <cctype>
#include <functional>
#include <set>
#include <vector>
namespace frontmatter {
struct Parsed { bool present=false; json::Document value=json::Document::make_object(); std::string body; std::string error; };
inline std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r");if(a==std::string::npos)return{};auto b=s.find_last_not_of(" \t\r");return s.substr(a,b-a+1);}
inline json::Document scalar(const std::string& raw){std::string s=trim(raw);if(s=="true")return json::Document(true);if(s=="false")return json::Document(false);if(s=="null"||s=="~")return json::Document();if(s.size()>=2&&((s.front()=='"'&&s.back()=='"')||(s.front()=='\''&&s.back()=='\'')))return json::Document(s.substr(1,s.size()-2));char* e=nullptr;double n=strtod(s.c_str(),&e);if(e&&*e=='\0'&&!s.empty())return json::Document(n);return json::Document(s);}
inline bool parse_yaml_object(const std::string& text,json::Document& out,std::string& error){
 if(text.size()>1024*1024){error="front matter exceeds 1 MiB limit";return false;}
 struct L{size_t indent,line;std::string text;};std::vector<L> ls;std::istringstream in(text);std::string line;size_t ln=0;
 while(std::getline(in,line)){++ln;if(line.size()>65536){error="front matter line exceeds 64 KiB at line "+std::to_string(ln);return false;}if(!line.empty()&&line.back()=='\r')line.pop_back();auto t=trim(line);if(t.empty()||t.rfind("#",0)==0)continue;size_t ind=line.find_first_not_of(" ");if(ind==std::string::npos)continue;if(line.find('\t',0)!=std::string::npos&&line.find('\t')<ind+1){error="front matter tabs are not allowed for indentation at line "+std::to_string(ln);return false;}ls.push_back({ind,ln,line.substr(ind)});}
 std::function<bool(size_t&,size_t,json::Document&,size_t)> block=[&](size_t&i,size_t indent,json::Document& node,size_t depth)->bool{
  if(depth>32){error="front matter nesting exceeds 32 levels";return false;}if(i>=ls.size()){node=json::Document::make_object();return true;}bool list=ls[i].text.rfind("- ",0)==0;node=list?json::Document::make_array():json::Document::make_object();std::set<std::string> keys;
  while(i<ls.size()&&ls[i].indent==indent){auto cur=ls[i];if(list){if(cur.text.rfind("- ",0)!=0){error="front matter mixed list/object at line "+std::to_string(cur.line);return false;}std::string val=trim(cur.text.substr(2));++i;if(val.empty()&&i<ls.size()&&ls[i].indent>indent){json::Document child;if(!block(i,ls[i].indent,child,depth+1))return false;node.push_back(child);}else node.push_back(scalar(val));}
   else {if(cur.text.rfind("- ",0)==0){error="front matter mixed object/list at line "+std::to_string(cur.line);return false;}auto c=cur.text.find(':');if(c==std::string::npos){error="front matter expected key: value at line "+std::to_string(cur.line);return false;}std::string key=trim(cur.text.substr(0,c)),val=trim(cur.text.substr(c+1));if(key.empty()){error="front matter has an empty key at line "+std::to_string(cur.line);return false;}if(!keys.insert(key).second){error="front matter duplicate key '"+key+"'";return false;}++i;if(val.empty()){if(i<ls.size()&&ls[i].indent>indent){json::Document child;if(!block(i,ls[i].indent,child,depth+1))return false;node[key]=child;}else node[key]=json::Document();}else node[key]=scalar(val);}
   if(i<ls.size()&&ls[i].indent<indent)break;if(i<ls.size()&&ls[i].indent>indent){error="front matter unexpected indentation at line "+std::to_string(ls[i].line);return false;}
  }return true;};
 size_t i=0;if(ls.empty()){out=json::Document::make_object();return true;}if(!block(i,ls[0].indent,out,0))return false;if(!out.is_object()){error="front matter root must be an object";return false;}return i==ls.size();}
inline Parsed parse_inline(const std::string& source){Parsed r;r.body=source;if(source.rfind("---\n",0)!=0&&source.rfind("---\r\n",0)!=0)return r;size_t first=source.find('\n');size_t pos=first+1,end=std::string::npos,next=pos;while(next<source.size()){size_t nl=source.find('\n',next);std::string line=source.substr(next,(nl==std::string::npos?source.size():nl)-next);if(!line.empty()&&line.back()=='\r')line.pop_back();if(line=="---"){end=next;pos=(nl==std::string::npos?source.size():nl+1);break;}if(nl==std::string::npos)break;next=nl+1;}if(end==std::string::npos){r.error="unterminated front matter";return r;}r.present=true;std::string yaml=source.substr(first+1,end-(first+1));if(!parse_yaml_object(yaml,r.value,r.error))return r;r.body=source.substr(pos);return r;}
inline bool load_external(const std::filesystem::path& root,const std::string& rel,json::Document& out,std::string& error){std::filesystem::path p=(root/rel).lexically_normal();auto rr=root.lexically_normal().generic_string(),pp=p.generic_string();if(pp!=rr&&pp.rfind(rr+"/",0)!=0){error="frontmatter path escapes project root";return false;}if(!filesystem::file_exists(p)){error="frontmatter file does not exist: "+rel;return false;}std::string s=filesystem::read_file(p);if(p.extension()==".json"){std::string e;if(!json::Document::parse(s,out,e)||!out.is_object()){error="invalid JSON frontmatter"+(e.empty()?std::string{}:": "+e);return false;}return true;}return parse_yaml_object(s,out,error);}
}
