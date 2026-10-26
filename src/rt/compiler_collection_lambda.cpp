#include "rt/compiler_collection_lambda.h"
#include "rt/collection.h"
#include "rt/string.h"
// rt-lint: cold-file compile-time callback entry lowering
namespace nift::rt::detail {
bool UnitCompiler::collection_callback_shape(const Node& site,NodeId callback,std::uint32_t argc) {
    const auto kind=kinds_.of(callback);
    const std::string reason="method:"+std::string(text(ast_.at(ast_.at(site.a).b)));
    if(kind&kKindUnknown)return fail(site,reason);
    // Quoted selectors use legacy expression validation; not admitted by S1.
    if(expression_result_&&(kind&kKindString))return fail(site,"eval:quoted-callback");
    if(!(kind&kKindFunction))return true;
    const auto targets=kinds_.callable_targets.find(callback);
    if(targets==kinds_.callable_targets.end()||targets->second.empty())return fail(site,reason);
    for(NodeId id:targets->second) {
        const Node& lambda=ast_.at(id);
        if(lambda.kind!=NodeKind::Lambda||!(lambda.flags&kFlagBlockBody)||lambda.count!=argc)continue;
        const Node& body=ast_.at(lambda.b);
        if(body.count!=1)return fail(site,reason);
        const Node& n=ast_.at(ast_.list(body)[0]);
        if(n.kind==NodeKind::ExprStmt||n.kind==NodeKind::Assign||n.kind==NodeKind::CompoundAssign)continue;
        if(n.kind!=NodeKind::Return||n.a==kNoNode)return fail(site,reason);
        const Node* left=&ast_.at(n.a);
        while(left->kind==NodeKind::Binary||left->kind==NodeKind::Logical)left=&ast_.at(left->a);
        if(left->kind!=NodeKind::Ident&&left->kind!=NodeKind::Number)return fail(site,reason);
        const auto prefix=ast_.source().substr(n.begin,left->begin-n.begin);
        if(prefix.substr(0,prefix.find_last_not_of(" \t\r\n")+1)!="return")return fail(site,reason);
    }
    return true;
}

bool UnitCompiler::compile_lambda_body(const Node& lambda,const NodeId* params,bool collection_view) {
    bool unreachable_parameters = false;
    for (std::uint32_t k = 0; k < lambda.count; ++k)
        unreachable_parameters = unreachable_parameters || kinds_.of(params[k]) == 0;
    // Empty inputs justify eliding pure bodies, never hiding an unsupported
    // call, suspension, nested callable or opaque body from whole-unit checks.
    if(unreachable_parameters) {
        std::vector<NodeId> pending{lambda.b};
        while(!pending.empty()&&unreachable_parameters) {
            const auto id=pending.back();pending.pop_back();if(id==kNoNode)continue;
            const Node& n=ast_.at(id);
            if(n.kind==NodeKind::Call||n.kind==NodeKind::Await||n.kind==NodeKind::Lambda||
               n.kind==NodeKind::Opaque||n.kind==NodeKind::Command||n.kind==NodeKind::TemplateStmt) {
                unreachable_parameters=false;break;
            }
            for(NodeId child:{n.a,n.b,n.c})pending.push_back(child);
            for(std::uint32_t k=0;k<n.count;++k)pending.push_back(ast_.list(n)[k]);
        }
    }
    bool ok;
    if (unreachable_parameters) {
        // A closed-unit callable with no incoming element values cannot run.
        fb_->emit(OpCode::ReturnNull);
        ok = true;
    } else if (collection_view) {
        const Node& body=ast_.at(lambda.b);
        if(body.count!=1)return fail(lambda,"method:map");
        const NodeId statement_id=ast_.list(body)[0];
        const Node& n=ast_.at(statement_id);
        std::uint16_t value=0;
        if(n.kind==NodeKind::ExprStmt)ok=temp(value)&&expr_to(n.a,value);
        else if(n.kind==NodeKind::Assign||n.kind==NodeKind::CompoundAssign)
            ok=statement(statement_id)&&temp(value)&&expr_to(n.a,value);
        else if(n.kind==NodeKind::Return&&n.a!=kNoNode) {
            const Node* left=&ast_.at(n.a);
            while(left->kind==NodeKind::Binary||left->kind==NodeKind::Logical)left=&ast_.at(left->a);
            if(left->kind!=NodeKind::Ident&&left->kind!=NodeKind::Number)return fail(lambda,"method:map");
            const auto prefix=ast_.source().substr(n.begin,left->begin-n.begin);
            if(prefix.substr(0,prefix.find_last_not_of(" \t\r\n")+1)!="return")return fail(lambda,"method:map");
            const std::string message="unknown value or malformed expression: "+
                std::string(ast_.source().substr(n.begin,left->end-n.begin));
            std::uint16_t constant=0;
            ok=temp(value)&&fb_->constant(make_string(message),constant);
            if(ok) {
                fb_->emit(OpCode::LoadK,value,constant&0x7fffu);
                fb_->set_offset(n.begin);
                fb_->emit(OpCode::CollectionStep,value,static_cast<unsigned>(CollectionMethod::Map),99);
            }
        } else return fail(lambda,"method:map");
        if(ok)fb_->emit(OpCode::Return,value);
    } else if (lambda.flags & kFlagBlockBody) {
        ok = statements(ast_.at(lambda.b));
        if (ok) {
            const auto& body = ast_.at(lambda.b);
            if (body.count == 0 || ast_.at(ast_.list(body)[body.count - 1]).kind != NodeKind::Return)
                fb_->session_returns |= kKindNull;
            fb_->emit(OpCode::ReturnNull);
        }
    } else {
        std::uint16_t value = 0;
        const Node& body=ast_.at(lambda.b);
        if(body.kind==NodeKind::Assign||body.kind==NodeKind::CompoundAssign)
            ok=statement(lambda.b)&&temp(value)&&expr_to(body.a,value);
        else ok = temp(value) && expr_to(lambda.b, value);
        if (ok) fb_->emit(OpCode::Return, value);
    }
    return ok;
}
}
