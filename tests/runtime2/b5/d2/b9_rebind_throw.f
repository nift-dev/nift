// D2 (docs/runtime-2/checkpoints/combined-b3-b5.md section 27; owner rulings 2026-11-08 03:27/03:29) family B: the rhs rebinds the target and then throws before returning: nothing is written back, the rebinding remains (20, Z), in each context; legacy-equal
x := 6
g := () => { x = 20; throw error("boom") }
try { x += g() } catch (e) { print("caught") }
print(x)
s := "a"
k := () => { s = "Z"; if (s == "Z") { throw error("boom") }; return "a" }
try { s += k() } catch (e) { print("caught") }
print(s)
x = 6
fn(t1()) { try { x *= g() } catch (e) { print("caught") }; print(x); return 0 }
t1()
fn(t2()) { y := 6; h := () => { y = 20; throw error("boom") }; try { y -= h() } catch (e) { print("caught") }; print(y); return 0 }
t2()
x = 6
once := 0
while (once < 1) { once += 1; try { x += g() } catch (e) { print("caught") }; print(x) }
x = 6
try { print(x + g()) } catch (e) { print("caught") }
print(x)
try { x /= g() } catch (e) { print("caught") }
print(x)
