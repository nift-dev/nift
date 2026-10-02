#!/usr/bin/env bash
set -euo pipefail

NIFT=${NIFT_BIN:-${NIFT:-./nift}}
case "$NIFT" in /*) ;; *) NIFT="$(pwd)/$NIFT" ;; esac
TMP=$(mktemp -d)
trap 'chmod -R u+rwx "$TMP" 2>/dev/null || true; rm -rf "$TMP"' EXIT

# catch-code: run a script that catches a recoverable failure and assert the
# reported code/category/origin. Pass the expected code as the first argument
# and a Nift program printing "code|category|source-ok|line-ok|message".
catch_code() {
    local expected=$1 script=$2
    local actual
    actual=$("$NIFT" -e "$script" 2>"$TMP/cc.err") || {
        echo "catch_code($expected) script failed:" >&2
        cat "$TMP/cc.err" >&2
        exit 1
    }
    local code category source_ok line_ok message
    IFS='|' read -r code category source_ok line_ok message <<<"$actual"
    [[ "$code" == "$expected" ]] || { echo "catch_code($expected): code=$code" >&2; exit 1; }
    [[ "$category" == "io" ]] || { echo "catch_code($expected): category=$category" >&2; exit 1; }
    [[ "$source_ok" == "true" ]] || { echo "catch_code($expected): source not populated" >&2; exit 1; }
    [[ "$line_ok" == "true" ]] || { echo "catch_code($expected): line not populated" >&2; exit 1; }
    [[ -n "$message" ]] || { echo "catch_code($expected): empty message" >&2; exit 1; }
}

mkdir -p "$TMP/wd"
cd "$TMP/wd"

# ---------------------------------------------------------------- io.open_failed
catch_code io.open_failed 'try { open("missing.txt") } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'
catch_code io.open_failed 'try { cat("missing.txt") } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'
catch_code io.open_failed 'try { open_bytes("missing.bin") } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'
catch_code io.open_failed 'f := file("missing.txt"); try { f.open("r") } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'
catch_code io.open_failed 'f := file("missing.txt"); try { f.open("rw") } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'

# ---------------------------------------------------------------- io.create_failed
catch_code io.create_failed 'try { touch("no-parent/x.txt") } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'
touch plainfile
catch_code io.create_failed 'try { mkdir("plainfile/child") } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'

# ---------------------------------------------------------------- io.copy_failed
catch_code io.copy_failed 'try { cp("missing-src.txt", "dest.txt") } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'
printf 'X' > src.txt
catch_code io.copy_failed 'f := file("src.txt"); try { f.copy("no-dir/dest.txt") } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'

# ---------------------------------------------------------------- io.move_failed
catch_code io.move_failed 'try { mv("missing-src.txt", "dest.txt") } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'

# ---------------------------------------------------------------- io.remove_failed
mkdir -p rodir; printf 'X' > rodir/f.txt; printf 'Y' > rodir/g.txt; chmod 555 rodir
catch_code io.remove_failed 'try { rm("rodir/f.txt") } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'
catch_code io.remove_failed 'f := file("rodir/g.txt"); try { f.remove() } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'
chmod 755 rodir

# ---------------------------------------------------------------- io.directory_read_failed
catch_code io.directory_read_failed 'try { ls("missing-dir") } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'
catch_code io.directory_read_failed 'try { ls("missing-dir/") } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'

# ---------------------------------------------------------------- io.change_directory_failed
catch_code io.change_directory_failed 'try { cd("missing-dir") } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'

# ---------------------------------------------------------------- io.write_failed (FileValue save)
mkdir -p savedir; printf 'ORIG' > savedir/f.txt; chmod 555 savedir
catch_code io.write_failed 'f := file("savedir/f.txt"); f.open("rw"); f.replace_once("ORIG", "CHG"); try { f.save() } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }; f.revert(); f.close()'
chmod 755 savedir

# ---------------------------------------------------------------- uncaught compatibility
# The same operational failures outside try exit nonzero and preserve the
# pre-CP4 human-facing message without exposing Error serialization.
for spec in \
    'open("missing.txt")|open: cannot open path' \
    'cat("missing.txt")|cat: cannot open path' \
    'open_bytes("missing.bin")|open_bytes: cannot open path' \
    'ls("missing-dir")|ls:' \
    'cd("missing-dir")|cd:' \
    'touch("no-parent/x.txt")|touch: cannot open path'; do
    script=${spec%%|*}; pattern=${spec#*|}
    if "$NIFT" -e "$script" >"$TMP/u.out" 2>"$TMP/u.err"; then
        echo "uncaught compatibility unexpectedly succeeded: $script" >&2
        exit 1
    fi
    grep -q "$pattern" "$TMP/u.err" || { echo "uncaught message mismatch for $script" >&2; cat "$TMP/u.err" >&2; exit 1; }
    if grep -q '"code"\|"category"\|"cause"' "$TMP/u.err"; then
        echo "uncaught failure exposed Error serialization: $script" >&2
        cat "$TMP/u.err" >&2
        exit 1
    fi
done

# ---------------------------------------------------------------- prepared/legacy parity
[[ "$("$NIFT" -e 'i := 0; try { while(i < 1) { i := i + 1; v := open("missing-prep.txt") } } catch(err) { print("prepared:" + err.code) }')" == 'prepared:io.open_failed' ]]
[[ "$("$NIFT" -e 'i := 0; try { while(i < 1) { 9007199254740993; i := i + 1; v := open("missing-legacy.txt") } } catch(err) { print("legacy:" + err.code) }')" == 'legacy:io.open_failed' ]]

# A recoverable failure must not fall back to legacy and re-execute an
# operation. The loop body runs exactly once before the failure is caught.
[[ "$("$NIFT" -e 'count := 0; i := 0; try { while(i < 1) { count = count + 1; i := i + 1; v := open("missing-once.txt") } } catch(err) { print("count:" + count + ":" + err.code) }')" == 'count:1:io.open_failed' ]]

# ---------------------------------------------------------------- fatal stays fatal
for script in \
    'try { open(42) } catch(err) { print("caught") }' \
    'try { mkdir(42) } catch(err) { print("caught") }' \
    'try { ls(42) } catch(err) { print("caught") }' \
    'try { cd(42) } catch(err) { print("caught") }' \
    'try { cp(1, 2) } catch(err) { print("caught") }'; do
    if "$NIFT" -e "$script" >"$TMP/f.out" 2>"$TMP/f.err"; then
        echo "fatal wrong-type was caught or succeeded: $script" >&2
        exit 1
    fi
    grep -q 'caught' "$TMP/f.out" && { echo "wrong-type failure was caught: $script" >&2; exit 1; }
done

mkdir -p adir
if "$NIFT" -e 'try { open("adir") } catch(err) { print("caught") }' >"$TMP/f.out" 2>&1; then
    echo 'directory-vs-file open unexpectedly succeeded' >&2
    exit 1
fi
grep -q 'caught' "$TMP/f.out" && { echo 'directory-vs-file open was caught' >&2; exit 1; }

if "$NIFT" -e 'f := file("lf.txt"); try { f.open("w"); f.open("r") } catch(err) { print("caught") }' >"$TMP/f.out" 2>&1; then
    echo 'already-open FileValue unexpectedly succeeded' >&2
    exit 1
fi
grep -q 'caught' "$TMP/f.out" && { echo 'already-open FileValue was caught' >&2; exit 1; }

if "$NIFT" -e 'f := file("lf2.txt"); f.open("w"); try { f.read_all() } catch(err) { print("caught") }' >"$TMP/f.out" 2>&1; then
    echo 'read-on-write-only FileValue unexpectedly succeeded' >&2
    exit 1
fi
grep -q 'caught' "$TMP/f.out" && { echo 'read-on-write-only FileValue was caught' >&2; exit 1; }

printf 'KEEP' > dirty.txt
if "$NIFT" -e 'f := file("dirty.txt"); f.open("rw"); f.write("CHANGE"); try { f.close() } catch(err) { print("caught") }' >"$TMP/f.out" 2>&1; then
    echo 'dirty-close FileValue unexpectedly succeeded' >&2
    exit 1
fi
grep -q 'caught' "$TMP/f.out" && { echo 'dirty-close FileValue was caught' >&2; exit 1; }

# ---------------------------------------------------------------- FileValue failed-save semantics
# A backend save failure is catchable, dirty state survives for retry, the disk
# is unchanged, revert() still works, catch does not implicitly save/revert,
# and no temporary file is left behind.
mkdir -p retrydir; printf 'ORIGINAL' > retrydir/f.txt; chmod 555 retrydir
cat > "$TMP/retry.f" <<NIFT
f := file("retrydir/f.txt")
f.open("rw")
f.replace_once("ORIGINAL", "CHANGED")
try { f.save() } catch(err) {
    print("caught:" + err.code)
    print("dirty-after-catch:" + f.modified())
}
f.revert()
print("dirty-after-revert:" + f.modified())
print("disk:" + open("retrydir/f.txt"))
f.close()
NIFT
[[ "$("$NIFT" "$TMP/retry.f")" == $'caught:io.write_failed\ndirty-after-catch:true\ndirty-after-revert:false\ndisk:ORIGINAL' ]] || {
    echo 'failed-save retry semantics regressed' >&2
    exit 1
}
chmod 755 retrydir

# No temporary-file leakage from any tested failed save.
if ls "$TMP/wd"/.nift-tmp-* >/dev/null 2>&1; then
    echo 'failed save leaked a temporary file' >&2
    exit 1
fi

# A retry after the external condition is repaired in-process succeeds (dirty
# data remains retryable). The missing-parent failure is repairable from Nift.
cat > "$TMP/retry2.f" <<NIFT
f := file("missing-parent/value.txt")
f.open("w")
f.write("CHANGED")
try { f.save() } catch(err) { print("first-fail:" + err.code) }
mkdir("missing-parent")
f.save()
f.close()
print(open("missing-parent/value.txt"))
NIFT
[[ "$("$NIFT" "$TMP/retry2.f")" == $'first-fail:io.write_failed\nCHANGED' ]] || {
    echo 'save retry after in-process repair regressed' >&2
    exit 1
}

# Catching a save failure must not implicitly commit or revert: after the
# catch, appending more and saving applies exactly the buffered content once.
cat > "$TMP/retry3.f" <<NIFT
f := file("missing-parent2/value.txt")
f.open("w")
f.write("ONE")
try { f.save() } catch(err) { print("fail:" + err.code) }
mkdir("missing-parent2")
f.append("TWO")
f.save()
f.close()
print(open("missing-parent2/value.txt"))
NIFT
[[ "$("$NIFT" "$TMP/retry3.f")" == $'fail:io.write_failed\nONETWO' ]] || {
    echo 'post-catch buffered content regressed' >&2
    exit 1
}

echo 'v4.6 Batch 4 CP4a filesystem/FileValue: PASS'