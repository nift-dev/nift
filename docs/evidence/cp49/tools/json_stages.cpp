#include "src/RuntimeJson.h"
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc,char** argv) {
 if(argc!=2)return 2;
 std::ifstream input(argv[1]);std::string text{std::istreambuf_iterator<char>(input),{}};json::Document doc;std::string error;
 if(!json::Document::parse(text,doc,error))return 1;
 auto value=nift::runtime_from_json(doc);
 auto measure=[&](auto task){double ms=0;for(int i=0;i<20;++i){auto t=std::clock();task();ms+=1000.0*(std::clock()-t)/CLOCKS_PER_SEC;}return ms/20;};
 volatile std::size_t sink=0;
 auto parse=measure([&]{json::Document x;std::string err;if(!json::Document::parse(text,x,err))std::abort();sink=x.array.size();});
 auto convert=measure([&]{auto x=nift::runtime_from_json(doc);sink=x.array.size();});
 auto serialize=measure([&]{auto s=value.dump();sink=s.size();});
 auto copy=measure([&]{auto x=value;sink=x.array.size();});
 std::cout<<doc.array.size()<<" "<<parse<<" "<<convert<<" "<<serialize<<" "<<copy<<" "<<sink<<"\n";
}
