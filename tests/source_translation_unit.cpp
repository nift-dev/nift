#include "Parser.h"
#include "ParserHelpers.h"
#include "ScriptHost.h"
#include <cassert>
struct SourceTranslationTestAccess {
    static bool translate(Parser& parser, const std::string& text, std::string& output,
                          nift::detail::SourceView& view, nift::detail::DiagnosticOrigin& error) {
        std::string message;
        return parser.translate_function_program(text, output, message,
            nift::detail::SourceView::identity("original.n",text), &view, &error);
    }
};
int main() {
    ScriptRenderHost host(std::filesystem::current_path());TrackedInfo tracked;Parser parser(host,tracked);
    const std::string input="first := 1\n\nfn(check()) {\n    for(i : [1]) {\n        missing_value\n    }\n}\n";
    std::string output;nift::detail::SourceView view;nift::detail::DiagnosticOrigin error;
    assert(SourceTranslationTestAccess::translate(parser,input,output,view,error));
    assert(output=="$[first := 1]@fn(check()){@for(i : [1]){$[missing_value]}}");
    const auto missing=output.find("missing_value");
    assert(view.locate(missing).source=="original.n");
    assert(view.locate(missing).line==5 && view.locate(missing).column==9);
    assert(view.locate(missing-2).line==5 && view.locate(missing-2).column==9);
    assert(view.slice(missing,13).slice(4,3).locate(0).column==13);
    assert(view.span_count()<30);
    const std::string body="\r\n\t  first\r\n\t  second\r\n";
    auto normalized=nift::detail::normalize_control_block_body(body,
        nift::detail::SourceView::identity("body.n",body));
    assert(normalized.text=="first\r\nsecond");
    assert(normalized.view.locate(0).line==2 && normalized.view.locate(0).column==4);
    assert(normalized.view.locate(7).line==3 && normalized.view.locate(7).column==4);
    assert(!SourceTranslationTestAccess::translate(parser,"first := 1\nwhile(true) {",output,view,error));
    assert(error.source=="original.n" && error.line==2 && error.column==1);
    assert(SourceTranslationTestAccess::translate(parser,"\n",output,view,error));
    assert(output.empty() && view.locate(0).line==2);
}
