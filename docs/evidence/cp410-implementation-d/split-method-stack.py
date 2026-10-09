from pathlib import Path
p=Path('.build/cp410-implementation/save/certification/src/ParserExpression.cpp');s=p.read_text()
a=s.index('                    bool aok=false; std::vector<bool> aq;',s.index('const auto expression_part_3'))
end_marker='                }\n            }\n        \n                handled = false;'
b=s.index(end_marker,a)
prefix='''                    bool value_handled = true;
                    const auto evaluate_method_value = [&]()
#if defined(__GNUC__) || defined(__clang__)
                        __attribute__((noinline))
#endif
                        -> bool {
'''
suffix='''                        value_handled = false;
                        return false;
                    };
                    const bool value_result = evaluate_method_value();
                    if (value_handled) return value_result;
'''
s=s[:a]+prefix+s[a:b]+suffix+s[b:];p.write_text(s)
