#include "shell/SessionJobs.h"
#include "rt/handles/handle_box.h"
#include "rt/handles/table.h"
#include "rt_test.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#ifndef _WIN32
#include <signal.h>
#include <unistd.h>
#include <cerrno>
#endif
using namespace nift::rt;
static SessionResult command(VmSession& s,const std::string& text) {
 std::string output;auto r=s.execute(text,"job-command",output);
 if(r.execution.reason!=StopReason::None)std::cerr<<"COMMAND "<<text<<" ERROR "<<r.execution.message<<" REFUSAL "<<r.refusal<<"\n";
 CHECK(r.admitted);CHECK(r.vm_executions==1);CHECK(r.legacy_executions==0);CHECK(r.execution.reason==StopReason::None);return r;
}
struct FaultOwner final : SessionJobOwner {
 int& closes;explicit FaultOwner(int& n):closes(n){}
 VmResult finish() override {++closes;VmResult r;r.host_fault=true;r.reason=StopReason::Error;r.message="independent host fault";r.host_fault_message=r.message;return r;}
};
int main(int argc,char**argv) {
 if(argc!=2)return 2;
#ifndef _WIN32
 for(bool cancelled:{false,true}) {
  VmSession s;auto owner=std::make_unique<nift::shell::SessionJobs>();auto* jobs=&owner->jobs;s.attach_jobs(std::move(owner));
  ProcessSpec spec;spec.program="/bin/sh";spec.args={"-c","sleep 60"};
  auto launched=jobs->launch({spec},"sleeping job",false);CHECK(launched.launched);CHECK(!launched.host_fault);
  auto list=jobs->jobs();CHECK(list.size()==1);const auto pid=list.front().process_ids.front();
  CHECK(::kill(static_cast<pid_t>(pid),0)==0);
  command(s,"a:=1");command(s,"a+=1");CHECK(s.value("a").as_int()==2);CHECK(jobs->has_live_jobs());
  std::string output;auto refused=s.execute("run(\"true\")","run-refusal",output);CHECK(!refused.admitted);CHECK(refused.vm_executions==0);CHECK(refused.legacy_executions==0);
  CHECK(jobs->has_live_jobs());
  command(s,"try{throw error(\"caught\")}catch(e){}");CHECK(jobs->has_live_jobs());
  auto end=s.finish(cancelled);CHECK(end.reason==StopReason::None);CHECK(!end.host_fault);
  errno=0;CHECK(::kill(static_cast<pid_t>(pid),0)==-1);CHECK(errno==ESRCH);CHECK(s.finish().reason==StopReason::None);
 }
 {
  // The script table still settles its own tickets independently of JobTable.
  VmSession s;auto owner=std::make_unique<nift::shell::SessionJobs>();auto*jobs=&owner->jobs;s.attach_jobs(std::move(owner));
  ProcessSpec spec;spec.program="/bin/sh";spec.args={"-c","exit 7"};CHECK(jobs->launch({spec},"exit seven",false).launched);
  { std::ofstream file(std::string(argv[1])+"/job-file");file<<"original"; }
  command(s,"f:=file(\""+std::string(argv[1])+"/job-file\");f.open(\"r\")");
  auto value=s.value("f");auto*table=static_cast<handles::HandleTable*>(value.heap_as<HandleBoxObject>()->table);
  const auto result=jobs->wait_all();CHECK(result.exit_code==7);CHECK(!result.host_fault);
  command(s,"f.close()");
  CHECK(s.finish(true).reason==StopReason::None);const auto stats=table->stats();CHECK(stats.created==1);CHECK(stats.live==0);CHECK(stats.created==stats.cleaned);CHECK(stats.created==stats.settled);
 }
#endif
 {
  int closes=0;VmSession s;s.attach_jobs(std::make_unique<FaultOwner>(closes));command(s,"a:=1");
  auto r=s.finish();CHECK(closes==1);CHECK(r.host_fault);CHECK(r.reason==StopReason::Error);CHECK(r.message=="independent host fault");CHECK(r.host_fault_message==r.message);
  CHECK(s.finish().reason==StopReason::None);CHECK(closes==1);
 }
 {
  int closes=0;{VmSession s;s.attach_jobs(std::make_unique<FaultOwner>(closes));}CHECK(closes==1);
 }
 return rt_test::finish("CP19 session JobTable ownership");
}
