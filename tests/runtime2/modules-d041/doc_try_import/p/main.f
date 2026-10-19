try {
    import("./optional-module.f")
} catch(err) {
    print(err.code)
}
