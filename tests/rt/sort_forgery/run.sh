#!/usr/bin/env bash
# Tooling checkpoint (component 4, RECHECK2 N4): the forged-header route to the
# sort commits (rc2/forge/g20) cannot be closed by the language (an ODR
# violation, no diagnostic required: the unit compiles, see case 0). It is
# closed at link time:
#   1. static: planted into a production source (src/Hooks.cpp), `make nift`
#      fails at the symbol-reference allowlist (check-symbol-refs.sh);
#   2. dynamic: a program with the forged unit, linked against the built
#      libnift_c.so, fails to link (the commit is not exported, component 6);
#      control: against the same objects linked without the version script it
#      links (the route RECHECK2 demonstrated).
#   run.sh   (from the repository root, after `make nift libnift_c.so`)
set -u
export LC_ALL=C
here=tests/rt/sort_forgery
work=$(mktemp -d "${TMPDIR:-/tmp}/nift-sort-forgery.XXXXXX")
cp -p src/Hooks.cpp "$work/Hooks.cpp.orig"
restore() { cp -p "$work/Hooks.cpp.orig" src/Hooks.cpp; touch src/Hooks.cpp; }
trap 'restore; rm -rf "$work"' EXIT
fail=0
var() { make -s -f Makefile -f - pv <<MK 2>/dev/null
pv: ; @echo \$($1)
MK
}
CXX=$(var CXX); CXXFLAGS=$(var CXXFLAGS); CPPFLAGS=$(var CPPFLAGS)
# 0. the forged unit compiles (the language does not refuse it)
# shellcheck disable=SC2086
if $CXX $CPPFLAGS $CXXFLAGS -c -x c++ "$here/forged.inc" -o "$work/forged.o" 2>"$work/compile.log"; then
  echo "PASS sort-forgery 0: the forged unit compiles (ODR violation, no diagnostic required): not closable by the language"
else echo "FAIL sort-forgery 0: the forged unit does not compile here, so this test shows nothing"; fail=1; fi
# 1. static nift
{ printf '\n'; cat "$here/forged.inc"; } >> src/Hooks.cpp
if make nift > "$work/static.log" 2>&1; then echo "FAIL sort-forgery 1: nift linked with the forged unit"; fail=1
elif grep -q 'src/Hooks.o nift::rt::mutation::Protocol::sort_storage' "$work/static.log"; then
  echo "PASS sort-forgery 1: make nift refuses the link (src/Hooks.o references Protocol::sort_storage outside the allowlist)"
else echo "FAIL sort-forgery 1: failed for another reason:"; grep -m3 -i error "$work/static.log"; fail=1; fi
restore
make nift > "$work/rebuild.log" 2>&1 || { echo "FAIL sort-forgery: nift does not rebuild after restore"; fail=1; }
# 2. dynamic: libnift_c.so
printf 'namespace nift::rt { class Value; }\nvoid nift_probe_hijack(nift::rt::Value&);\nint main() { return 0; }\nvoid (*keep)(nift::rt::Value&) = nift_probe_hijack;\n' > "$work/main.cpp"
# shellcheck disable=SC2086
if $CXX $CXXFLAGS "$work/main.cpp" "$work/forged.o" -L. -lnift_c -Wl,-rpath,"$(pwd)" -o "$work/hijack" 2>"$work/dyn.log"; then
  echo "FAIL sort-forgery 2: the forged unit links against libnift_c.so"; fail=1
elif grep -q 'undefined reference to .*Protocol::sort_storage' "$work/dyn.log"; then
  echo "PASS sort-forgery 2: against libnift_c.so the forged unit does not link (Protocol::sort_storage is not exported)"
else echo "FAIL sort-forgery 2: failed for another reason:"; head -3 "$work/dyn.log"; fail=1; fi
# shellcheck disable=SC2046
$CXX $CXXFLAGS -shared $(var LIBFFI_SHARED_LINK_FLAGS) -o "$work/libopen.so" $(var C_ABI_PIC) $(var LIBFFI_A) 2>"$work/open.log"
# shellcheck disable=SC2086
if $CXX $CXXFLAGS "$work/main.cpp" "$work/forged.o" -L"$work" -lopen -o "$work/hijack-open" 2>>"$work/open.log"; then
  echo "PASS sort-forgery 2 control: against the same objects without the version script it links (the route closed by component 6)"
else echo "FAIL sort-forgery 2 control: did not link without the version script either, so case 2 shows nothing"; fail=1; fi
git diff --quiet -- src/Hooks.cpp || { echo "FAIL sort-forgery: src/Hooks.cpp not restored"; fail=1; }
echo "$( [ $fail = 0 ] && echo PASS || echo FAIL ) sort-forgery: the forged-header route is refused at every link"
exit $fail
