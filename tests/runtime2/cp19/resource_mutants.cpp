#include "rt/session.h"
#include "rt/handles/handle_box.h"
#include "rt_test.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace nift::rt;
SessionResult execute(VmSession& session,const std::string& source) {
    std::string output;auto r=session.execute(source,"resource-witness",output);
    CHECK(r.legacy_executions==0);CHECK(output.empty());return r;
}
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    const std::filesystem::path root=argv[1];const std::string test=argv[2],expect=argv[3];
    const bool bad=expect=="broken";const auto path=root/"p";{std::ofstream out(path);out<<"original";}
    const std::string file="file(\""+path.generic_string()+"\")";
    VmSession session;
    if(test=="unwind" || test=="reports") {
        auto r=execute(session,"fn(temp()){f:="+file+";f.open(\"rw\");"+
            (test=="unwind"?"throw error(\"primary\")":"")+"};temp()");
        CHECK(r.admitted && r.vm_executions==1);CHECK(r.execution.reason==StopReason::Error);
        if(test=="unwind")CHECK(r.execution.message=="primary");
        else CHECK(r.execution.message.find("managed file left open")!=std::string::npos);
        r=execute(session,"tick:=1");CHECK(r.admitted);
        CHECK(r.execution.reason==(bad?StopReason::Error:StopReason::None));
        if(bad)CHECK(r.execution.message.find("managed file left open")!=std::string::npos);
        auto finish=session.finish();
        CHECK(finish.reason==(bad && test=="reports"?StopReason::Error:StopReason::None));
    } else {
        auto r=execute(session,"f:="+file);CHECK(r.admitted && r.execution.reason==StopReason::None);
        if(test=="survival") {
            r=execute(session,"f.open(\"r\")");CHECK(r.admitted);
            CHECK(r.execution.reason==(bad?StopReason::Error:StopReason::None));
            if(bad)CHECK(r.execution.code=="runtime.handle_closed");
            else { r=execute(session,"f.close()");CHECK(r.admitted && r.execution.reason==StopReason::None); }
        } else if(test=="capture") {
            r=execute(session,"fn(capture()){f.close()}");CHECK(r.admitted==bad);
            if(!bad)CHECK(r.refusal=="host-handle-flow");
        } else if(test=="widening") {
            r=execute(session,"a:=[1]");CHECK(r.admitted);
            r=execute(session,"a.push(\"wide\")");CHECK(r.admitted!=bad);
            if(bad)CHECK(r.refusal=="session:widening-callable-flow");
        } else if(test=="storage") {
            auto value=session.value("f");CHECK(value.boxed_handle());
            auto* box=value.heap_as<HandleBoxObject>();CHECK(bool(box->table_owner)!=bad);
            if(!box->table_owner)value=Value(); // no deliberate use-after-free in a negative control
            auto finished=session.finish();CHECK(finished.reason==StopReason::None);
            if(value.boxed_handle())CHECK(static_cast<handles::HandleTable*>(box->table)->stats().live==0);
        } else return 2;
        CHECK(session.finish().reason==StopReason::None);
    }
    auto status=rt_test::finish("CP19 resource mutation witness");
    if(!status)std::cout<<"WITNESS "<<test<<' '<<expect<<'\n';
    return status;
}
