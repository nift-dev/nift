#include "rt/session.h"
#include "rt/handles/file_resource.h"
#include "rt/handles/handle_box.h"
#include "rt/string.h"
#include "rt/host/process_handles.h"
#include "rt/host/process.h"
#include "rt_test.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <type_traits>
static_assert(!std::is_copy_constructible_v<nift::rt::Vm>);
static_assert(!std::is_move_constructible_v<nift::rt::Vm>);
using namespace nift::rt;
namespace {
SessionResult run(VmSession& session, const std::string& source, StopReason expected=StopReason::None) {
    std::string output; auto result=session.execute(source,"resource-command",output);
    if (!result.admitted || result.execution.reason!=expected)
        std::cerr<<"SOURCE "<<source<<" REFUSAL "<<result.refusal<<" ERROR "<<result.execution.message<<'\n';
    CHECK(result.admitted); CHECK(result.vm_executions==1); CHECK(result.legacy_executions==0);
    CHECK(result.execution.reason==expected); return result;
}
std::string quoted(const std::filesystem::path& path) { return '"'+path.generic_string()+'"'; }
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    const std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
    const auto p=root/"p";{std::ofstream file(p);file<<"original\n";}
    {
        VmSession session;
        run(session,"f:=file("+quoted(p)+");f.open(\"rw\")");
        Value first=session.value("f"); CHECK(first.boxed_handle());
        run(session,"if(true){f:=file("+quoted(p)+");f.open(\"r\");f.close()}");
        CHECK(session.value("f").heap()==first.heap());
        run(session,"a:=[1]");run(session,"a.push(\"wide\")");
        run(session,"g:=f"); CHECK(session.value("g").heap()==first.heap());
        first=Value();
        run(session,"f = file("+quoted(root/"new")+")");
        run(session,"g.write(\"changed\")"); run(session,"g.revert()");
        run(session,"g.close()"); run(session,"g.close()");
        auto result=run(session,"g.read_all()",StopReason::Error);
        CHECK(result.execution.code=="runtime.handle_closed"); CHECK(result.execution.catchable);
        first=session.value("g");
        CHECK(session.finish().reason==StopReason::None);
        auto* box=first.heap_as<HandleBoxObject>();
        auto* table=static_cast<handles::HandleTable*>(box->table);
        CHECK(table->stats().live==0);
        // The embedding-held identity outlives VmSession safely; storage is pinned,
        // while finalization has already closed the resource context.
    }
    {
        VmSession session;run(session,"s:=ifstream("+quoted(p)+")");
        run(session,"line:=s.read_line()"); CHECK(string_view_of(session.value("line"))=="original");
        run(session,"s.close()"); CHECK(session.finish().reason==StopReason::None);
    }
    {
        VmSession session;run(session,"f:=file("+quoted(p)+");f.open(\"rw\")");
        const auto handle=session.value("f");
        run(session,"throw error(\"primary\")",StopReason::Error);
        run(session,"text:=f.read_all()"); CHECK(string_view_of(session.value("text"))=="original\n");
        run(session,"f.close()"); CHECK(session.finish().reason==StopReason::None);
    }
    {
        VmSession session;
        run(session,"fn(temp()){f:=file("+quoted(p)+");f.open(\"rw\");f.write(\"unsaved\");throw error(\"primary\")};temp()",StopReason::Error);
        CHECK(session.finish().reason==StopReason::None);
        std::ifstream input(p);std::string contents((std::istreambuf_iterator<char>(input)),{});
        CHECK(contents=="original\n");
    }
    {
        VmSession session;
        run(session,"fn(temp()){f:=file("+quoted(p)+");f.open(\"rw\")};temp()",StopReason::Error);
        run(session,"tick:=1"); CHECK(session.finish().reason==StopReason::None);
    }
    {
        VmSession session;
        run(session,"f:=file("+quoted(p)+");f.open(\"rw\")");
        std::string output;auto refused=session.execute("f.close();a:=[f]","refusal",output);
        CHECK(!refused.admitted);CHECK(output.empty());
        run(session,"text:=f.read_all()");CHECK(string_view_of(session.value("text"))=="original\n");
        refused=session.execute("fn(capture()){f.close()}","capture",output);
        CHECK(!refused.admitted);CHECK(refused.refusal=="host-handle-flow");
        run(session,"f.close()");CHECK(session.finish().reason==StopReason::None);
    }
    {
        VmSession session;run(session,"f:=file("+quoted(p)+");f.open(\"rw\");f.write(\"unsaved\")");
        CHECK(session.finish(true).reason==StopReason::None);
        std::ifstream input(p);std::string contents((std::istreambuf_iterator<char>(input)),{});
        CHECK(contents=="original\n");
    }
    {
        VmSession session;
        auto primary=run(session,"fn(temp()){f:=file("+quoted(p)+");f.open(\"rw\")};temp();throw error(\"primary\")",StopReason::Error);
        CHECK(primary.execution.message=="primary");
        auto obligation=run(session,"tick:=1",StopReason::Error);
        CHECK(obligation.execution.message.find("managed file left open")!=std::string::npos);
        run(session,"tick+=1");CHECK(session.finish().reason==StopReason::None);
    }
    {
        VmSession session;run(session,"anchor:=file("+quoted(p)+")");
        auto anchor=session.value("anchor");auto* box=anchor.heap_as<HandleBoxObject>();
        auto& table=*static_cast<handles::HandleTable*>(box->table);
        CHECK(table.stats().live==1);
        run(session,"fn(streamtemp()){s:=ofstream("+quoted(root/"out")+");s.write(\"committed\")};streamtemp()");
        CHECK(table.stats().live==1);CHECK(table.stats().cleaned==1);
        std::ifstream input(root/"out");std::string contents((std::istreambuf_iterator<char>(input)),{});
        CHECK(contents=="committed");CHECK(session.finish().reason==StopReason::None);
    }
#ifndef _WIN32
    {
        // Accepted CP17 Host seam, not a speculative script/job-control API.
        VmSession session;run(session,"anchor:=file("+quoted(p)+")");
        auto anchor=session.value("anchor");auto* box=anchor.heap_as<HandleBoxObject>();
        auto& table=*static_cast<handles::HandleTable*>(box->table);handles::ContextId owner=0;
        CHECK(table.owner_of(anchor,HandleKind::File,owner)==HandleStatus::Ok);
        CHECK(host::register_process_kind(table)==HandleStatus::Ok);
        host::SpawnRequest request;host::SpawnStage stage;stage.program="sh";stage.args={"-c","sleep 20"};
        request.stages.push_back(std::move(stage));request.mode=host::OutputMode::Interactive;
        request.terminal=host::TerminalPolicy::Never;
        request.kill_after=host::Millis(5);request.post_kill_wait=host::Millis(2000);
        host::HostStatus status;host::HostError error;
        auto group=host::ProcessGroup::start(std::move(request),status,error);
        CHECK(status==host::HostStatus::Ok && group!=nullptr);
        Value process;
        CHECK(host::create_process_handle(table,owner,group,process)==HandleStatus::Ok);
        auto reference=handles::box_counted_handle(table,process);
        run(session,"tick:=1");CHECK(group->state()==host::GroupState::Running);
        const auto result=session.finish(true);CHECK(result.reason==StopReason::None);
        CHECK(table.stats().live==0);CHECK(table.stats().pending_obligations==0);
        CHECK(table.stats().created==table.stats().cleaned);
        CHECK(table.stats().created==table.stats().settled);
    }
#endif
    return rt_test::finish("CP19 persistent CP17 resources");
}
