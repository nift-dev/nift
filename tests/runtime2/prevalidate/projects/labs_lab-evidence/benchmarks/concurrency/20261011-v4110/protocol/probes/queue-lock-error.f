fn(holds(m)) { m.lock(); bad:=unknown_identifier; m.unlock();return true }
fn(waiter(m)) { sleep(10);m.lock();m.unlock();return true }
m:=mutex(0);a:=thread(holds,m);b:=thread(waiter,m);a.join();b.join()
