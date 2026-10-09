#include "Parser.h"
#include "ScriptHost.h"
#include <cassert>
#include <iostream>
struct SourceTranslationTestAccess {
    static void guard(Parser& parser) {
        std::size_t peak_bytes=0,peak_entries=0;
        for(std::size_t size:{8192u,16384u,32768u,65536u,131072u}) {
            const std::string source="\""+std::string(size-80,'a')+"\".to_upper().replace(\"A\",\"x\")";
            nift::RuntimeValue value;std::string error;
            assert(parser.evaluate_expression(source,value,error));
            assert(value.is_string()&&value.string==std::string(size-80,'x'));
        }
        for(unsigned i=0;i<80;++i) {
            const std::string payload=std::to_string(i)+std::string(120000,'a');
            const std::string source="\""+payload+"\".to_upper().replace(\"A\",\"x\")";
            nift::RuntimeValue value;std::string error;
            assert(parser.evaluate_expression(source,value,error));
            assert(value.string==std::to_string(i)+std::string(120000,'x'));
            assert(parser.large_string_plans_.size()<=16);
            assert(parser.large_string_plan_bytes_<=4*1024*1024);
            peak_bytes=std::max(peak_bytes,parser.large_string_plan_bytes_);
            peak_entries=std::max(peak_entries,parser.large_string_plans_.size());
        }
        // The ordinary preparation decision remains unchanged, while the
        // separate literal-payload entry still rejects excessive structure.
        const std::string source="\""+std::string(16384,'a')+"\".to_upper().replace(\"A\",\"x\")";
        assert(!nift::ast::parse_expression(source).supported);
        assert(nift::ast::parse_literal_payload_expression(source).supported);
        assert(!nift::ast::parse_literal_payload_expression(std::string(300,'(')+source+std::string(300,')')).supported);
        assert(!nift::ast::parse_literal_payload_expression("\""+std::string(131072,'a')+"\"").supported);
        assert(!Parser::prepare_pure_string_plan("\""+std::string(16384,'a')+"\".replace(\"a\",\"x\",)",true));
        std::string chain="\""+std::string(120000,'a')+"\"";for(unsigned i=0;i<1000;++i)chain+=".trim()";
        chain+=".replace(\"a\",\"x\")";
        assert(!Parser::prepare_pure_string_plan(chain,true));
        assert(!nift::ast::parse_literal_payload_expression(chain).supported);
        std::cout<<"PASS bounded canonical cache: peak owned-capacity allowance="<<peak_bytes<<" entries="<<peak_entries<<"\n";
    }
};
int main(){ScriptRenderHost host(std::filesystem::current_path());TrackedInfo tracked;Parser parser(host,tracked);SourceTranslationTestAccess::guard(parser);}
