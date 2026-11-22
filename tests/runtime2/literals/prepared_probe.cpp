// Compare JSON conversion, prepared literal parsing and evaluation on the
// toolchain that built the tested CLI. The parity suites still give verdicts.
#include "Ast.h"
#include "RuntimeJson.h"

#include <iostream>
#include <string>

static void show(const char* stage, const nift::RuntimeValue& value) {
    std::cout << "  " << stage << " type=" << int(value.type)
              << " stored=" << value.string << " output=" << value.dump(0) << '\n';
}

int main() {
    for (const char* token : {"0.0", "-0.00", "1e300", "1e-400", "{\"x\":1e-400}"}) {
        std::cout << "literal probe " << token << '\n';
        std::string error;
        json::Document parsed_json;
        if (json::Document::parse(token, parsed_json, error))
            show("json", nift::runtime_from_json(parsed_json));
        else
            std::cout << "  json failed: " << error << '\n';
        auto prepared = nift::ast::parse_expression(token);
        if (!prepared.supported) {
            std::cout << "  prepared parse failed: " << prepared.error << '\n';
            continue;
        }
        if (prepared.expr->kind == nift::ast::Kind::Literal)
            show("prepared literal", prepared.expr->literal);
        nift::RuntimeValue evaluated;
        nift::ast::Context context;
        if (nift::ast::evaluate(*prepared.expr, context, evaluated, error))
            show("prepared result", evaluated);
        else
            std::cout << "  prepared evaluator failed: " << error << '\n';
    }
}
