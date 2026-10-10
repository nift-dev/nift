#pragma once
#include <chrono>
#include <iostream>
namespace stage { using Clock=std::chrono::steady_clock; inline void emit(const char* name,Clock::time_point start){std::cerr<<"STAGE "<<name<<" "<<std::chrono::duration<double,std::milli>(Clock::now()-start).count()<<"\n";} struct Scope{const char* name;Clock::time_point start;Scope(const char* n):name(n),start(Clock::now()){} ~Scope(){emit(name,start);}};}
