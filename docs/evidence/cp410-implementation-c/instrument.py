from pathlib import Path
p=Path('.build/cp410-implementation/filesystem');f=p/'ParserHelpers.h';s=f.read_text();pos=s.index('enum class GlobPrefixEvent');s=s[:pos]+'''enum class FilesystemPlanEvent { Prepare, Recipe, Backend, Operand, Pure, Reject, Fallback };
#ifdef NIFT_TEST_FS_RECIPE_STATS
NIFT_PARSER_HELPER_HIDDEN void record_filesystem_plan_event(FilesystemPlanEvent event);
#else
inline void record_filesystem_plan_event(FilesystemPlanEvent) {}
#endif
'''+s[pos:];f.write_text(s)
f=p/'ParserExpression.cpp';s=f.read_text();pos=s.index('bool Parser::execute_filesystem_operation');s=s[:pos]+'''#ifdef NIFT_TEST_FS_RECIPE_STATS
namespace nift::detail {
struct FilesystemPlanStats {
    unsigned long long prepares=0,recipes=0,backends=0,operands=0,pure=0,rejects=0,fallbacks=0;
    ~FilesystemPlanStats(){if(std::getenv("NIFT_TEST_FS_RECIPE_STATS"))std::fprintf(stderr,"fs-plan prepares=%llu recipes=%llu backends=%llu operands=%llu pure=%llu rejects=%llu fallbacks=%llu\\n",prepares,recipes,backends,operands,pure,rejects,fallbacks);}
} filesystem_plan_stats;
void record_filesystem_plan_event(FilesystemPlanEvent event){
    auto& s=filesystem_plan_stats;
    switch(event){case FilesystemPlanEvent::Prepare:++s.prepares;break;case FilesystemPlanEvent::Recipe:++s.recipes;break;case FilesystemPlanEvent::Backend:++s.backends;break;case FilesystemPlanEvent::Operand:++s.operands;break;case FilesystemPlanEvent::Pure:++s.pure;break;case FilesystemPlanEvent::Reject:++s.rejects;break;case FilesystemPlanEvent::Fallback:++s.fallbacks;break;}
}
}
#endif

'''+s[pos:]
s=s.replace('''    const bool moving=kind==PreparedFilesystemOperation::Kind::Move;''','''    nift::detail::record_filesystem_plan_event(nift::detail::FilesystemPlanEvent::Backend);
    auto evaluate_operand=[&](std::size_t i,nift::RuntimeValue& value){nift::detail::record_filesystem_plan_event(nift::detail::FilesystemPlanEvent::Operand);return operand(i,value);};
    const bool moving=kind==PreparedFilesystemOperation::Kind::Move;''',1)
a=s.index('bool Parser::execute_filesystem_operation');b=s.index('std::shared_ptr<const nift::ast::Expr>',a);block=s[a:b];block=block.replace('!operand(0,path_value)','!evaluate_operand(0,path_value)').replace('!operand(i,value)','!evaluate_operand(i,value)');s=s[:a]+block+s[b:]
s=s.replace('''    if(!eligible(expression))return false;''','''    if(!eligible(expression)){nift::detail::record_filesystem_plan_event(nift::detail::FilesystemPlanEvent::Reject);return false;}
    nift::detail::record_filesystem_plan_event(nift::detail::FilesystemPlanEvent::Pure);''',1)
s=s.replace('''    const auto open=source.find('(');''','''    nift::detail::record_filesystem_plan_event(nift::detail::FilesystemPlanEvent::Prepare);
    const auto open=source.find('(');''',1)
s=s.replace('#include <algorithm>','#include <algorithm>\n#ifdef NIFT_TEST_FS_RECIPE_STATS\n#include <cstdio>\n#endif',1);f.write_text(s)
f=p/'ParserTemplate.cpp';s=f.read_text();s=s.replace('''        if(found->second)return evaluate_filesystem_expression(x,out,e,prepared_view->slice(span.begin,span.end-span.begin),found->second.get());''','''        if(found->second){nift::detail::record_filesystem_plan_event(nift::detail::FilesystemPlanEvent::Recipe);return evaluate_filesystem_expression(x,out,e,prepared_view->slice(span.begin,span.end-span.begin),found->second.get());}
        nift::detail::record_filesystem_plan_event(nift::detail::FilesystemPlanEvent::Fallback);''',1);f.write_text(s)
print('Added test-only recipe/phase/operand/eligibility counters')
