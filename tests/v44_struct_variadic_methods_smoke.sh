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

cat >"$t/method-module.f" <<'NIFT'
struct(module_point) { x := 0 }
first := module_point()
first.x = 8
second := module_point()
second.x = 8
items := [first, second]
module_only := 42
fn(pick()) { return "module-global" }
value := 100
rest := [100]
seed := 5
secret := "module-private"
x := "module-x"
sqlite_handle_path := {"primary": "module-db"}
fn(seed_value()) { return seed }
fn(caller_callback(x)) { return secret }
fn(private_transform(x)) { return secret + ":" + x.to_string() }
fn[async](private_async(x)) { return x + 1 }
fn[async](exported_async(x)) { return x + 1 }
fn(start_private_async()) { return private_async(1) }
fn(callable_collision(value, ...rest)) { return value + rest[0] }
fn(recur(n, value)) { if(n == 0) { return value } return recur(n - 1, value) + value }
fn(prepared_indirect(cb)) { result := ""; i := 0; while(i < 1) { result = cb(); i += 1 }; return result }
struct(stateful) {
    initial := seed
    derived := seed_value()
    fn(stateful(seed)) { initial = initial + seed }
    fn(values()) { return [initial, derived] }
}
struct(method_api) {
    outer := {"a": 9}
    indexed := [9]
    fn(eq(first, second)) { return first.x == second.x }
    fn(member(first)) { return first.x }
    fn(bare(first)) { return first }
    fn(compound(first, second)) { return first.x + second.x == 3 }
    fn(variadic(...items)) { return items[0] == items[1] }
    fn(self()) { return this }
    fn(module_value()) { return module_only }
    fn(pick()) { return "sibling" }
    fn(call_pick()) { return pick() }
    fn(call_sibling()) { return this.pick() }
    fn(run(cb)) { return [1].map(cb)[0] }
    fn(invoke_bound(pick)) { return pick() }
    fn(private_call()) { return private_transform(1) }
    fn(private_value()) { return private_transform }
    fn(start_private_async()) { return private_async(1) }
    fn(same_handle(first_handle, second_handle)) { return first_handle._handle_id == second_handle._handle_id }
    fn(handle_path(sqlite_handle_path)) { return sqlite_handle_path.get("primary") }
    fn(module_handle_path()) { return sqlite_handle_path.get("primary") }
    fn(receiver_direct(outer)) { return outer.a }
    fn(receiver_compound(outer)) { return outer.a + 1 }
    fn(receiver_direct_prepared(outer)) {
        result := 0
        i := 0
        while(i < 1) { result = outer.a; i += 1 }
        return result
    }
    fn(receiver_compound_prepared(outer)) {
        result := 0
        i := 0
        while(i < 1) { result = outer.a + 1; i += 1 }
        return result
    }
    fn(index_direct(indexed)) { return indexed[0] }
    fn(index_compound(indexed)) { return indexed[0] + 1 }
    fn(index_direct_prepared(indexed)) {
        result := 0
        i := 0
        while(i < 1) { result = indexed[0]; i += 1 }
        return result
    }
    fn(index_compound_prepared(indexed)) {
        result := 0
        i := 0
        while(i < 1) { result = indexed[0] + 1; i += 1 }
        return result
    }
    fn(index_dynamic(indexed, position)) { return indexed[position] }
    fn(index_postfix(indexed, position)) { return indexed[position].a + 1 }
}
api := method_api()
mutate := (slot) => { slot[0] = 9; return slot[0] }
export(api)
export(mutate)
export(stateful)
export(callable_collision)
export(recur)
export(prepared_indirect)
export(exported_async)
export(start_private_async)
NIFT

cat >"$t/method-parameters.f" <<'NIFT'
import("method-module.f")
struct(point) { x := 0 }
left := point()
left.x = 1
right := point()
right.x = 2
other := point()
other.x = 2
first := other
second := other
items := [other, left]
value := 200
rest := [200]
seed := 200
secret := "consumer"
x := "consumer-x"
fn(pick()) { return "consumer-global" }
fn(seed_value()) { return 200 }
fn(caller_callback(x)) { return secret + ":" + x.to_string() }
fn(dynamic_inner()) { return dynamic_local }
fn(dynamic_outer()) { dynamic_local := 17; return dynamic_inner() }
struct(local_order) {
    fn(pick()) { return "local-sibling" }
    fn(call()) { return pick() }
    fn(explicit()) { return this.pick() }
}
print(api.eq(left, right))
print(api.member(left))
print(api.bare(left) == left)
print(api.compound(left, other))
print(api.variadic(left, left))
print(api.self() == api)
print(api.module_value())
print(api.call_pick())
print(api.call_sibling())
local := local_order()
first_handle := api
second_handle := api
sqlite_handle_path := api
left_handle := {"_handle_id": 11}
right_handle := {"_handle_id": 11}
argument_handle_path := {"primary": "argument-db"}
receiver_argument := {"a": 2}
print(local.call())
print(local.explicit())
print(api.bare(left_handle).stringify())
print(api.handle_path(argument_handle_path))
print(api.module_handle_path())
print(api.same_handle(left_handle, right_handle))
print(api.receiver_direct(receiver_argument))
print(api.receiver_compound(receiver_argument))
print(api.receiver_direct_prepared(receiver_argument))
print(api.receiver_compound_prepared(receiver_argument))
print(api.index_direct([2]))
print(api.index_compound([2]))
print(api.index_direct_prepared([2]))
print(api.index_compound_prepared([2]))
print(api.index_dynamic([2], 0))
print(api.index_postfix([{"a": 2}], 0))
print(dynamic_outer())
print(api.invoke_bound(pick))
print(prepared_indirect(pick))
print(api.run(caller_callback))
print(api.run((x) => secret + ":" + x.to_string()))
print(api.private_call())
exported_future := exported_async(4)
print(await exported_future)
location := [[1]]
print(mutate(location[0]))
print(location[0][0])
s := stateful(2)
print(s.values().stringify())
print(callable_collision(1, 2))
print(recur(3, 2))
fn(prepared_eq(target, first, second)) {
    result := true
    i := 0
    while(i < 1) {
        result = target.eq(first, second)
        i += 1
    }
    return result
}
fn(prepared_callable()) {
    result := 0
    i := 0
    while(i < 1) {
        result = callable_collision(1, 2)
        i += 1
    }
    return result
}
fn(legacy_eq(target, first, second)) { return target.eq(first, second) }
print(prepared_eq(api, left, right))
print(prepared_callable())
print(legacy_eq(api, left, right))
print(seed)
print(secret)
print(x)
NIFT

collision_out=$($NIFT "$t/method-parameters.f")
[ "$collision_out" = 'false
1
true
true
true
true
42
module-global
sibling
consumer-global
local-sibling
{"_handle_id":11}
argument-db
module-db
true
2
3
2
3
2
3
2
3
2
3
17
consumer-global
consumer-global
consumer:1
consumer:1
module-private:1
5
9
9
[7,5]
3
8
false
3
false
200
consumer
consumer-x' ] || { printf 'unexpected method parameter precedence output:\n%s\n' "$collision_out" >&2; exit 1; }

cat >"$t/private-callable-value.f" <<'NIFT'
import("method-module.f")
callback := api.private_value()
print(callback(1))
NIFT
if $NIFT "$t/private-callable-value.f" >/dev/null 2>"$t/private-callable-value.err"; then
    echo 'module-private named function converted to a first-class value' >&2
    exit 1
fi
grep -q 'private_transform' "$t/private-callable-value.err"

for method in receiver_direct receiver_compound receiver_direct_prepared receiver_compound_prepared; do
    cat >"$t/receiver-collision-missing.f" <<NIFT
import("method-module.f")
argument := {"b": 2}
print(api.$method(argument))
NIFT
    if $NIFT "$t/receiver-collision-missing.f" >/dev/null 2>"$t/receiver-collision-missing.err"; then
        echo "$method missing lexical member fell through to the implicit receiver field" >&2
        exit 1
    fi
    grep -q 'value has no member: a' "$t/receiver-collision-missing.err"
done

for method in index_direct index_compound index_direct_prepared index_compound_prepared; do
    cat >"$t/receiver-index-collision.f" <<NIFT
import("method-module.f")
print(api.$method(2))
NIFT
    if $NIFT "$t/receiver-index-collision.f" >/dev/null 2>"$t/receiver-index-collision.err"; then
        echo "$method scalar parameter fell through to the indexable receiver field" >&2
        exit 1
    fi
    grep -q 'invalid index' "$t/receiver-index-collision.err"
done

cat >"$t/private-async-method.f" <<'NIFT'
import("method-module.f")
future := api.start_private_async()
NIFT
if $NIFT "$t/private-async-method.f" >/dev/null 2>"$t/private-async-method.err"; then
    echo 'module-private async callable scheduled from imported method' >&2
    exit 1
fi
grep -q 'module-private async callables are unsupported' "$t/private-async-method.err"

cat >"$t/private-async-function.f" <<'NIFT'
import("method-module.f")
future := start_private_async()
NIFT
if $NIFT "$t/private-async-function.f" >/dev/null 2>"$t/private-async-function.err"; then
    echo 'module-private async callable scheduled from exported module function' >&2
    exit 1
fi
grep -q 'module-private async callables are unsupported' "$t/private-async-function.err"

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
