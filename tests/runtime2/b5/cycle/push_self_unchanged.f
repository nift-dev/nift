// B5 fix-up 2 (b5-refsem.md 12.3; vm_former_replay aggregates-10): `arr.push(arr)` is the approved
// reference-cycle error (owner 2026-11-01: refuse cycle creation before mutation; D-NEXT-refsem #4:
// catchable runtime.reference_cycle; legacy pushes a copy). The failed push must leave the receiver
// and everything reachable from it unchanged: length, contents, the identity of the receiver and of
// its parts, seen through every alias. Every golden line below is derived from that rule.
inner := [2, 3]
arr := [[1], inner]
al := arr
obj := {"k": inner}
before := arr.stringify()
caught := 0
try { arr.push(arr) } catch (e) { caught += 1; print(e.code) }
print(arr.length())
print(al.length())
print(arr.stringify() == before)
print(al.stringify() == before)
print(same(arr, al))
a1 := arr[1]
print(same(a1, inner))
print(inner.length())
// the same cycle through the alias, and closed one or two levels down
try { al.push(arr) } catch (e) { caught += 1; print(e.code) }
try { inner.push(al) } catch (e) { caught += 1; print(e.code) }
try { a1.push(arr) } catch (e) { caught += 1; print(e.code) }
try { inner.push(obj) } catch (e) { caught += 1; print(e.code) }
try { arr[0] = arr } catch (e) { caught += 1; print(e.code) }
print(caught)
print(arr.stringify() == before)
print(al.stringify())
print(inner.stringify())
print(obj.stringify())
ok := obj.k
print(same(ok, inner))
b1 := al[1]
print(same(b1, inner))
// the receiver stays an ordinary, shared array afterwards
arr.push([4])
print(al.length())
inner.push(5)
print(al.stringify())
print(obj.stringify())
