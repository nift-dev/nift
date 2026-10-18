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
fn(execute(kind,root,dest,n,variant)) {
 rows := []; lines := []; entries := walk(root,"")
 if(kind == "relocate") { make_dir(dest + "/moved"); for(fname : ls(dest)) { if(fname != "moved") { move(dest + "/" + fname,dest + "/moved/" + fname) } } }
 for(entry : entries) {
  rel := entry[0]; dir := entry[1]; p := root + "/" + rel; q := dest + "/" + rel
  if(dir) { if(kind == "walk") { rows.push([rel,-1,0]) }; continue }
  if(kind == "walk") { rows.push([rel,0,0]); continue }
  if(kind == "mixed" && (rel.starts_with("group19/") || (!rel.ends_with(".md") && !rel.ends_with(".dat")))) { continue }; size := stat(p).size; v := 0
  if(kind == "filter") { if((variant == 101 && !rel.ends_with(".md")) || (variant == 102 && !rel.starts_with("group00/")) || (variant == 103 && size < 768)) { continue } }
  if(kind == "read" || kind == "hash-portable" || kind == "mirror" || kind == "detect-hash" || kind == "clean" || kind == "manifest" || kind == "generate" || kind == "mixed") { v = checksum(open_bytes(p)) }
  if(kind == "large-read") { data := open_bytes(p); v = data[0]*256 + data[data.size()-1] }
  if(kind == "detect-hash") { if(exists(q) && v == checksum(open_bytes(q))) { continue } }
  if(kind == "clean") { if(!exists(q)) { rows.push([rel,"missing",0]) } else { if(v != checksum(open_bytes(q))) { rows.push([rel,"modified",0]) } }; continue }
  if(kind == "mixed" && !rel.ends_with(".md") && !rel.ends_with(".dat")) { continue }
  if(kind == "large-write" || kind == "write" || kind == "overwrite" || kind == "copy" || kind == "mirror" || kind == "generate" || kind == "mixed") {
   make_dir(dest + "/" + parent(rel))
   if(kind == "mixed" && rel.ends_with(".dat")) { copy(p,q) } else { if(kind == "generate" || kind == "mixed") { write(q + ".generated","generated\n" + open(p)) } else {
    if(kind == "mirror") { if(!exists(q) || v != checksum(open_bytes(q))) { copy(p,q) } } else {
     if(kind == "copy") { copy(p,q) } else { write(q,open_bytes(p)) }
    }
   }
   }
  }
  if(kind == "rename") { move(q,q + ".renamed") }
  if(kind == "delete") { remove(q) }
  if(kind == "manifest" || kind == "generate" || kind == "mixed") { lines.push(rel + "\t" + size.stringify() + "\t" + v.stringify() + "\n") }
  rows.push([rel,size,v])
 }
 if(kind == "mirror" || kind == "clean") { for(entry : walk(dest,"")) { rel := entry[0]; if(!entry[1] && !exists(root + "/" + rel)) { if(kind == "mirror") { remove(dest + "/" + rel) } else { rows.push([rel,"unexpected",0]) } } } }
 if(kind == "manifest" || kind == "generate" || kind == "mixed") { write(dest + "/manifest.tsv",lines.join("")) }
 return rows
}
print("BOOT")
while(true) { line := read(); if(line == null) { break }; p := line.split("|"); result := execute(p[0],p[1],p[2],p[3].to_int(),p[4].to_int()); print(result.stringify()) }
