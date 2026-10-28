#include "rt/session.h"
#include <iostream>
#include <string>
using namespace nift::rt;
struct Owner final:SessionJobOwner {
 int& count;explicit Owner(int& c):count(c){}
 VmResult finish() override {++count;VmResult r;r.reason=StopReason::Error;r.host_fault=true;r.host_fault_message=r.message="job-terminal-fault";return r;}
};
int main(int argc,char**argv){
 if(argc!=2)return 2;
 const std::string mode=argv[1];
 if(mode=="unrelated"){std::cout<<"UNRELATED FAILURE\n";return 2;}
 int count=0;VmSession s;s.attach_jobs(std::make_unique<Owner>(count));
 auto r=s.finish();
 const bool finish=count==1 && r.reason==StopReason::Error && r.message=="job-terminal-fault";
 const bool fault=r.host_fault && r.host_fault_message=="job-terminal-fault";
 const bool ok=finish&&fault;
 std::cout<<"WITNESS finish="<<finish<<" fault="<<fault<<" count="<<count<<'\n';
 if(mode=="normal")return ok?0:1;
 if(mode=="expect-missing-finish")return !finish&&count==0?0:1;
 if(mode=="expect-missing-fault")return finish&&!fault&&count==1?0:1;
 return 2;
}
