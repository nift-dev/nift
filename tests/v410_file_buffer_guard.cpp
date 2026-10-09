#include "Parser.h"
#include "ScriptHost.h"
#include <cassert>
#include <chrono>
#include <iostream>
struct SourceTranslationTestAccess {
    static void guard(Parser& parser,const std::filesystem::path& path){
        auto call=[&](const std::string& source){nift::RuntimeValue out;std::string error;const bool ok=parser.evaluate_expression(source,out,error);if(!ok)std::cerr<<source<<": "<<error<<"\n";assert(ok);return out;};
        auto tag=call("file("+nift::RuntimeValue(path.generic_string()).dump()+")");
        assert(tag.string=="\x1fnift:file:1");
        parser.variable_scopes_.back().emplace("f",Parser::VariableBinding{std::make_shared<nift::RuntimeValue>(tag),0,true,false});
        auto f=parser.file_instances_.at("1");
        call("f.open(\"w\")");call("f.write(\"abc\")");call("f.save()");
        assert(f->saved_is_working&&f->saved.empty()&&!f->dirty&&f->working=="abc");
        call("f.seek(0)");call("f.write(\"abc\")");
        assert(f->saved_is_working&&f->saved.empty()&&!f->dirty);
        call("f.seek(0)");call("f.write(\"xbc\")");
        assert(!f->saved_is_working&&f->saved=="abc"&&f->working=="xbc"&&f->dirty);
        call("f.revert()");assert(f->saved_is_working&&f->saved.empty()&&f->working=="abc"&&f->cursor==0&&!f->dirty);
        call("f.append(\"\")");assert(f->saved_is_working&&f->saved.empty()&&!f->dirty);
        std::size_t peak_entries=0,peak_charge=0;
        for(unsigned i=0;i<200;++i){call("f.write(\""+std::to_string(i)+"\")");call("f.revert()");peak_entries=std::max(peak_entries,parser.canonical_file_plans_.size());peak_charge=std::max(peak_charge,parser.canonical_file_plan_bytes_);assert(peak_entries<=32&&peak_charge<=4*1024*1024);}
        call("f.close()");assert(!f->open&&!f->saved_is_working&&f->working.empty()&&f->saved.empty());
        parser.variable_scopes_.back().emplace("opaque",Parser::VariableBinding{std::make_shared<nift::RuntimeValue>(std::string("\x1fnift:file:1")),0,true,false});
        call("opaque.open(\"rw\")");
        assert(f->open&&f->saved_is_working&&f->working=="abc"&&f->saved.empty());
        call("f.close()");auto next=call("file("+nift::RuntimeValue(path.generic_string()).dump()+")");
        assert(next.string=="\x1fnift:file:2"&&parser.file_instances_.size()==2);
        std::cout<<"PASS saved-state alias, lazy snapshots, no-op writes, revert, retained shell/IDs; cache="<<peak_entries<<" entries/"<<peak_charge<<" conservative bytes\n";
    }
};
int main(){auto root=std::filesystem::temp_directory_path()/("nift-file-guard-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(root);{ScriptRenderHost host(root);TrackedInfo tracked;Parser parser(host,tracked);SourceTranslationTestAccess::guard(parser,root/"data");}std::filesystem::remove_all(root);}
