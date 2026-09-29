#!/bin/sh
set -eu
NIFT=${NIFT:-./nift}
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT

cat >"$t/ok.f" <<'NIFT'
fn(global_pack(...args)) { return args }

struct(api) {
    private fn(pack(head, ...rest)) { return [head, rest] }
    fn(count(...args)) { return args.size() }
    fn(command(head, ...args)) { return pack(head, ...args) }
    fn(to_global(...args)) { return global_pack(...args) }
    fn(array_value(value, ...rest)) { return [value.size(), rest.size()] }
    fn(fixed(a, b)) { return a + b }
}

a := api()
print(a.count())
print(a.count("one"))
print(a.count("one", 2, true))
r := a.command("head", 1, "two", true)
print(r[0])
print(r[1].stringify())
spread := ["spread", 3, false]
s := a.command(...spread)
print(s[0])
print(s[1].stringify())
print(a.to_global(1, "two", true).stringify())
print(a.array_value([1, 2]).stringify())
print(a.fixed(2, 3))
multi := a.command(...["multi", 1], ...[2, 3])
print(multi[1].stringify())

fn(prepared_count(target, ...args)) { return target.count(...args) }
print(prepared_count(a, 1, 2, 3, 4))

events := []
fn(mark(value)) { events.push(value); return value }
fn(group(value)) { events.push(value); return [value] }
fn(global_order(first, ...rest)) { return null }
global_order(mark("first"), ...group("spread"), mark("last"))
print(events.join(","))
events.clear()
a.command(mark("first"), ...group("spread"), mark("last"))
print(events.join(","))
NIFT

out=$($NIFT "$t/ok.f")
[ "$out" = '0
1
3
head
[1,"two",true]
spread
[3,false]
[1,"two",true]
[2,0]
5
[1,2,3]
4
spread,first,last
spread,first,last' ] || { printf 'unexpected variadic method output:\n%s\n' "$out" >&2; exit 1; }

cat >"$t/private.f" <<'NIFT'
struct(api) { private fn(hidden(...args)) { return args.size() } }
a := api()
print(a.hidden(1, 2))
NIFT
if $NIFT "$t/private.f" >/dev/null 2>"$t/private.err"; then
    echo 'external private variadic method call succeeded' >&2
    exit 1
fi
grep -q 'private struct method: hidden' "$t/private.err"

for sig in 'f(...a, b)' 'f(...a, ...b)' 'f(...)' 'f(a, a)' 'f(a, ...a)' 'f(this)' 'f(...this)'; do
    printf 'struct(api) { fn(%s) { return null } }\n' "$sig" >"$t/bad.f"
    if $NIFT "$t/bad.f" >/dev/null 2>&1; then
        echo "accepted bad variadic struct method: $sig" >&2
        exit 1
    fi
done

for sig in 'f(a, a)' 'f(a, ...a)'; do
    printf 'fn(%s) { return null }\n' "$sig" >"$t/bad-function.f"
    if $NIFT "$t/bad-function.f" >/dev/null 2>&1; then
        echo "accepted duplicate named-function parameter: $sig" >&2
        exit 1
    fi
done

cat >"$t/constructor.f" <<'NIFT'
struct(api) { fn(api(...args)) { return null } }
NIFT
if $NIFT "$t/constructor.f" >/dev/null 2>"$t/constructor.err"; then
    echo 'accepted variadic struct constructor' >&2
    exit 1
fi
grep -q 'struct constructors cannot be variadic' "$t/constructor.err"

cat >"$t/non-array-spread.f" <<'NIFT'
struct(api) { fn(call(...args)) { return args } }
a := api()
x := 3
print(a.call(...x))
NIFT
if $NIFT "$t/non-array-spread.f" >/dev/null 2>"$t/non-array-spread.err"; then
    echo 'non-array method spread succeeded' >&2
    exit 1
fi
grep -q 'spread value must be an array' "$t/non-array-spread.err"

echo 'PASS v4.4 variadic struct named methods'
