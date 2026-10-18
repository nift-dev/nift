// Invalid JSON falls back to the Nift literal evaluator, whose error is fatal.
print("before")
print(inject("bad.json").stringify())
