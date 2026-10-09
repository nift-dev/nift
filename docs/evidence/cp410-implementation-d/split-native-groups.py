from pathlib import Path
import shutil
p=Path(__file__).resolve().parent;f=p/'certification/src/ParserExpression.cpp';s=f.read_text()
def wrap(a,b,name,indent):
 global s
 pad=' '*indent
 prefix=f'''#ifdef NIFT_COMPAT_SPLIT_FRAMES
{pad}bool group_handled = true;
{pad}const auto {name} = [&]() NIFT_COMPAT_NOINLINE -> bool {{
#endif
'''
 suffix=f'''#ifdef NIFT_COMPAT_SPLIT_FRAMES
{pad}    group_handled = false;return false;
{pad}}};
{pad}const bool group_result = {name}();
{pad}if (group_handled) return group_result;
#endif
'''
 # Scope helper status only in sanitizer builds, without adding ordinary scopes.
 prefix='#ifdef NIFT_COMPAT_SPLIT_FRAMES\n'+pad+'{\n#endif\n'+prefix
 suffix+='#ifdef NIFT_COMPAT_SPLIT_FRAMES\n'+pad+'}\n#endif\n'
 s=s[:a]+prefix+s[a:b]+suffix+s[b:]
# Independent native families share argument/path helpers from the parent frame.
markers=['            if(call_args("cmd",args,q))','            if(call_args("ffi_open",args,q))','            if(call_args("setenv",args,q))','            // Native script-land Minify++','            if(call_args("file",args,q))','            if(call_args("ifstream",args,q)']
a=s.index(markers[0]);end=s.index('#ifdef NIFT_COMPAT_SPLIT_FRAMES\n                handled = false;',a);bounds=[s.index(m,a) for m in markers]+[end]
for i in reversed(range(len(markers))):wrap(bounds[i],bounds[i+1],'evaluate_native_family_'+str(i),12)
# Split value families after receiver evaluation, keeping lazy arguments shared.
markers=['                    if(base.is_timer())','                    if(base.is_string() && base.string.rfind("\\x1fnift:",0)!=0)', '                    if(method=="to_string" && base.is_number())','                    if(base.is_object())','                    if(base.is_array())']
a=s.index(markers[0]);end=s.index('                    if(method=="to_string" && (base.is_array()',a);bounds=[s.index(m,a) for m in markers]+[end]
for i in reversed(range(len(markers))):wrap(bounds[i],bounds[i+1],'evaluate_value_family_'+str(i),20)
f.write_text(s);shutil.copy2(f,p/'prototype/src/ParserExpression.cpp')
# Propagate the safety-only changes to the private two-line write experiment.
f2=p.parent/'concat/prototype/src/ParserExpression.cpp';f2.write_text(s.replace('d=render_expression_value(v);if(','d=v.is_string()?std::move(v.string):render_expression_value(v);if(',1))
print('Split native and value families only in sanitizer builds')
