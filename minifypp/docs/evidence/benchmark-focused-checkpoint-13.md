# Benchmark-focused optimizer checkpoint 13

Date: 2026-09-12

This milestone compares Minify++ aggressive output after checkpoints 1–12 with
Terser and UglifyJS configured with compression disabled. That isolates their
parser/mangler/printer foundation from most compression passes. All outputs
were produced and validated by the upstream harness on the same host.

| Artifact | Minify++ aggressive | Best no-compress output | Minify++ gap |
|---|---:|---:|---:|
| antd | 2,511,941 | 2,420,587 | 3.8% |
| d3 | 347,264 | 275,347 | 26.1% |
| echarts | 1,192,909 | 1,068,386 | 11.7% |
| jquery | 102,276 | 94,082 | 8.7% |
| lodash | 95,929 | 74,607 | 28.6% |
| moment | 76,927 | 62,495 | 23.1% |
| react | 29,659 | 25,030 | 18.5% |
| terser | 574,011 | 472,162 | 21.6% |
| three | 729,267 | 674,490 | 8.1% |
| typescript | 4,446,368 | 3,525,593 | 26.1% |
| victory | 812,279 | 756,530 | 7.4% |
| vue | 146,970 | 126,137 | 16.5% |

The best no-compress value is the smaller raw output from Terser or UglifyJS.
This result is important: compression passes are not the only remaining gap.
Identifier coverage and general AST printing still account for roughly 4–29%
on these fixtures. The next batch should therefore expand safe mangling and
printer coverage before treating local constant/DCE passes as the sole route to
the benchmark leaders.

Checkpoint 10 reduced D3 by 44 bytes and the Terser fixture by 3,824 bytes.
Checkpoint 12 removed a further 22 bytes from the Terser fixture. Other fixture
sizes were unchanged.

