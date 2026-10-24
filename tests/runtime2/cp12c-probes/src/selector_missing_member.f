f:=()=>0
o:={"a":{"x":1}}
for((k,v):o){f = ()=>v.x}
o={}
print(f())
