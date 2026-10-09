from pathlib import Path
import shutil
p=Path(__file__).resolve().parent;f=p/'certification/src/ParserExpression.cpp';s=f.read_text()
s=s.replace('#define NIFT_COMPAT_NOINLINE __attribute__((noinline))','#define NIFT_COMPAT_SPLIT_FRAMES 1\n#define NIFT_COMPAT_NOINLINE __attribute__((noinline))')
for name in ['lambda_factory','native_call','presentation','postfix','collection_method','array_method','resource_method','callable_call']:
 a=s.index('            bool handled = true;',s.index('const auto evaluate_slice') if name=='lambda_factory' else s.index('const bool result = evaluate_'+previous+'();'))
 b=s.index('-> bool {',a)+len('-> bool {')
 s=s[:a]+'#ifdef NIFT_COMPAT_SPLIT_FRAMES\n'+s[a:b]+'\n#endif'+s[b:]
 a=s.index('                handled = false;',b);b=s.index('            if (handled) return result;',a)+len('            if (handled) return result;')
 s=s[:a]+'#ifdef NIFT_COMPAT_SPLIT_FRAMES\n'+s[a:b]+'\n#endif'+s[b:];previous=name
# The method value dispatch ends before its enclosing postfix fallthrough.
a=s.index('                    bool value_handled = true;');b=s.index('-> bool {',a)+len('-> bool {')
s=s[:a]+'#ifdef NIFT_COMPAT_SPLIT_FRAMES\n'+s[a:b]+'\n#endif'+s[b:]
a=s.index('                        value_handled = false;');b=s.index('                    if (value_handled) return value_result;',a)+len('                    if (value_handled) return value_result;')
s=s[:a]+'#ifdef NIFT_COMPAT_SPLIT_FRAMES\n'+s[a:b]+'\n#endif'+s[b:]
s+='\n#undef NIFT_COMPAT_SPLIT_FRAMES\n';f.write_text(s);shutil.copy2(f,p/'prototype/src/ParserExpression.cpp')
