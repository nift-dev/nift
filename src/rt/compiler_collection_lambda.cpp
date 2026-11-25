#include "rt/compiler_collection_lambda.h"
#include "rt/collection.h"
#include "rt/reserved_names.h"
#include "rt/string.h"
// rt-lint: cold-file compile-time callback entry lowering
namespace nift::rt::detail {
namespace {

// Collection expression views yield the value of a sole local declaration.
// Limit the admitted shape to a plain numeric initializer and an ordinary name.
bool collection_local_declaration(const Ast& ast, const Node& node) {
    return node.kind == NodeKind::Declare && !node.flags && node.b != kNoNode &&
           ast.at(node.a).kind == NodeKind::Ident && !reserved_name(ast.text(ast.at(node.a))) &&
           ast.at(node.b).kind == NodeKind::Number;
}

} // namespace

const Node* UnitCompiler::lambda_entry_duplicate(const Node& lambda) const {
    if (session_ || plan_.coroutines || (module_names_ && !module_names_->empty()) ||
        !(lambda.flags & kFlagBlockBody)) return nullptr;
    const Node& body = ast_.at(lambda.b);
    if (body.count != 2) return nullptr;
    const auto* statements = ast_.list(body);
    const Node& declaration = ast_.at(statements[0]);
    const Node& tail = ast_.at(statements[1]);
    if (declaration.kind != NodeKind::Declare || declaration.flags || declaration.b == kNoNode ||
        ast_.at(declaration.b).kind != NodeKind::Number || tail.kind != NodeKind::Return ||
        tail.a == kNoNode || ast_.at(tail.a).kind != NodeKind::Ident) return nullptr;
    const Node& target = ast_.at(declaration.a);
    if (target.kind != NodeKind::Ident || text(ast_.at(tail.a)) != text(target) || reserved_name(text(target)))
        return nullptr;
    for (std::uint32_t k = 0; k < lambda.count; ++k)
        if (text(ast_.at(ast_.list(lambda)[k])) == text(target)) return &target;
    bool top_level = false;
    for (NodeId id : top_level_decl_ids_) {
        const Node& candidate = ast_.at(id);
        if (candidate.kind == NodeKind::Declare && candidate.b == node_id(lambda)) top_level = true;
    }
    const auto capture = top_level_bindings_.find(text(target));
    if (top_level && capture != top_level_bindings_.end() && capture->second.decl_node != kNoNode &&
        ast_.at(capture->second.decl_node).begin < lambda.begin) return &target;
    return nullptr;
}

bool UnitCompiler::collection_callback_shape(const Node& site,NodeId callback,std::uint32_t argc) {
    const auto kind=kinds_.of(callback);
    const std::string reason="method:"+std::string(text(ast_.at(ast_.at(site.a).b)));
    if(kind&kKindUnknown)return fail(site,reason);
    // Quoted selectors use legacy expression validation; not admitted by S1.
    if(expression_result_&&(kind&kKindString))return fail(site,"eval:quoted-callback");
    if(!(kind&kKindFunction))return true;
    if (session_ && kinds_.historical_call((std::uint64_t(site.begin) << 32) | site.end)) return true;
    const auto targets=kinds_.callable_targets.find(callback);
    if(targets==kinds_.callable_targets.end()||targets->second.empty())return fail(site,reason);
    for(NodeId id:targets->second) {
        const Node& lambda=ast_.at(id);
        if(lambda.kind!=NodeKind::Lambda||!(lambda.flags&kFlagBlockBody))continue;
        const bool variadic = lambda.count && (ast_.at(ast_.list(lambda)[lambda.count-1]).flags & kFlagVariadic);
        if(variadic ? argc<lambda.count-1 : argc!=lambda.count)continue;
        const Node& body=ast_.at(lambda.b);
        if (lambda_entry_duplicate(lambda)) continue;
        if(body.count!=1)return fail(site,reason);
        const Node& n=ast_.at(ast_.list(body)[0]);
        if(n.kind==NodeKind::ExprStmt||n.kind==NodeKind::Assign||n.kind==NodeKind::CompoundAssign||
           collection_local_declaration(ast_, n))continue;
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
    if (!session_ && !plan_.coroutines && (!module_names_ || module_names_->empty())) {
        if (const Node* receiver = invalid_object_member_lambda(ast_, kinds_, lambda)) {
            const Binding* binding = find_binding(text(*receiver));
            if (binding && binding->loop_binding) {
                std::uint16_t scratch = 0;
                return temp(scratch) &&
                       legacy_fatal(ast_.at(lambda.b),
                                    "member assignment requires a struct instance: " + std::string(text(*receiver)),
                                    scratch);
            }
        }
    }
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
    if (const Node* duplicate = lambda_entry_duplicate(lambda)) {
        std::uint16_t scratch = 0;
        return temp(scratch) &&
               legacy_fatal(*duplicate, "binding already declared in this scope: " + std::string(text(*duplicate)),
                            scratch);
    }
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
        else if(collection_local_declaration(ast_, n))
            ok=statement(statement_id)&&temp(value)&&expr_to(n.a,value);
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
