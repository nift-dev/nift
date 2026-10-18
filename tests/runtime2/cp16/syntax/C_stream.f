r := cmd("make", "-j4").stream().run()
print(r.exit_code)
