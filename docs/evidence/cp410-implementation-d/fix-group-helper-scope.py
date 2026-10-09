from pathlib import Path
p=Path(__file__).resolve().parent;f=p/'certification/src/ParserExpression.cpp';s=f.read_text()
name='evaluate_value_family_0';start=s.index('const auto '+name);helper=s.index('                    // CP171:',start);after=s.index('                    if(base.is_string() && base.string.rfind("\\x1fnift:cmd:',helper)
suffix_start=s.index('#ifdef NIFT_COMPAT_SPLIT_FRAMES\n                        group_handled = false;',after)
suffix_end=s.index('                    }\n#endif\n',suffix_start)+len('                    }\n#endif\n')
suffix=s[suffix_start:suffix_end]
prefix='''#ifdef NIFT_COMPAT_SPLIT_FRAMES
                    {
#endif
#ifdef NIFT_COMPAT_SPLIT_FRAMES
                    bool group_handled = true;
                    const auto evaluate_resource_file_methods = [&]() NIFT_COMPAT_NOINLINE -> bool {
#endif
'''
# Existing first family now ends before the shared key-conversion helper.
s=s[:suffix_start]+suffix.replace(name,'evaluate_resource_file_methods')+s[suffix_end:]
s=s[:after]+prefix+s[after:];s=s[:helper]+suffix+s[helper:];f.write_text(s)
(p/'prototype/src/ParserExpression.cpp').write_text(s)
(p.parent/'concat/prototype/src/ParserExpression.cpp').write_text(s.replace('d=render_expression_value(v);if(','d=v.is_string()?std::move(v.string):render_expression_value(v);if(',1))
print('Shared key helper remains in parent dispatch scope')
