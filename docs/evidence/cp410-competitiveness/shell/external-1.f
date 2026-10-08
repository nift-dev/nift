i := 0
while(i < 1) { r := run("/usr/bin/true"); if(r.exit_code != 0) { print("FAIL") }; i += 1 }
print(i)
