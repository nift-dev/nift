printf 'line one\nline two\r\nthree' > text.txt
printf 'a\000b\377' > bin.dat
printf '{"a": 1, "b": [true, null], "c": "x"}' > data.json
printf '1 + 2' > sum.expr
printf '42 "two words" [1, 2] {"k": "v"}\n' > vals.txt
printf 'outside\n' > "$BASE/ext/out.txt"
