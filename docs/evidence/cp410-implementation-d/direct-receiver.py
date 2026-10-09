from pathlib import Path
p=Path(__file__).resolve().parent;s=(p/'ParserExpression.cpp').read_text()
old='if(!eval(nift::detail::SourceText(recipe->receiver,text.view.slice(0,recipe->receiver.size())),base,depth+1))return false;'
new='''// Eligibility proved a live FileValue in the exact logical slot.
                // Canonical direct resolution performs the same sync and copy;
                // it invokes no user code and cannot select a different owner.
                if(!resolve_direct(recipe->receiver,base))return false;'''
assert old in s;s=s.replace(old,new,1);(p/'ParserExpression.cpp').write_text(s)
print('File-only receiver resolution uses the existing canonical direct resolver')
