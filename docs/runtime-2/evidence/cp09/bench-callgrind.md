make bench-rt-value @ 64a7f33 (GCC 13.3.0 -O2, callgrind Ir, empty run subtracted)

sizeof rt::Value 16, sizeof legacy RuntimeValue 152
| workload | rt Ir | legacy Ir | rt/legacy |
|---|---:|---:|---:|
| copy (1000 copies of a 10k-string array) | 4207707 | 1577088074 | 0.003 |
| push (100k numbers) | 9235173 | 30067859 | 0.307 |
| index only (1M reads) | 4000200 | 2500122 | 1.600 |
| object set (128k sets, 64 keys) | 13119442 | 136269836 | 0.096 |
| object get only (128k gets, 64 keys) | 5121447 | 118159871 | 0.043 |
