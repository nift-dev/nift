from pathlib import Path
p=Path('.build/cp410-implementation/save');base=Path('.build/cp410-implementation/strings')
for name in ['Parser.h','ParserExpression.cpp','ParserTemplate.cpp','ParserHelpers.h','Ast.cpp','Ast.h']:(p/name).write_text((base/name).read_text())
(p/'Parser.cpp').write_text(Path('src/Parser.cpp').read_text())
f=p/'Parser.h';s=f.read_text().replace('''        bool open = false, dirty = false, existed_at_open = false;
''','''        bool open = false, dirty = false, existed_at_open = false;
        // A clean saved state aliases working without a second owned buffer.
        bool saved_is_working = false;
        void prepare_mutation(){if(saved_is_working){saved=working;saved_is_working=false;}}
        void update_dirty(){dirty=!saved_is_working&&working!=saved;if(!dirty){saved_is_working=true;saved.clear();}}
''',1);f.write_text(s)
f=p/'ParserExpression.cpp';s=f.read_text().replace('auto dirty=[&](){f->dirty=f->working!=f->saved;};','auto dirty=[&](){f->update_dirty();};',1)
s=s.replace('f->saved=data;f->working=m=="w"?std::string():data;', 'if(m=="w"){f->saved=std::move(data);f->working.clear();f->saved_is_working=false;}else{f->working=std::move(data);f->saved.clear();f->saved_is_working=true;}',1)
s=s.replace('f->working.clear();f->saved.clear();f->cursor=0;', 'f->working.clear();f->saved.clear();f->saved_is_working=false;f->cursor=0;',1)
# Snapshot only after argument evaluation and all validation, immediately before
# actual byte mutations; reentrant argument factories may save/revert/close.
s=s.replace('f->working.replace(base,ov,d);', 'if(ov!=d.size()||f->working.compare(base,ov,d)!=0)f->prepare_mutation();f->working.replace(base,ov,d);')
s=s.replace('f->working.replace(pos,a.size(),b);', 'if(a!=b)f->prepare_mutation();f->working.replace(pos,a.size(),b);')
s=s.replace('f->working.insert(pos,d);', 'if(!d.empty())f->prepare_mutation();f->working.insert(pos,d);')
s=s.replace('f->working=f->saved;f->cursor=0;f->dirty=false;', 'if(!f->saved_is_working)f->working=f->saved;f->saved_is_working=true;f->saved.clear();f->cursor=0;f->dirty=false;',1)
s=s.replace('f->saved=f->working;f->dirty=false;', 'f->saved_is_working=true;f->saved.clear();f->dirty=false;',1)
f.write_text(s)
f=p/'Parser.cpp';s=f.read_text().replace('''        file.saved.clear();
        file.cursor''','''        file.saved.clear();
        file.saved_is_working = false;
        file.cursor''',1);f.write_text(s)
print('Private saved-buffer alias / lazy snapshot candidate, all mutation sites')

f=p/'ParserTemplate.cpp';s=f.read_text().replace('                        f->working.replace(base,ov,d);','                        if(ov!=d.size()||f->working.compare(base,ov,d)!=0)f->prepare_mutation();\n                        f->working.replace(base,ov,d);',1).replace('                        f->dirty=f->working!=f->saved;','                        f->update_dirty();',1);f.write_text(s)
