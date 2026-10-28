#include "rt/session.h"
#include "rt/session_history.h"
#include "rt/session_shape.h"
#include "rt/parser.h"
#include "rt/cell.h"
#include "rt/aggregate.h"
#include "rt/string.h"
#include "rt_test.h"
#include <iostream>
using namespace nift::rt;
using Facts = std::vector<SessionCallFact>;
namespace {
std::shared_ptr<const Facts> facts(VmSession& session) {
    auto value = session.value("g");
    CHECK(value.is(Kind::Function));
    return value.is(Kind::Function) ? static_cast<const Proto*>(value.heap_as<FunctionObject>()->code)->
        session_call_facts : nullptr;
}
CompileResult boundary(const std::string& expression, bool historical, bool unknown_result,
                       SessionCompileContext& context) {
    const std::string source = unknown_result ? "fn(g(p)){p(0);return 0};x:=1" :
        "fn(g(p)){return p(0)*2};" + expression;
    Ast ast(source); const auto parsed = parse_script(ast);
    CHECK(parsed.ok);
    context.source_owner = std::make_shared<const std::string>(source);
    SessionSymbol symbol; symbol.name = "mystery"; symbol.slot = 0;
    symbol.kinds = symbol.contract = kKindFunction;
    symbol.structure = infer_detail::kinds_ty(kKindFunction);
    if (historical) symbol.structure = session_historical_shape(symbol.structure);
    context.symbols.push_back(std::move(symbol)); context.slots = 2;
    context.declarations.emplace("mystery", ast.add(NodeKind::Ident, 0, 0));
    for (NodeId id = 0; id < ast.size(); ++id) {
        const auto& node = ast.at(id);
        if (node.kind == NodeKind::Param && ast.text(node) == "p")
            context.parameter_seeds.emplace(id, session_historical_shape(infer_detail::kinds_ty(kKindFunction)));
        if (node.kind == NodeKind::Call && ast.at(node.a).kind == NodeKind::Ident &&
            ast.text(ast.at(node.a)) == "p")
            context.call_result_seeds.emplace((std::uint64_t(node.begin) << 32) | node.end,
                unknown_result ? infer_detail::any_ty() : infer_detail::kinds_ty(kKindNumber));
    }
    CompileLimits limits; limits.session = &context;
    return compile_unit(ast, parsed.root, limits);
}
void unknown_effect(bool admitted) {
    const std::string source = "fn(readv()){return a[0]*2};mystery();readv()";
    Ast ast(source); const auto parsed = parse_script(ast); CHECK(parsed.ok);
    SessionCompileContext context; context.source_owner=std::make_shared<const std::string>(source);
    SessionSymbol cb; cb.name="mystery"; cb.slot=0; cb.kinds=cb.contract=kKindUnknown;
    cb.structure=infer_detail::any_ty(); context.symbols.push_back(cb);
    SessionSymbol array; array.name="a"; array.slot=2; array.kinds=array.contract=kKindArray;
    auto type=std::make_shared<infer_detail::Ty>(); type->kinds=kKindArray;
    type->elem=infer_detail::kinds_ty(kKindNumber); array.structure=type; context.symbols.push_back(array);
    context.slots=4;
    context.declarations.emplace("mystery",ast.add(NodeKind::Ident,0,0));
    context.declarations.emplace("a",ast.add(NodeKind::Ident,0,0));
    CompileLimits limits; limits.session=&context;
    auto compiled=compile_unit(ast,parsed.root,limits);
    if(admitted) CHECK(compiled.program!=nullptr);
    else { CHECK(!compiled.program); CHECK(compiled.reason=="session:current-callable-proof"); }
}
void partial_target(bool admitted) {
    const std::string source="fn(known(v)){return v+1};mystery(0)";
    Ast ast(source); const auto parsed=parse_script(ast); CHECK(parsed.ok);
    SessionCompileContext context; context.source_owner=std::make_shared<const std::string>(source);
    auto definition=std::make_shared<SessionDefinition>(); definition->name="known";
    definition->source=context.source_owner; definition->end=source.find(';');
    SessionCallable target; target.identity=1; target.definition=definition;
    auto shape=std::make_shared<SessionCallableShape>(); shape->complete=false;
    shape->unknown_target=true; shape->targets.push_back(target);
    SessionSymbol cb; cb.name="mystery"; cb.slot=0; cb.kinds=cb.contract=kKindFunction;
    cb.structure=infer_detail::kinds_ty(kKindFunction); cb.callable_shape=shape;
    context.symbols.push_back(cb); context.slots=2;
    context.declarations.emplace("mystery",ast.add(NodeKind::Ident,0,0));
    CompileLimits limits; limits.session=&context;
    auto compiled=compile_unit(ast,parsed.root,limits);
    if(admitted) CHECK(compiled.program!=nullptr);
    else { CHECK(!compiled.program); CHECK(compiled.reason=="session:current-callable-proof"); }
}
void execute_bad(CompileResult& result, std::size_t slots) {
    if(!result.program)std::cerr<<"UNEXPECTED refusal "<<result.reason<<'\n';
    CHECK(result.program != nullptr);
    if (!result.program) return;
    Proto callback; callback.nparams = callback.nregs = 1;
    callback.code = {{OpCode::LoadK,0,0,0,0},{OpCode::Return,0,0,0,0}};
    callback.offsets = {0,0}; callback.notes = {0,0}; callback.constants.push_back(make_string("bad"));
    auto* function = new FunctionObject(); function->code = &callback;
    function->is_lambda = function->expression_body = true;
    std::vector<Value> globals(slots);
    globals[0] = make_cell(Value::adopt(function), 4); globals[1] = Value::integer(4);
    VmOptions options; options.session_globals = &globals;
    Proto halt; halt.code = {{OpCode::Halt,0,0,0,0}}; halt.offsets={0}; halt.notes={0};
    Program bootstrap; bootstrap.protos.push_back(std::make_unique<Proto>(std::move(halt)));
    Vm vm(bootstrap, options); std::vector<const Proto*> linked;
    for (const auto& proto : result.program->protos) linked.push_back(proto.get());
    std::string output; const auto stopped = vm.run_session_unit(*result.program, linked, output);
    CHECK(stopped.reason == StopReason::Internal);
    CHECK(stopped.message == "unexpected operands (compiler defect): arithmetic");
    CHECK(output.empty());
}
void identity(bool wrong) {
    const std::string source = "fn(g(p)){return p(0)}";
    auto old_source = std::make_shared<const std::string>(source);
    auto new_source = std::make_shared<const std::string>(source);
    Ast ast(source); const auto parsed = parse_script(ast); CHECK(parsed.ok);
    auto proof = std::make_shared<Facts>();
    for (NodeId id=0;id<ast.size();++id) if (ast.at(id).kind==NodeKind::Call)
        proof->push_back({old_source,ast.at(id).begin,ast.at(id).end,infer_detail::kinds_ty(kKindNumber)});
    auto definition = std::make_shared<SessionDefinition>(); definition->name="g";
    definition->source=old_source; definition->end=source.size();
    SessionCallable target; target.identity=1; target.definition=definition; target.call_facts=proof;
    auto shape=std::make_shared<SessionCallableShape>(); shape->targets.push_back(target);
    SessionCompileContext current,pending; SessionSymbol symbol; symbol.name="g";symbol.callable_shape=shape;
    current.symbols.push_back(symbol);
    SessionText text; text.text=source; text.imports.emplace("g",definition);
    text.pieces.push_back({"new",new_source,0,static_cast<std::uint32_t>(source.size())});
    prepare_session_history(ast,text,current,pending);
    CHECK(pending.call_result_seeds.empty() != wrong);
}
void lifetime(bool cached) {
    std::weak_ptr<const Facts> weak;
    std::weak_ptr<const Program> owner;
    Value retained;
    {
        VmSession session; std::string output;
        auto r=session.execute("a:=[1];fn(g(p)){return p(0)+a[0]};warm:=g(v=>v+1)","seed",output);
        CHECK(r.admitted && r.execution.reason==StopReason::None);
        auto first=facts(session); CHECK(first && first->size()==1); weak=first; first.reset();
        r=session.execute("a.push(\"bad\")","widen",output);
        CHECK(r.admitted && r.execution.reason==StopReason::None);
        CHECK(weak.expired() != cached);
        for(unsigned k=0;k<64;++k) {
            auto before=facts(session); std::weak_ptr<const Facts> previous=before; before.reset();
            r=session.execute("g(v=>v+"+std::to_string(k)+")","current",output);
            CHECK(r.admitted && r.execution.reason==StopReason::None);
            CHECK(previous.expired() != cached);
            auto proof=facts(session); CHECK(proof && proof->size()==1);
            CHECK(session.retained_code_units()<=3);
        }
        auto proof=facts(session); weak=proof;proof.reset();
        retained=session.value("g"); owner=session_function(retained.heap_as<FunctionObject>())->program_owner;
        session.close(); CHECK(!weak.expired() && !owner.expired());
    }
    retained=Value(); CHECK(owner.expired()); CHECK(weak.expired() != cached);
}
void shared_callable(bool internal) {
    VmSession session;std::string output;
    auto r=session.execute("fn(applycb(v)){return v()};fn(makeo()){a:=[()=>1];return {\"putv\":v=>{a[0]=v;return 0},\"readv\":()=>{q:=a[0];return applycb(q)*2}}};o:=makeo();warm:=o.putv(()=>1)","seed",output);
    CHECK(r.admitted && r.execution.reason==StopReason::None);
    r=session.execute("o.putv(()=>\"bad\");print(\"WROTE\");o.readv()","same-unit",output);
    CHECK(r.admitted && r.vm_executions==1 && !r.execution.catchable && output=="WROTE\n");
    CHECK(r.execution.reason==(internal ? StopReason::Internal : StopReason::Error));
    CHECK(r.execution.message==(internal ? "unexpected operands (compiler defect): arithmetic" :
        "return: arithmetic operators require numeric operands"));
}
void windows(bool broken) {
    for(const auto* declaration:{"fn(addv(a,b)){return a*10+b}","addv:=(a,b)=>(a*10+b)"}) {
        VmSession session;std::string output;
        auto r=session.execute(std::string(declaration)+";x:=1;y:=2","seed",output);
        CHECK(r.admitted && r.execution.reason==StopReason::None);
        r=session.evaluate("addv(x,y)","call",output);
        CHECK(r.admitted && r.vm_executions==1);
        if(r.execution.reason!=(broken ? StopReason::Internal : StopReason::None))
            std::cerr<<"WINDOW reason="<<static_cast<int>(r.execution.reason)<<" message="<<r.execution.message<<" int="<<(r.value.is(Kind::Int)?r.value.as_int():-99)<<"\n";
        CHECK(r.execution.reason==(broken ? StopReason::Internal : StopReason::None));
        if(broken)CHECK(r.execution.message=="unexpected operands (compiler defect): arithmetic");
        else CHECK(r.value.is(Kind::Int) && r.value.as_int()==12);
    }
}
void array_payload(bool broken) {
    VmSession session;std::string output;
    auto r=session.execute("fn(g(p)){return p+1};fn(h(p)){return p+2};a:=[g,h]","seed",output);
    CHECK(r.admitted && r.execution.reason==StopReason::None);
    const auto array=session.value("a");CHECK(array_size(array)==2);
    CHECK(array_items(array)[0].is(Kind::Function));
    CHECK(array_items(array)[1].is(Kind::Function) != broken);
    r=session.execute("f:=a[0];f(3)","alias",output);
    CHECK(r.admitted && r.execution.reason==StopReason::None);
    if(broken) return;  // M22 corrupts element 1; element 0 remains a proved callable.
    r=session.execute("f = a[1]","replace",output);
    CHECK(r.admitted && r.execution.reason==(broken ? StopReason::Error : StopReason::None));
    r=session.evaluate("f(3)","invoke",output);
    CHECK(r.admitted && r.execution.reason==StopReason::None && r.value.as_int()==(broken?4:5));
}
void reporting_arguments(bool lost) {
    VmSession session;std::string output;
    auto r=session.execute("fn(arg()){return 1};fn(readx(v)){return x*2};fn(h()){return readx(arg())};throw error(\"stop\");x:=1","seed",output);
    CHECK(r.admitted && r.execution.reason==StopReason::Error);
    r=session.execute("x:=\"bad\"","fresh",output);CHECK(r.admitted && r.execution.reason==StopReason::None);
    r=session.evaluate("h()","report",output);CHECK(r.admitted && r.execution.reason==StopReason::Error);
    CHECK(r.execution.message==(lost ? "return: arithmetic operators require numeric operands" :
        "return: return: arithmetic operators require numeric operands"));
}
void reporting(bool raw) {
    VmSession session; std::string output;
    auto r=session.execute("fn(readx()){return x*2};fn(h()){return readx()};throw error(\"stop\");x:=1", "seed", output);
    CHECK(r.admitted && r.execution.reason==StopReason::Error);
    r=session.execute("x:=\"bad\"", "fresh", output);
    CHECK(r.admitted && r.execution.reason==StopReason::None);
    r=session.evaluate("h()", "report", output);
    CHECK(r.admitted && r.execution.reason==StopReason::Error && !r.execution.catchable);
    CHECK(r.execution.message==(raw ? "arithmetic operators require numeric operands" :
        "return: return: arithmetic operators require numeric operands"));
}
void history(bool refused) {
    VmSession session;std::string output;
    auto r=session.execute("a:=[1];fn(g(p)){return p(0)+a[0]};warm:=g(v=>v+1)","seed",output);
    CHECK(r.admitted && r.execution.reason==StopReason::None);
    const auto identity=session.value("g");const auto prior=facts(session);
    for(unsigned k=0;k<3;++k) {
        r=session.execute("a.push(\"bad\");leaked:=77;print(\"FORBIDDEN\");bytes([65])","failed",output);
        CHECK(!r.admitted && r.vm_executions==0 && r.legacy_executions==0 && output.empty());
        CHECK(!session.contains("leaked") && array_size(session.value("a"))==1);
        CHECK(session.value("g").heap()==identity.heap() && facts(session)==prior);
    }
    r=session.execute("a.push(\"bad\");print(\"ONCE\");throw error(\"stop\")","publish",output);
    if(refused) {
        CHECK(!r.admitted && r.refusal=="session:current-callable-proof" && r.vm_executions==0);
        CHECK(output.empty() && array_size(session.value("a"))==1);
    } else {
        CHECK(r.admitted && r.execution.reason==StopReason::Error && r.vm_executions==1);
        CHECK(output=="ONCE\n" && array_size(session.value("a"))==2);
        CHECK(session.value("g").heap()==identity.heap());
        r=session.evaluate("g(v=>v+2)","new-current",output);
        CHECK(r.admitted && r.execution.reason==StopReason::None && r.value.as_int()==3);
    }
}
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    const std::string test=argv[1],expect=argv[2];
    if(test=="unknown-effect")unknown_effect(expect=="admitted");
    else if(test=="partial-target")partial_target(expect=="admitted");
    else if(test=="reporting-arguments")reporting_arguments(expect=="lost");
    else if(test=="windows")windows(expect=="broken");
    else if(test=="shared-callable")shared_callable(expect=="internal");
    else if(test=="array")array_payload(expect=="broken");
    else if(test=="reporting")reporting(expect=="raw");
    else if(test=="history")history(expect=="refused");
    else if(test=="identity")identity(expect=="wrong-source");
    else if(test=="lifetime")lifetime(expect=="cached");
    else {
        SessionCompileContext context;
        const bool active=test.rfind("active",0)==0;
        const bool unknown_result=test=="unknown-result";
        std::string expression="g(mystery)";
        if(test=="active-method")expression="struct(C){fn(go(v)){return g(v)}};c:=C();c.go(mystery)";
        if(test=="active-constructor")expression="struct(C){fn(C(v)){g(v)}};c:=C(mystery)";
        if(test=="active-collection")expression="[mystery].map(v=>g(v))";
        if(test=="active-indirect")expression="invokev:=v=>g(v);invokev(mystery)";
        if(test=="field")expression="o:={\"cb\":mystery};q:=o.cb;g(q)";
        if(test=="element")expression="a:=[mystery];q:=a[0];g(q)";
        if(test=="return")expression="fn(id(v)){return v};g(id(mystery))";
        if(test=="alias")expression="alias:=mystery;g(alias)";
        if(test=="cell")expression="fn(pass()){v:=mystery;return ()=>v};producer:=pass();g(producer())";
        auto compiled=boundary(expression,active || unknown_result,unknown_result,context);
        if(expect=="internal")execute_bad(compiled,context.slots);
        else if(expect=="admitted")CHECK(compiled.program!=nullptr);
        else {
            CHECK(!compiled.program);
            const auto reason=unknown_result ? "session:current-callable-proof" :
                active || expect=="untainted" ? "session:active-historical-call" : "session:current-callable-proof";
            CHECK(compiled.reason==reason);
        }
    }
    const auto status=rt_test::finish("CP19 summary proof");
    if(!status)std::cout<<"WITNESS "<<test<<' '<<expect<<'\n';
    return status;
}
