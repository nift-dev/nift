#include "Parser.h"
#include "ParserHelpers.h"
#include "ScriptHost.h"
#include <cassert>
struct SourceTranslationTestAccess {
    static void evaluate_slices(Parser& parser) {
        const std::string expression = "[1,\n  missing_value]";
        nift::RuntimeValue value; std::string message;
        assert(!parser.evaluate_expression(expression, value, message,
            nift::detail::SourceView::identity("expression.n", expression)));
        auto origin = parser.expression_failure_view_.locate(0);
        assert(origin.source == "expression.n" && origin.line == 2 && origin.column == 3);
        assert(message == "unknown value or malformed expression: missing_value");
        assert(parser.evaluate_expression("[1,2]", value, message = ""));
        assert(parser.expression_failure_view_.locate(0).column == 3);
    }
    static void definition_boundaries(Parser& parser) {
        const std::string field = "struct(origin_field_box) {\n    callback := () => missing_field\n}\norigin_box := origin_field_box()\norigin_box.callback()\n";
        auto result = parser.run_script(field, "field-definition.n");
        assert(!result.ok && result.diagnostic);
        assert(result.diagnostic->origin.source == "field-definition.n");
        assert(result.diagnostic->origin.line == 2 && result.diagnostic->origin.column == 23);
        const std::string initializer = "struct(origin_value_box) {\n    value := missing_initializer\n}\norigin_value := origin_value_box()\n";
        result = parser.run_script(initializer, "initializer-definition.n");
        assert(!result.ok && result.diagnostic);
        assert(result.diagnostic->origin.source == "initializer-definition.n");
        assert(result.diagnostic->origin.line == 2 && result.diagnostic->origin.column == 14);
        const std::string thread_definition = "origin_thread_lambda := () => missing_thread_origin";
        assert(parser.run_statement(thread_definition, "<lambda-thread-definition>").ok);
        assert(parser.run_statement("origin_thread := thread(origin_thread_lambda)", "<lambda-thread-create>").ok);
        for (int observation = 0; observation < 2; ++observation) {
            result = parser.run_statement("origin_thread.join()", "<lambda-thread-join>");
            assert(!result.ok && result.diagnostic);
            assert(result.error.source_file == "<lambda-thread-definition>");
            assert(result.error.column == thread_definition.find("missing_thread_origin") + 1);
            assert(result.error.source_line == thread_definition);
            assert(result.diagnostic->origin.source_line == thread_definition);
        }
        const std::string future_definition = "origin_future_lambda := async () => missing_future_origin";
        assert(parser.run_statement(future_definition, "<lambda-future-definition>").ok);
        assert(parser.run_statement("origin_future := origin_future_lambda()", "<lambda-future-create>").ok);
        for (int observation = 0; observation < 2; ++observation) {
            result = parser.run_statement("await origin_future", "<lambda-future-await>");
            assert(!result.ok && result.diagnostic);
            assert(result.error.source_file == "<lambda-future-definition>");
            assert(result.error.column == future_definition.find("missing_future_origin") + 1);
            assert(result.error.source_line == future_definition);
            assert(result.diagnostic->origin.source_line == future_definition);
        }
        parser.finalize_execution_workers();
    }
    static bool translate(Parser& parser, const std::string& text, std::string& output,
                          nift::detail::SourceView& view, nift::detail::DiagnosticOrigin& error) {
        std::string message;
        return parser.translate_function_program(text, output, message,
            nift::detail::SourceView::identity("original.n",text), &view, &error);
    }
};
int main() {
    const std::string arguments = "  first, nested(1, missing), \"a\\n,b\"";
    nift::detail::SourceText mapped_arguments(arguments,
        nift::detail::SourceView::identity("arguments.n", arguments));
    bool parameters_ok = false; std::vector<bool> quoted;
    auto parameters = nift::detail::parse_parameters(mapped_arguments, parameters_ok, &quoted);
    assert(parameters_ok && parameters.size() == 3 && quoted[2]);
    assert(parameters[0] == "first" && parameters[0].view.locate(0).column == 3);
    assert(parameters[1].substr(10, 7).view.locate(0).column == 20);
    assert(parameters[2] == "a\n,b" && parameters[2].view.locate(2).column == 30);
    ScriptRenderHost host(std::filesystem::current_path());TrackedInfo tracked;Parser parser(host,tracked);
    SourceTranslationTestAccess::evaluate_slices(parser);
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
    SourceTranslationTestAccess::definition_boundaries(parser);
}
