i := 0
while(i < 100) { r := run("/usr/bin/true"); if(r.exit_code != 0) { print("FAIL") }; i += 1 }
print(i)
