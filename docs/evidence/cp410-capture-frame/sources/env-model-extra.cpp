#include "BindingModel.h"
#include "Phases.h"
#include <iostream>
#include <cassert>
using Binding=Parser::VariableBinding;
using Map=std::unordered_map<std::string,Binding>;
struct Node { Map local; std::shared_ptr<const Node> parent; };
struct Env { Map flat; std::shared_ptr<const Node> parent; std::shared_ptr<const Map> snapshot; };
void copy(Map& dst,const Map& src){for(const auto& b:src)dst.insert_or_assign(b.first,b.second);}
Binding binding(double n){return Binding(std::make_shared<nift::RuntimeValue>(n),0,true,false);}
const Binding* parent_find(const std::shared_ptr<const Node>& p,const std::string& key){for(auto n=p;n;n=n->parent){auto i=n->local.find(key);if(i!=n->local.end())return &i->second;}return nullptr;}
const Binding* capture_find(const Env& env,const std::string& key){if(env.snapshot){auto i=env.snapshot->find(key);return i==env.snapshot->end()?nullptr:&i->second;}if(env.parent)return parent_find(env.parent,key);auto i=env.flat.find(key);return i==env.flat.end()?nullptr:&i->second;}
void flatten(Map& target,const std::shared_ptr<const Node>& n){if(!n)return;flatten(target,n->parent);copy(target,n->local);}
struct Frame {
 std::string kind;std::shared_ptr<const Env> env;Map local;Map* caller=nullptr;
 Frame(std::string k,std::shared_ptr<const Env> e,double arg,Map* c):kind(std::move(k)),env(std::move(e)),caller(c){
  if(kind=="current"||kind=="cow") {if(env->parent)flatten(local,env->parent);else copy(local,env->flat);}
  // Deliberately retain current parameter insertion, including its default slot.
  local["x"]=binding(arg);
 }
 Binding* find(const std::string& key){auto i=local.find(key);if(i!=local.end()){i->second.sync();return &i->second;}
  if(kind!="current"&&kind!="cow")if(auto* b=capture_find(*env,key)){auto q=local.insert_or_assign(key,*b);q.first->second.sync();return &q.first->second;}
  auto c=caller->find(key);if(c!=caller->end()){c->second.sync();return &c->second;}return nullptr;
 }
 bool can_declare(const std::string& key)const{return !local.count(key)&&!capture_find(*env,key);}
 std::shared_ptr<Env> nested(){auto out=std::make_shared<Env>();if(kind=="parent"){
   auto layer=std::make_shared<Node>();for(const auto& b:*caller)if(!capture_find(*env,b.first))layer->local.emplace(b);copy(layer->local,local);layer->parent=env->parent;out->parent=layer;
  }else{copy(out->flat,*caller);if(env->snapshot)copy(out->flat,*env->snapshot);else copy(out->flat,env->flat);copy(out->flat,local);}return out;}
};
Map visible(std::size_t unused){Map m;for(const auto& key:{"cmd","args","a","i"})m.emplace(key,binding(1));m.emplace("offset",binding(2));for(std::size_t i=0;i<unused;++i)m.emplace("unused"+std::to_string(i),binding(i));return m;}
std::shared_ptr<const Node> roots(const Map& m){auto global=std::make_shared<Node>();auto user=std::make_shared<Node>();for(const auto& b:m){if(b.first=="cmd"||b.first=="args")global->local.emplace(b);else user->local.emplace(b);}user->parent=global;return user;}
template<class T> bool same_owner(const std::shared_ptr<T>& a,const std::shared_ptr<T>& b){return a.get()==b.get()&&!a.owner_before(b)&&!b.owner_before(a);}
bool descriptor_equal(const Binding& a,const Binding& b){return same_owner(a.value,b.value)&&same_owner(a.slot,b.slot)&&same_owner(a.ref_root_slot,b.ref_root_slot)&&a.type==b.type&&a.mutable_binding==b.mutable_binding&&a.deep_readonly==b.deep_readonly&&a.is_script_invocation==b.is_script_invocation&&a.ref_valid==b.ref_valid&&a.ref_path==b.ref_path;}
struct SnapshotMemo {std::shared_ptr<const Map> snapshot;
 std::shared_ptr<const Map> get(const Map& source){bool equal=snapshot&&source.size()==snapshot->size();if(equal)for(const auto& b:source){auto i=snapshot->find(b.first);if(i==snapshot->end()||!descriptor_equal(i->second,b.second)){equal=false;break;}}
  if(!equal){auto fresh=std::make_shared<Map>();copy(*fresh,source);snapshot=std::move(fresh);}return snapshot;}
};
thread_local SnapshotMemo memo;
std::shared_ptr<Env> factory(const std::string& kind,const Map& source,const std::shared_ptr<const Node>& root){auto e=std::make_shared<Env>();if(kind=="memo")e->snapshot=memo.get(source);else if(kind=="parent")e->parent=root;else copy(e->flat,source);return e;}
void validate(const std::string& kind){
 if(kind=="memo"){Map m=visible(0);SnapshotMemo probe;auto a=probe.get(m);assert(probe.get(m)==a);m.at("offset").rebind(std::make_shared<nift::RuntimeValue>(9));assert(probe.get(m)!=a);auto b=probe.get(m);m.emplace("later",binding(11));assert(probe.get(m)!=b);}

 Map source=visible(5),caller;source.emplace("x",binding(99));auto rootvalue=nift::RuntimeValue::make_array();rootvalue.array.emplace_back(1);auto rootbinding=Binding(std::make_shared<nift::RuntimeValue>(std::move(rootvalue)),0,true,false);Binding alias=binding(0);alias.ref_root_slot=rootbinding.slot;alias.ref_path.push_back(Parser::PathComponent::at(0));alias.sync();source.emplace("alias",alias);
 auto r=roots(source);auto env=factory(kind,source,r);caller.emplace("late",binding(11));
 Frame f(kind,env,3,&caller);assert(f.find("x")->value->num==3);assert(f.find("late")->value->num==11);assert(!f.can_declare("offset"));assert(f.can_declare("new_name"));assert(!f.find("missing"));
 source.at("offset").rebind(std::make_shared<nift::RuntimeValue>(9));assert(f.find("offset")->value->num==9);
 f.find("offset")->rebind(std::make_shared<nift::RuntimeValue>(7));assert(source.at("offset").slot==capture_find(*env,"offset")->slot);source.at("offset").sync();assert(source.at("offset").value->num==7);
 for(int i=0;i<100;++i){(*rootbinding.slot)->array.emplace_back(i);}assert(f.find("alias")->value->num==1);
 auto replacement=nift::RuntimeValue::make_array();replacement.array.emplace_back(8);rootbinding.rebind(std::make_shared<nift::RuntimeValue>(std::move(replacement)));assert(f.find("alias")->value->num==8);
 auto child=f.nested();Frame next(kind,child,0,&caller);assert(next.find("alias")->value->num==8);assert(next.find("offset")->value->num==7);caller.erase("late");caller.emplace("late",binding(22));assert(next.find("late")->value->num==11);
 // Bare named-callable priority is a resolver responsibility, not changed by an env.
 std::unordered_map<std::string,int> named{{"x",1}};assert(named.count("x")&&f.find("x")->value->num==3);
}
int main(int argc,char**argv){if(argc!=6)return 2;std::string kind=argv[1],scenario=argv[2];std::size_t n=std::stoul(argv[3]),unused=std::stoul(argv[4]),reads=std::stoul(argv[5]);validate(kind);memo.snapshot.reset();Map source=visible(unused),caller;if(scenario=="identity"||scenario=="reused-identity")source.erase("offset");caller.emplace("late",binding(11));auto parent=roots(source);std::vector<std::shared_ptr<Env>> registry;registry.reserve(n*2);double sum=0;
 {cp51::Phase whole(cp51::Factory);std::shared_ptr<Env> reused;
  if(scenario=="reused"||scenario=="reused-identity")reused=factory(kind,source,parent);
  for(std::size_t i=0;i<n;++i){if(scenario=="changing") source.at("offset").rebind(std::make_shared<nift::RuntimeValue>(static_cast<double>(i)));std::shared_ptr<Env> env;if(reused)env=reused;else{cp51::Phase capture(cp51::Capture);env=factory(kind,source,parent);registry.push_back(env);}
   {cp51::Phase frame(cp51::Frame);Frame f(kind,env,i,&caller);frame.finish(); if(scenario=="local")f.local.emplace("local",binding(7));
    {cp51::Phase body(cp51::Body);for(std::size_t j=0;j<reads;++j){auto b=f.find((scenario=="identity"||scenario=="reused-identity")?"x":scenario=="missing"?"missing":scenario=="global"?"cmd":scenario=="local"?"local":"offset");if(b)sum+=b->value->num;}
     if(scenario=="mutation")f.find("offset")->rebind(std::make_shared<nift::RuntimeValue>(static_cast<double>(i)));
     if(scenario=="nested")registry.push_back(f.nested());}
   }
  }
  {cp51::Phase destruction(cp51::Destruction);registry.clear();reused.reset();memo.snapshot.reset();}
 }
 std::cout<<sum<<'\n';std::fprintf(stderr,"model binding_size=%zu retained=%zu\n",sizeof(Binding),n);
}
