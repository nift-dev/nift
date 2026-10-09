from pathlib import Path
p=Path('.build/cp410-implementation/save/certification/src/ParserExpression.cpp')
s=p.read_text(); lines=s.splitlines(keepends=True)
starts=[1946,1992,2487,2500,2968,3078,3122,3153]
def end_brace(start):
 i=start; depth=0; quote=None; comment=None
 while i<len(s):
  c=s[i]; n=s[i:i+2]
  if comment=='line':
   if c=='\n':comment=None
  elif comment=='block':
   if n=='*/':comment=None;i+=1
  elif quote:
   if c=='\\':i+=1
   elif c==quote:quote=None
  elif n=='//':comment='line';i+=1
  elif n=='/*':comment='block';i+=1
  elif c in '\"\'':quote=c
  elif c=='{':depth+=1
  elif c=='}':
   depth-=1
   if depth==0:return i
  i+=1
 raise Exception('unbalanced')
edits=[]
for j,line in enumerate(starts):
 off=sum(map(len,lines[:line-1])); start=s.index('{',off); end=end_brace(start)
 name=f'expression_part_{j}'
 prefix='{\n            bool handled = true;\n            const auto '+name+' = [&]()\n#if defined(__GNUC__) || defined(__clang__)\n                __attribute__((noinline))\n#endif\n                -> bool {'
 suffix='\n                handled = false;\n                return false;\n            };\n            const bool result = '+name+'();\n            if (handled) return result;\n        }'
 edits.extend([(start,start+1,prefix),(end,end+1,suffix)])
for a,b,t in sorted(edits,reverse=True):s=s[:a]+t+s[b:]
p.write_text(s)
print('split',len(starts),'independent compatibility dispatch frames')
