printf '{"n": 12345678901234567890123, "z": -0, "s": "\\u00e9"}' > exact.json
printf '"a $[1+1] string"' > str.json
printf '[1, 2 + 3]' > arr.expr
printf 'inject("self.expr")' > self.expr
