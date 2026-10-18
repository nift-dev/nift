// epoch/sleep/timer: only clock-independent facts are printed (booleans, types).
t0 := epoch(); print(type(t0)); print(t0 > 1700000000000)
sleep(0)
sleep(40); print(epoch() - t0 >= 40)
tm := timer(); print(type(tm)); print(tm.running()); print(tm.paused()); print(tm.elapsed())
tm.start(); print(tm.running()); sleep(25); tm.pause(); print(tm.paused())
x := tm.elapsed(); print(x >= 25); sleep(25); print(tm.elapsed() == x)
tm.resume(); print(tm.running()); sleep(5); print(tm.elapsed() > x)
tm.stop(); print(tm.running()); y := tm.elapsed(); sleep(10); print(tm.elapsed() == y)
tm.resume(); print(tm.running())
tm.reset(); print(tm.elapsed()); print(tm.running())
tm.start(); tm.start(); print(tm.elapsed() < 25)
try { tm.start(1) } catch(e) { print("caught") }
