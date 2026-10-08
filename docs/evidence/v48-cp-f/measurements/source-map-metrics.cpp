#include "Parser.h"
#include "ScriptHost.h"
#include <fstream>
#include <iostream>
#include <iterator>
struct SourceTranslationTestAccess {
    static bool translate(Parser& parser,const std::string& source,const std::filesystem::path& path,
                          std::string& output,nift::detail::SourceView& view) {
        std::string error;return parser.translate_function_program(source,output,error,
            nift::detail::SourceView::identity(path,source),&view);
    }
};
int main(int argc,char**argv) {
    ScriptRenderHost host(std::filesystem::current_path());TrackedInfo tracked;Parser parser(host,tracked);
    std::cout<<"file original_bytes transformed_bytes spans logical_mapping_bytes line_index_bytes\n";
    for(int arg=1;arg<argc;++arg){
        std::ifstream stream(argv[arg]);std::string source((std::istreambuf_iterator<char>(stream)),{}),output;
        nift::detail::SourceView view;
        if(!SourceTranslationTestAccess::translate(parser,source,argv[arg],output,view))return 2;
        std::cout<<std::filesystem::path(argv[arg]).filename().string()<<" "<<source.size()<<" "<<output.size()<<" "
                 <<view.span_count()<<" "<<view.mapping_bytes()<<" "<<view.document()->line_starts.size()*sizeof(std::size_t)<<"\n";
    }
}
