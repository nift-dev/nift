fn(job(m)) { m.lock(); s:=m.get();s["active"]+=1;if(s["active"]>s["max"]) {s["max"]=s["active"]};m.set(s);m.unlock();sleep(20);m.lock();s=m.get();s["active"]-=1;m.set(s);m.unlock();return 1 }
for(w:[1,2,4]) { m:=mutex({"active":0,"max":0});hs:=[];i:=0;while(i<w){hs.push(thread(job,m));i+=1};for(h:hs){h.join()};m.lock();s:=m.get();m.unlock();print([w,s["max"],s["active"]].stringify()) }
fn[async](bad()) {throw error("probe","user.probe")}
fn[async](good()) {sleep(5);return 42}
a:=bad();b:=good();try{await a}catch(e){print(e.code)};print(await b);try{await a}catch(e){print(e.code)}
