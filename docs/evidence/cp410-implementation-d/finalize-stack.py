from pathlib import Path
import shutil
p=Path(__file__).resolve().parent;f=p/'certification/src/ParserExpression.cpp';s=f.read_text()
macro='''// Lifetime ASan cannot reuse the compatibility evaluator's many stack slots.
// Keep independent dispatch frames there; ordinary builds retain inlining choices.
#if defined(__SANITIZE_ADDRESS__)
#define NIFT_COMPAT_NOINLINE __attribute__((noinline))
#elif defined(__clang__)
#if __has_feature(address_sanitizer)
#define NIFT_COMPAT_NOINLINE __attribute__((noinline))
#else
#define NIFT_COMPAT_NOINLINE
#endif
#else
#define NIFT_COMPAT_NOINLINE
#endif

'''
s=s.replace('#include <algorithm>\n','#include <algorithm>\n'+macro)
s=s.replace('#if defined(__GNUC__) || defined(__clang__)\n                __attribute__((noinline))\n#endif','                NIFT_COMPAT_NOINLINE').replace('#if defined(__GNUC__) || defined(__clang__)\n                        __attribute__((noinline))\n#endif','                        NIFT_COMPAT_NOINLINE')
for i,name in enumerate(['lambda_factory','native_call','presentation','postfix','collection_method','array_method','resource_method','callable_call']):s=s.replace(f'expression_part_{i}',f'evaluate_{name}')
s+='\n#undef NIFT_COMPAT_NOINLINE\n';f.write_text(s);shutil.copy2(f,p/'prototype/src/ParserExpression.cpp')
print('final sanitizer boundaries applied')
