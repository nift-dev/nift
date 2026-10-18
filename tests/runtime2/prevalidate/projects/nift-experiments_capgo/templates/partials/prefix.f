fn(prefix_html(html,base)) { if(base == "") { return html }
 for(attr : ["href", "src", "action", "poster", "srcset"]) { html = html.replace(attr + "=\"/", attr + "=\"" + base + "/") }
 return html.replace(", /", ", " + base + "/") }
export(prefix_html)
