# Mangler checkpoint 2: benchmark barrier inventory

The checkpoint-1 report was run over the conservative Minify++ output for all
12 upstream fixtures. Conservative output preserves identifier spellings and
scope topology while avoiding the cost of reparsing comments and redundant
trivia. `*-bytes` values estimate the removable bytes if every occurrence of a
binding could use a one-byte name; they are opportunity estimates, not promised
output reductions.

| Fixture | Eligible bytes | Unsupported binding kind | Dynamic scope | `arguments` | Class | Concise arrow | Method |
|---|---:|---:|---:|---:|---:|---:|---:|
| antd | 1,898,601 | 67,427 | 0 | 19,751 | 24 | 0 | 0 |
| d3 | 52,074 | 8,516 | 0 | 6,106 | 49,713 | 1,049 | 0 |
| echarts | 575,436 | 130,227 | 0 | 3,546 | 0 | 0 | 0 |
| jquery | 41,811 | 3,896 | 0 | 1,937 | 0 | 0 | 0 |
| lodash | 52,775 | 19,248 | 0 | 1,554 | 0 | 0 | 0 |
| moment | 21,186 | 13,013 | 0 | 219 | 0 | 0 | 0 |
| react | 10,664 | 3,450 | 0 | 1,390 | 0 | 0 | 0 |
| terser | 64,748 | 8,476 | 0 | 55,967 | 5,422 | 20,367 | 1,503 |
| three | 212,301 | 53,257 | 0 | 2,526 | 0 | 0 | 45 |
| typescript | 1,419,512 | 489,183 | 79,537 | 37,408 | 1,943 | 0 | 300,790 |
| victory | 619,976 | 45,411 | 0 | 6,892 | 0 | 197 | 0 |
| vue | 49,806 | 17,606 | 0 | 710 | 320 | 0 | 0 |

The ordering is now evidence-driven. Function/class binding kinds are the
largest broadly shared omission. TypeScript method handling is the largest
single structural barrier, while D3 is dominated by class-containing functions
and the Terser fixture by `arguments` and concise arrows. Top-level opportunity
is negligible because almost every fixture is already wrapped.

