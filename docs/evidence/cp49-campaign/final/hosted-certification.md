# Final hosted certification

All selected non-release gates are PASS. Runtime source has no changes after `26ab438`; final guard source is `6e67dd72267c2e0061dbf435b39a5b05ff3dc360`. Later closeout is documentation/evidence only.

| Gate | Certified SHA | Result |
|---|---|---|
| [Deep guards](https://github.com/nift-dev/nift/actions/runs/37765410181) | `e80a95c6523d` | PASS |
| [Test integrity guards](https://github.com/nift-dev/nift/actions/runs/37767455760) | `6e67dd72267c` | PASS |
| [Checkpoint 10 cross-platform equivalence](https://github.com/nift-dev/nift/actions/runs/37767455507) | `6e67dd72267c` | PASS |
| [Performance regression guards](https://github.com/nift-dev/nift/actions/runs/37766314672) | `b7c5c3392b0e` | PASS |
| [Init targets](https://github.com/nift-dev/nift/actions/runs/37766314653) | `b7c5c3392b0e` | PASS |
| [Gate 6A-R vendored libffi](https://github.com/nift-dev/nift/actions/runs/37765410272) | `e80a95c6523d` | PASS |
| [Gate 6B bytes value semantics](https://github.com/nift-dev/nift/actions/runs/37765410351) | `e80a95c6523d` | PASS |
| [v4.4/v4.5 cross-platform](https://github.com/nift-dev/nift/actions/runs/37765410162) | `e80a95c6523d` | PASS |
| [Hosted certification diagnostic](https://github.com/nift-dev/nift/actions/runs/37765410144) | `e80a95c6523d` | PASS |
| [packaging matrix (build-only, non-publishing)](https://github.com/nift-dev/nift/actions/runs/37765410131) | `e80a95c6523d` | PASS |

Test Integrity includes fresh Go/race, C#, Node and Python tests, clean public `make test-bindings`, clean parallel `make -j2 test-all`, and the non-destructive build-boundary proof. Deep includes full 1,219-case sanitizer fuzz, 57-phase core lifecycle, NRS 93/93 with embedding consumers and PRS 12/12.

Earlier superseded Test Integrity runs were cancelled to avoid duplicate rebuilds. Earlier Windows dependency/fixture failures are superseded by the final passing wall; their causes and repairs remain documented.
