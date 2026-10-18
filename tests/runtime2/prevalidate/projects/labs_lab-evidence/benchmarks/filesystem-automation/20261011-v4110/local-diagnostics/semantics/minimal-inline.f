fn(workload(kind,root,dest,n,variant)) { print("EXECUTE"); return [[kind,n,variant]] }
p := ["walk","root","dest","3","0"]
print(workload(p[0],p[1],p[2],p[3].to_int(),p[4].to_int()).stringify())
