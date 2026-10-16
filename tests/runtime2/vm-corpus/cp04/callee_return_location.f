data := {"items": [1]}
fn(items()) { return data.items }
it := items()
it.push(2)
print(data)
