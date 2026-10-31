fn(go(h, a)) { fut := h(a); return await fut }
export(go)
