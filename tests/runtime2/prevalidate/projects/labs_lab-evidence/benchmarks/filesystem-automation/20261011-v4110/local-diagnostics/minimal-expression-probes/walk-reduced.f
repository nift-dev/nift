fn(walk(root,rel)) {
 out := []
 for(fname : ls(root + "/" + rel)) {
  r := fname; if(rel != "") { r = rel + "/" + fname }
  d := is_dir(root + "/" + r); out.push([r,d])
  if(d) { for(e : walk(root,r)) { out.push(e) } }
 }
 return out
}
fn(checksum(b)) { h := 0; i := 0; while(i < b.size()) { h = (h*31+b[i])%1000000007; i += 1 }; return h }
fn(write(path,data)) { s := ofstream(path); s.write(data); s.close() }
fn(parent(rel)) { parts := rel.split("/"); out := ""; i := 0; while(i < parts.size()-1) { if(i > 0) { out += "/" }; out += parts[i]; i += 1 }; return out }
fn(execute(kind,root,dest,n,variant)) { print("EXECUTE"); rows := []; entries := walk(root,""); for(entry : entries) { rel := entry[0]; if(entry[1]) { rows.push([rel,-1,0]) } else { rows.push([rel,0,0]) } }; return rows }
print("BOOT")
while(true) { line := read(); if(line == null) { break }; p := line.split("|"); print(execute(p[0],p[1],p[2],p[3].to_int(),p[4].to_int()).stringify()) }
