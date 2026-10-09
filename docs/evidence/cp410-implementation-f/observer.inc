#include <unordered_map>
#include <vector>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
namespace nift {
namespace {
struct WorkloadRow { unsigned long long id=0,width=0,reads=0,writes=0,copies=0,iterations=0,insertions=0; };
struct WorkloadObserver {
    std::mutex mutex;
    unsigned long long next=1;
    std::unordered_map<const void*,WorkloadRow> active;
    std::unordered_map<const void*,const void*> members;
    std::vector<WorkloadRow> finished;
    WorkloadRow& get(const void* key,std::size_t width){auto& row=active[key];if(!row.id)row.id=next++;row.width=std::max(row.width,(unsigned long long)width);return row;}
    static WorkloadObserver& instance(){static auto* value=new WorkloadObserver;return *value;}
    static void flush(){
        const char* path=std::getenv("NIFT_OBJECT_WORKLOAD");if(!path)return;
        auto& observer=instance();std::lock_guard<std::mutex> lock(observer.mutex);FILE* out=std::fopen(path,"w");if(!out)return;
        std::fprintf(out,"id,width,lookup_reads,member_assignments,source_copies,range_passes,insertions\n");
        auto emit=[&](const WorkloadRow& r){std::fprintf(out,"%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",r.id,r.width,r.reads,r.writes,r.copies,r.iterations,r.insertions);};
        for(const auto& row:observer.finished)emit(row);for(const auto& entry:observer.active)emit(entry.second);std::fclose(out);
    }
    WorkloadObserver(){std::atexit(flush);}
};
}
void workload_object_event(const void* key,std::size_t width,char event) noexcept {try{
    auto& observer=WorkloadObserver::instance();std::lock_guard<std::mutex> lock(observer.mutex);auto& row=observer.get(key,width);
    if(event=='r')++row.reads;else if(event=='w')++row.writes;else if(event=='c')++row.copies;else if(event=='i')++row.iterations;else if(event=='n')++row.insertions;
}catch(...) {}}
void workload_object_member(const void* member,const void* parent,std::size_t width) noexcept {try{
    auto& observer=WorkloadObserver::instance();std::lock_guard<std::mutex> lock(observer.mutex);observer.get(parent,width);observer.members[member]=parent;
}catch(...) {}}
void workload_object_write(const void* key) noexcept {try{
    auto& observer=WorkloadObserver::instance();std::lock_guard<std::mutex> lock(observer.mutex);auto found=observer.members.find(key);
    if(found!=observer.members.end()){auto row=observer.active.find(found->second);if(row!=observer.active.end())++row->second.writes;}
}catch(...) {}}
void workload_object_destroy(const void* key) noexcept {try{
    auto& observer=WorkloadObserver::instance();std::lock_guard<std::mutex> lock(observer.mutex);observer.members.erase(key);auto found=observer.active.find(key);
    if(found!=observer.active.end()){
        observer.finished.push_back(found->second);observer.active.erase(found);
        for(auto it=observer.members.begin();it!=observer.members.end();)if(it->second==key)it=observer.members.erase(it);else ++it;
    }
}catch(...) {}}
void workload_object_move(const void* destination,const void* source,std::size_t width) noexcept {try{
    auto& observer=WorkloadObserver::instance();std::lock_guard<std::mutex> lock(observer.mutex);
    auto found=observer.active.find(source);
    if(found!=observer.active.end()&&destination!=source){
        auto old=observer.active.find(destination);if(old!=observer.active.end())observer.finished.push_back(old->second);
        const auto row=found->second;observer.active.erase(found);observer.active[destination]=row;
    }else if(width)observer.get(destination,width);
    for(auto& entry:observer.members)if(entry.second==source)entry.second=destination;
    auto member=observer.members.find(source);if(member!=observer.members.end()&&destination!=source){auto parent=member->second;observer.members.erase(member);observer.members[destination]=parent;}
}catch(...) {}}
}
