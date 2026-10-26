fn[async](f()) { sleep(30); print("task"); return 1 }
p := f()
return 5
