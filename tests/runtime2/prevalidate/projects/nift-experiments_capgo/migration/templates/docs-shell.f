/* Apply only route-specific changes to the shared docs shell. */
fn(render_docs_shell(path, replacements)) {
    output := open(path)
    for(pair : replacements) {
        output = output.replace(pair[0], pair[1])
    }
    return output
}
export(render_docs_shell)
