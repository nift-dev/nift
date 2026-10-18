// Keep source-generated HTML, omitting React-only resource hints outside Astro's body.
fn(render_faithful_mdx(html)) {
    output := html
    while(output.starts_with("<link rel=\"preload\" as=\"image\"")) {
        end := output.index_of("/>")
        if(end < 0) { throw error("Malformed React image preload", "user.mdx_preload") }
        output = output.substr(end + 2)
    }
    return output
}
export(render_faithful_mdx)
