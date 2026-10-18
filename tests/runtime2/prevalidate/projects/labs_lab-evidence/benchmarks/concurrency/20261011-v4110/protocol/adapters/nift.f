fn(mutex_run(n,w,it)) { counter := mutex(0); handles := []; j := 0; while(j < w) { handles.push(thread(inc,counter,n/w,it,true)); j += 1 }; for(h : handles) { h.join() }; counter.lock(); v := counter.get(); counter.unlock(); return [[0,v]] }
fn(produce(q,done,p,total,n)) { id := p; while(id < n) { put := false; q.lock(); state := q.get(); if(state["count"] < 64) { buf := state["buf"]; tail := state["tail"]; buf[tail] = id; state["buf"] = buf; state["tail"] = (state["tail"]+1)%64; state["count"] += 1; q.set(state); put = true }; q.unlock(); if(put) { id += total } else { sleep(1) } }; done.fetch_add(1); return true }
fn(consume(q,done,total)) { out := []; while(true) { got := false; stop := false; id := 0; q.lock(); state := q.get(); if(state["count"] > 0) { buf := state["buf"]; head := state["head"]; id = buf[head]; state["head"] = (state["head"]+1)%64; state["count"] -= 1; q.set(state); got = true } else { stop = done.load() == total }; q.unlock(); if(got) { out.push([id,id*17+3]) } else { if(stop) { break }; sleep(1) } }; return out }
fn(kernel(id, count)) { x := id + 1; i := 0; while(i < count) { x = (x * 48271) % 2147483647; i += 1 }; return x }
fn(job(kind, id, it, extra)) {
 if(kind == "wait" || kind == "memory") { sleep(extra); return id }
 if(kind == "stagger") { sleep(1 + id % 20); return id }
 if(kind == "mixed-wait") { if(id % 10 == 0) { sleep(20) } else { sleep(1) }; return id }
 if(kind == "files") { stream := ifstream("work/fixtures/" + id.stringify() + ".dat"); data := stream.read_all_bytes(); stream.close(); h := 0; i := 0; while(i < data.size()) { h = (h * 31 + data[i]) % 1000000007; i += 1 }; return h }
 if(kind == "process" || kind == "process-mixed" || kind == "process-fail" || kind == "mixed") {
  ms := 0; code := 0; if(kind == "process-mixed") { ms = id % 5 }; if(kind == "process-fail" && id < extra) { code = 7 }; if(kind == "mixed") { sleep(extra) }
  child := run("work/helper", id.stringify(), ms.stringify(), code.stringify()); if(!child.launched) { throw error("child launch failed") }; value := child.stdout.trim().to_int(); if(kind == "mixed") { value += kernel(id,it) }; return {"value":value,"exit":child.exit_code}
 }
 if(kind == "failure" && id < extra) { throw error("intentional", "user.task_failure") }
 return kernel(id,it)
}
fn[async](async_job(kind,id,it,extra)) { return job(kind,id,it,extra) }
fn(worker(kind,n,it,extra,next)) { out := []; while(true) { id := next.fetch_add(1); if(id >= n) { break }; value := job(kind,id,it,extra); out.push([id,value]) }; return out }
fn(inc(counter,n,it,is_mutex)) { i := 0; while(i < n) { if(is_mutex) { counter.lock(); v := counter.get(); j := 0; while(j < it) { v += 1; j += 1 }; counter.set(v); counter.unlock() } else { counter.fetch_add(1) }; i += 1 }; return true }
fn[async](nested_one(id,count)) { handles := []; i := 0; while(i < count) { handles.push(async_job("tiny",id*count+i,8,0)); i += 1 }; out := []; for(h : handles) { out.push(await h) }; return out }
fn[async](held_job(id,gate,extra)) { while(!gate.load()) { sleep(1) }; sleep(extra); return id }
fn(execute(kind,n,w,it,extra)) {
 handles := []; out := []
 if(kind == "queue") { buf := []; i := 0; while(i < 64) { buf.push(0); i += 1 }; q := mutex({"buf":buf,"head":0,"tail":0,"count":0}); done := atomic<int>(0); producers := []; i = 0; while(i < extra) { handles.push(thread(consume,q,done,w)); i += 1 }; i = 0; while(i < w) { producers.push(thread(produce,q,done,i,w,n)); i += 1 }; for(h : producers) { h.join() }; for(h : handles) { for(pair : h.join()) { out.push(pair) } }; return out }
 if(kind == "memory") { gate := atomic<bool>(false); j := 0; while(j < n) { handles.push(held_job(j,gate,extra)); j += 1 }; print("READY"); read(); gate.store(true); j = 0; for(h : handles) { out.push([j,await h]); j += 1 }; return out }
 if(kind == "atomic" || kind == "mutex") { counter := atomic<int>(0); if(kind == "mutex") { return mutex_run(n,w,it) }; j := 0; while(j < w) { handles.push(thread(inc,counter,n/w,it,kind == "mutex")); j += 1 }; for(h : handles) { h.join() }; if(kind == "mutex") { counter.lock(); v := counter.get(); counter.unlock(); return [[0,v]] }; return [[0,counter.load()]] }
 if(kind == "pool" || kind == "cpu" || kind == "files" || kind == "process" || kind == "process-mixed" || kind == "process-fail" || kind == "mixed") { next := atomic<int>(0); j := 0; while(j < w) { handles.push(thread(worker,kind,n,it,extra,next)); j += 1 }; for(h : handles) { for(pair : h.join()) { out.push(pair) } }; return out }
 if(kind == "nested") { j := 0; while(j < n) { handles.push(nested_one(j,it)); j += 1 }; j = 0; for(h : handles) { for(v : await h) { out.push([j,v]); j += 1 } }; return out }
 j := 0; while(j < n) { handles.push(async_job(kind,j,it,extra)); j += 1 }; j = 0; for(h : handles) { try { v := await h; out.push([j,v]) } catch(e) { if(e.code != "user.task_failure") { throw e }; out.push([j,{"error":"task.failure"}]) }; j += 1 }; return out
}
print("BOOT")
while(true) { line := read(); if(line == null) { break }; p := line.split("|"); result := execute(p[0],p[1].to_int(),p[2].to_int(),p[3].to_int(),p[4].to_int()); text := result.stringify(); result = []; print(text); text = "" }
