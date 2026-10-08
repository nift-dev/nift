#include "Parser.h"
#include "ScriptHost.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
int main(int argc,char**argv) {
    ScriptRenderHost host(std::filesystem::current_path());TrackedInfo tracked;Parser parser(host,tracked);
    for(int arg=1;arg<argc;++arg){
        std::ifstream stream(argv[arg]);std::string source((std::istreambuf_iterator<char>(stream)),{});
        std::vector<double> samples;
        for(int round=0;round<9;++round){
            const auto start=std::chrono::steady_clock::now();
            for(int iteration=0;iteration<25;++iteration)
                if(parser.statement_state(source)!=Parser::StatementState::Complete)return 2;
            const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/25;
            if(round>=2)samples.push_back(us);
        }
        std::sort(samples.begin(),samples.end());
        std::cout<<std::filesystem::path(argv[arg]).filename().string()<<" "<<source.size()<<" "<<samples[3]<<"\n";
    }
}
