#include "Ast.h"
#include <cassert>
int main(){
    const std::string text="outer(\n  1,\n  inner(missing_value))";
    auto parsed=nift::ast::parse_expression(text);assert(parsed.supported);
    assert(parsed.expr->items[1]->items[0]->span.begin==text.find("missing_value"));
    auto method=nift::ast::parse_expression("a.map(\n missing_value)");assert(method.supported);
    assert(method.expr->items[0]->span.begin==8);
    auto statement=nift::ast::parse_statement("\t x := [1,\n    missing_value]");assert(statement.supported);
    assert(statement.stmt->expr->items[1]->span.begin==15);
    auto condition=nift::ast::parse_statement("  while(\n missing_value) {}");assert(condition.supported);
    assert(condition.stmt->condition->span.begin==10);
    auto offset=nift::ast::parse_expression_at("prefix missing_value",7);assert(offset.supported);
    assert(offset.expr->span.begin==7);
    nift::ast::Context context;std::size_t failure=std::string::npos;
    context.resolve=[](const std::string&,nift::RuntimeValue&,std::string& e){e="missing";return false;};
    context.call=[](const std::string&,std::vector<nift::RuntimeValue>&&,nift::RuntimeValue&,std::string&){return true;};
    context.legacy=[](const std::string&,nift::RuntimeValue&,std::string&){return false;};
    context.failure_origin=[&](nift::ast::SourceSpan span){if(failure==std::string::npos)failure=span.begin;};
    nift::RuntimeValue result;std::string error;
    assert(!nift::ast::evaluate(*parsed.expr,context,result,error));
    assert(failure==text.find("missing_value"));
    nift::ast::rebase(*parsed.expr,10);
    assert(parsed.expr->items[1]->items[0]->span.begin==text.find("missing_value")+10);
}
