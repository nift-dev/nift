# rt-diff self-test corpus (CP08 round-2 review reproducers)

Used only by `tests/runtime2/rt_diff_selftest.sh`, never by the same-binary
gate (the `timeout/` entries never finish by design).

| Path | Reproduces |
| --- | --- |
| `channels/time` | a script printing `time taken: 1 seconds` — user output is never normalised |
| `channels/il`, `channels/il2` | async script that only computes; script whose comment mentions async — the combined (stream order) channel is compared unless A's order is measured unstable |
| `timeout/hang`, `timeout/loop` | a child process that outlives the run; a busy loop — timeouts are differences and leave no process behind |
| `pc/one`, `other/pc/one` | the same entry id with different programs — policies bind to the source hash |
| `gen/m1` | a generated program that imports a module — the minimiser copies the whole entry directory |
