@import("mdx-prototype.f")
doc := mdx.input("fixtures/page.mdx")
print(doc.stringify())
print(mdx.html(doc))
