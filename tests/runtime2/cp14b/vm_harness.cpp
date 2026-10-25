// In-process CP14B sanitizer harness with the CLI's SourceMap/report contract.
#include "rt_vm_tool.h"
#include "rt/source_map.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
using namespace nift::rt;
namespace {
void stats(const char* outcome,int executions,const VmResult& r) {
    const char* path=std::getenv("NIFT_VM_STATS");if(!path)return;
    std::ofstream out(path,std::ios::app);
    out<<"{\"outcome\":\""<<outcome<<"\",\"vm_executions\":"<<executions
       <<",\"legacy_executions\":0,\"heap_peak\":"<<r.heap_peak
       <<",\"heap_live\":"<<r.heap_live<<",\"callback_frames\":"<<r.diagnostic_frames.size()<<"}\n";
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    const std::string path=std::filesystem::absolute(argv[1]).lexically_normal().generic_string();
    std::ifstream file(argv[1]);std::ostringstream text;text<<file.rdbuf();const auto source=text.str();
    try {
        auto c=rt_vm_tool::compile(source);
        if(!c.program) {
            stats("require-error",0,{});
            std::fprintf(stderr,"not compiled: %s\n",c.reason.c_str());return 2;
        }
        auto map=SourceMap::single(path,source);VmOptions options;options.source_map=&map;
        if(const char* limit=std::getenv("NIFT_VM_HEAP_BUDGET"))options.heap_budget=std::strtoull(limit,nullptr,10);
        Vm vm(*c.program,options);std::string output;const auto result=vm.run(output);
        std::fwrite(output.data(),1,output.size(),stdout);
        if(result.reason==StopReason::None) {
            if(result.returned&&!result.return_text.empty())std::printf("%s\n",result.return_text.c_str());
            stats("vm",1,result);return 0;
        }
        stats(result.reason==StopReason::Error?"vm-error":"internal",1,result);
        auto origin=map.at(result.offset);
        if(result.error_origin)origin={result.origin_source,result.origin_line,result.origin_column};
        else if(!result.diagnostic_frames.empty()&&!result.diagnostic_frames.front().site.source.empty()) {
            const auto& display=result.diagnostic_frames.front().site;
            origin={display.source.generic_string(),static_cast<std::uint32_t>(display.line),
                static_cast<std::uint32_t>(display.column)};
        }
        if(result.reason==StopReason::Error)
            std::fprintf(stderr,"error: %s:%u:%u: %s\n",origin.path.c_str(),origin.line,origin.column,result.message.c_str());
        else std::fprintf(stderr,"NIFT_VM internal failure: %s\n",result.message.c_str());
        return result.reason==StopReason::Error?1:70;
    } catch(const std::bad_alloc&) {std::fputs("NIFT_VM internal failure: bad_alloc\n",stderr);return 70;}
}
