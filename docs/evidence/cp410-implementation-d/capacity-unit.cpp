#include <Parser.h>
#include <ScriptHost.h>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
struct SourceTranslationTestAccess {
    static void measure(Parser& parser,const std::filesystem::path& root){
        auto call=[&](const std::string& source){nift::RuntimeValue out;std::string error;assert(parser.evaluate_expression(source,out,error));return out;};
        for(unsigned i=0;i<32;++i){
            auto tag=call("file("+nift::RuntimeValue((root/std::to_string(i)).generic_string()).dump()+")");
            parser.variable_scopes_.back()["f"]=Parser::VariableBinding{std::make_shared<nift::RuntimeValue>(tag),0,true,false};
            call("f.open(\"rw\")");call("f.close()");
        }
        assert(parser.file_instances_.size()==32);
        std::size_t working=0,saved=0;
        for(const auto& entry:parser.file_instances_){assert(!entry.second->open&&entry.second->working.empty()&&entry.second->saved.empty());working+=entry.second->working.capacity();saved+=entry.second->saved.capacity();}
        std::cout<<"{\"instances\":32,\"working_capacity\":"<<working<<",\"saved_capacity\":"<<saved<<"}\n";
    }
};
int main(){auto root=std::filesystem::temp_directory_path()/("nift-capacity-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(root);std::string payload(1024*1024,'x');for(unsigned i=0;i<32;++i){std::ofstream out(root/std::to_string(i),std::ios::binary);out.write(payload.data(),payload.size());}{ScriptRenderHost host(root);TrackedInfo tracked;Parser parser(host,tracked);SourceTranslationTestAccess::measure(parser,root);}std::filesystem::remove_all(root);}
