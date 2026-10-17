sizeof rt::Value 16, sizeof legacy RuntimeValue 152
| workload | rt Ir | legacy Ir | rt/legacy |
|---|---:|---:|---:|
| copy (1000 copies of a 10k-string array) | 3810476 | 1577088074 | 0.002 |
| push (100k numbers) | 8235942 | 30067859 | 0.274 |
| index only (1M reads) | 4000214 | 2500136 | 1.600 |
| object set (128k sets, 64 keys) | 13119456 | 136269850 | 0.096 |
| object get only (128k gets, 64 keys) | 5121461 | 118159885 | 0.043 |
