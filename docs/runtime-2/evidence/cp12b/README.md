# CP12 phase B evidence

`inventory-baseline/`: the coverage inventory at `runtime-2` @`ed7d64d9` plus
the classify-all tooling (commit `64c76f27`, before any phase B feature):

    tools/rt-vm/cp12b_inventory.py collect --nift <nift> --out DIR \
        --projunits <cp13 projunits>        # NIFT_VM=classify-all, nothing executes
    tools/rt-vm/cp12b_inventory.py analyse DIR --json DIR/summary.json > DIR/analysis.txt

`shared.jsonl` (789 units, rt-diff's set as test-rt-vm sees it),
`projects.jsonl` (86 CP13 project units), `generator.jsonl` (300 general
generator programs): one line per unit with the classify outcome, its first
reason and `reasons` (every refusal, `consequential` flagged). The
statistics files' timings and paths are dropped. `analysis.txt` and
`summary.json` are reproduced from these files by `analyse`.

Record: `../../checkpoints/cp12b-coverage-inventory.md`.
