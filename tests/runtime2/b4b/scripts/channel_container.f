// B4b C7: a container sent through a channel arrives as a copy.
c := channel(1)
xs := [" a "]
c.send(xs)
ys := c.recv()
ys[0] = 5
print("before")
print(xs[0].trim())
