from pathlib import Path
import subprocess,json
import tempfile,os
p=Path(tempfile.mkdtemp(prefix='nift-number-sort-oracle-'));r=Path(__file__).resolve().parent.parent;s=(r/'src/ParserExpression.cpp').read_text();a=s.index('const bool xn=std::isnan(x.value)');b=s.index('\n                                });',a);body=s[a:b]
code='''#include "RuntimeValue.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <random>
#include <iostream>
struct Key {double value;size_t original_index;};
bool candidate(Key x,Key y,bool desc){'''+body+'''}
int main(){std::mt19937_64 rng(881);std::vector<nift::RuntimeValue> v;
for(int i=0;i<2000;++i){double d;auto b=rng();std::memcpy(&d,&b,8);v.emplace_back(d);}
for(double d:{0.,-0.,std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::max(),std::numeric_limits<double>::denorm_min(),std::nextafter(1.,2.),1.,std::nextafter(1.,0.)})v.emplace_back(d);
for(uint64_t bits:{0x7ff8000000000001ULL,0x7ff8000000000123ULL,0xfff8000000000001ULL}){double d;std::memcpy(&d,&bits,8);v.emplace_back(d);}
size_t pairs=0;for(const auto&x:v)for(const auto&y:v){auto c=nift::runtime_compare_numbers(x,y);assert(candidate({x.num,0},{y.num,0},false)==(c<0));assert(candidate({x.num,0},{y.num,0},true)==(c>0));++pairs;}
for(bool desc:{false,true}){std::vector<Key> keys;std::vector<size_t> order(v.size());std::iota(order.begin(),order.end(),0);for(size_t i=0;i<v.size();++i)keys.push_back({v[i].num,i});std::stable_sort(keys.begin(),keys.end(),[=](auto x,auto y){return candidate(x,y,desc);});std::stable_sort(order.begin(),order.end(),[&](auto x,auto y){auto c=nift::runtime_compare_numbers(v[x],v[y]);return desc?c>0:c<0;});for(size_t i=0;i<v.size();++i)assert(keys[i].original_index==order[i]);}
std::cout<<"PASS actual candidate comparator "<<pairs<<" Number pairs, both directions, full stable index order; key bytes="<<sizeof(Key)<<"\\n";}
'''
(p/'special-oracle.cpp').write_text(code)
commands=[['c++','-std=c++17','-O2','-I'+str(r/'src'),'-I'+str(r/'include'),str(p/'special-oracle.cpp'),str(r/'src/RuntimeValue.cpp'),'-o',str(p/'special-oracle')],[str(p/'special-oracle')]]
rows=[]
for c in commands:
 q=subprocess.run(c,capture_output=True,text=True)
 rows.append({'command':c,'exit':q.returncode,'stdout':q.stdout,'stderr':q.stderr})
 assert q.returncode==0,rows[-1]
print(rows[-1]['stdout']);import shutil;shutil.rmtree(p)
