/*
    ignore package for Nift: deterministic ignore-style pattern matching for
    paths, tuned for Nift site/content pipelines.

    Public API: the exported `ignore` struct. Helpers stay private. This is a
    pure, no-process package: patterns are parsed and paths are matched entirely
    in Nift.

    Semantics (a documented Git-compatible subset, see README):

    - blank lines and `#` comment lines are ignored; `\#` is a literal hash;
    - `!` negates the previous match; `\!` is a literal bang;
    - a trailing `/` marks a directory pattern;
    - a leading `/` (or any unescaped middle `/`) anchors the pattern to the
      project root;
    - a pattern with no slash matches a path component at any depth;
    - `*` matches any run of characters except `/`;
    - `?` matches exactly one character;
    - `**` as a full path component matches zero or more components;
    - a `\` escapes the next character;
    - the last matching rule wins;
    - matching is performed on characters (not bytes), so `?` is per character.

    Resource bounds are enforced: pattern count, per-line length, path length and
    component count all have documented caps; the matcher is iterative and
    bounded so pathological inputs cannot cause unbounded work.
*/

id_max_patterns := 256
id_max_line_chars := 2048
id_max_path_chars := 4096
id_max_components := 128

struct(ignore) {
    private fn(rstrip(s)) {
        chars := s.split("")
        i := chars.size()
        while(i > 0 && chars[i - 1] == " ") { i -= 1 }
        if(i == chars.size()) { return s }
        return chars.slice(0, i).join("")
    }

    private fn(split_on_slash(chars)) {
        segments := []
        current := []
        i := 0
        while(i < chars.size()) {
            c := chars[i]
            if(c == "/") {
                if(i > 0 && chars[i - 1] == "\\") { current.push(c) }
                else { segments.push(current); current = [] }
            }
            else { current.push(c) }
            i += 1
        }
        segments.push(current)
        return segments
    }

    private fn(segment_star(chars)) {
        return chars.size() == 2 && chars[0] == "*" && chars[1] == "*"
    }

    private fn(tokenize_segment(chars)) {
        tokens := []
        i := 0
        while(i < chars.size()) {
            c := chars[i]
            if(c == "\\") {
                if(i + 1 < chars.size()) { tokens.push({"c": chars[i + 1], "w": "lit"}); i += 2 }
                else { tokens.push({"c": "\\", "w": "lit"}); i += 1 }
            }
            else if(c == "*") { tokens.push({"c": "*", "w": "star"}); i += 1 }
            else if(c == "?") { tokens.push({"c": "?", "w": "any"}); i += 1 }
            else { tokens.push({"c": c, "w": "lit"}); i += 1 }
        }
        return tokens
    }

    private fn(parse_line(line)) {
        text := this.rstrip(line)
        if(text == "") { return null }
        if(text.length() > id_max_line_chars) { return null }
        chars := text.split("")
        if(chars[0] == "#") { return null }
        negated := false
        if(chars[0] == "!") {
            negated = true
            chars = chars.slice(1, chars.size())
        }
        dir_only := false
        if(chars.size() > 0) {
            last := chars.size() - 1
            if(chars[last] == "/" && !(last > 0 && chars[last - 1] == "\\")) {
                dir_only = true
                chars = chars.slice(0, last)
            }
        }
        anchored := false
        if(chars.size() > 0 && chars[0] == "/") {
            anchored = true
            chars = chars.slice(1, chars.size())
        }
        if(chars.size() == 0) { return null }
        j := 0
        while(j < chars.size()) {
            if(chars[j] == "/" && !(j > 0 && chars[j - 1] == "\\")) { anchored = true }
            j += 1
        }
        raw_segments := this.split_on_slash(chars)
        segments := []
        k := 0
        while(k < raw_segments.size()) {
            seg_chars := raw_segments[k]
            if(this.segment_star(seg_chars)) { segments.push({"star": true}) }
            else if(seg_chars.size() > 0) { segments.push({"star": false, "tokens": this.tokenize_segment(seg_chars)}) }
            k += 1
        }
        if(segments.size() == 0) { return null }
        minc := 0
        m := 0
        while(m < segments.size()) { if(!segments[m].star) { minc += 1 }; m += 1 }
        return {"negated": negated, "dir_only": dir_only, "anchored": anchored, "segments": segments, "minc": minc}
    }

    fn(parse(text)) {
        if(type(text) != "string") { return null }
        lines := text.split("\n")
        if(lines.size() > id_max_patterns) { return null }
        i := 0
        while(i < lines.size()) {
            if(lines[i].length() > id_max_line_chars) { return null }
            i += 1
        }
        rules := []
        i = 0
        while(i < lines.size()) {
            rule := this.parse_line(lines[i])
            if(rule != null) { rules.push(rule) }
            i += 1
        }
        return rules
    }

    private fn(glob_match(tokens, text)) {
        m := tokens.size()
        n := text.size()
        p := 0
        t := 0
        star := -1
        star_t := 0
        while(t < n) {
            if(p < m) {
                tk := tokens[p]
                w := tk.w
                if(w == "any") { p += 1; t += 1 }
                else if(w == "star") { star = p; star_t = t; p += 1 }
                else {
                    if(tk.c == text[t]) { p += 1; t += 1 }
                    else if(star != -1) { p = star + 1; star_t += 1; t = star_t }
                    else { return false }
                }
            }
            else if(star != -1) { p = star + 1; star_t += 1; t = star_t }
            else { return false }
        }
        while(p < m && tokens[p].w == "star") { p += 1 }
        return p == m
    }

    private fn(match_segments(segments, comps, k)) {
        reach := [0]
        s := 0
        while(s < segments.size()) {
            seg := segments[s]
            next := []
            if(seg.star) {
                minc := reach[0]
                j := minc
                while(j <= k) { next.push(j); j += 1 }
            }
            else {
                r := 0
                while(r < reach.size()) {
                    c := reach[r]
                    if(c < k && this.glob_match(seg.tokens, comps[c])) { next.push(c + 1) }
                    r += 1
                }
            }
            if(next.size() == 0) { return false }
            reach = next
            s += 1
        }
        i := 0
        while(i < reach.size()) { if(reach[i] == k) { return true }; i += 1 }
        return false
    }

    private fn(rule_matches(rule, comps)) {
        n := comps.size()
        if(rule.anchored) {
            k := rule.minc
            if(k < 1) { k = 1 }
            while(k <= n) {
                if(this.match_segments(rule.segments, comps, k)) { return true }
                k += 1
            }
            return false
        }
        if(n == 0) { return false }
        if(rule.segments.size() == 1 && rule.segments[0].star) { return true }
        toks := rule.segments[0].tokens
        i := 0
        while(i < n) {
            if(this.glob_match(toks, comps[i])) { return true }
            i += 1
        }
        return false
    }

    private fn(normalize_path(path)) {
        if(type(path) != "string" || path.length() > id_max_path_chars) { return null }
        text := path.replace("\\", "/")
        substrings := text.split("/")
        comps := []
        i := 0
        while(i < substrings.size()) {
            part := substrings[i]
            if(part != "" && part != ".") { comps.push(part.split("")) }
            i += 1
        }
        if(comps.size() > id_max_components) { return null }
        return comps
    }

    fn(match(rules, path)) {
        if(type(rules) != "array") { return null }
        comps := this.normalize_path(path)
        if(comps == null) { return null }
        status := false
        i := 0
        while(i < rules.size()) {
            rule := rules[i]
            if(type(rule) == "object" && this.rule_matches(rule, comps)) {
                status = !rule.negated
            }
            i += 1
        }
        return status
    }

    fn(ignored(rules, path)) { return this.match(rules, path) }
}

ignore := ignore()
export(ignore)