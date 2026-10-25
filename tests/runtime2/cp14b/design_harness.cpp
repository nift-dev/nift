// Real-runtime test driver: bounded frame options and reporting-only metadata.
#include "rt_vm_tool.h"
#include "rt/source_map.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#ifndef CP14B_MUTANT_MARKER
#define CP14B_MUTANT_MARKER "control"
#endif
using namespace nift::rt;
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    std::ifstream file(argv[1]);std::ostringstream text;text<<file.rdbuf();const auto source=text.str();
    const auto path=std::filesystem::absolute(argv[1]).lexically_normal().generic_string();
    auto compiled=rt_vm_tool::compile(source);VmResult result;std::string output;
    int executions=0,exit=1;std::string outcome="require-error";
    if(compiled.program) {
        auto map=SourceMap::single(path,source);VmOptions options;options.source_map=&map;
        if(const char* frames=std::getenv("NIFT_CP14B_TEST_MAX_FRAMES"))
            options.max_frames=std::strtoull(frames,nullptr,10);
        try {
            Vm vm(*compiled.program,options);executions=1;result=vm.run(output);
            outcome=result.reason==StopReason::None?"vm":result.reason==StopReason::Error?"vm-error":"internal-error";
            exit=result.reason==StopReason::None?0:result.reason==StopReason::Error?1:70;
            if(result.reason==StopReason::None&&result.returned&&!result.return_text.empty())
                output+=result.return_text+"\n";
            if(result.reason==StopReason::Error) {
                auto origin=map.at(result.offset);
                if(result.error_origin)origin={result.origin_source,result.origin_line,result.origin_column};
                std::fprintf(stderr,"error: %s:%u:%u: %s\n",origin.path.c_str(),origin.line,origin.column,
                    result.message.c_str());
            }
        }catch(const std::bad_alloc&){outcome="internal-error";result.message="bad_alloc";exit=70;}
    } else result.message=compiled.reason;
    std::fwrite(output.data(),1,output.size(),stdout);
    if(const char* stats=std::getenv("NIFT_VM_STATS")) {
        std::ofstream stream(stats);
        stream<<"{\"outcome\":"<<std::quoted(outcome)<<",\"vm_executions\":"<<executions
            <<",\"legacy_executions\":0,\"reason\":"<<std::quoted(result.message)
            <<",\"callback_frames\":"<<result.diagnostic_frames.size()
            <<",\"beyond_legacy_depth\":"<<(result.beyond_legacy_depth?"true":"false")
            <<",\"error_origin\":"<<(result.error_origin?"true":"false")
            <<",\"marker\":"<<std::quoted(CP14B_MUTANT_MARKER)<<"}\n";
    }
    return exit;
}
