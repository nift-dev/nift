// P1 audit: an object element store used as a value (constant and register values). Refused (index-target) until
// checkpoint 1 (combined-b3-b5.md section 95); it now runs and equals legacy.
o := {"k": 1}
v := 3
print(o["k"] = 5)
print(o["k"] = v)
