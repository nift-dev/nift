/* Bounded MDX preservation parser. Public API: only the `mdx` facade. */

struct(mdx) {
    profile_name := "bounded"

    private fn(limits()) {
        if(this.profile_name == "bounded") { return {"bytes":4096,"lines":1024,"line_bytes":16384,"aggregate":16384} }
        if(this.profile_name == "trusted") { return {"bytes":65536,"lines":8192,"line_bytes":65536,"aggregate":2097152} }
        throw error("Unknown MDX parser profile", "user.mdx_profile")
    }

    fn(with_profile(name)) {
        if(name != "bounded" && name != "trusted") { throw error("Unknown MDX parser profile", "user.mdx_profile") }
        configured := mdx()
        configured.profile_name = name
        return configured
    }

    private fn(byte_at(source, index)) { return source.data[index] }
    private fn(text(source, begin, end)) { return source.raw.substr(begin, end - begin) }
    private fn(source_length(source)) { return source.data.length() }
    private fn(is_space(value)) { return value == 32 || value == 9 || value == 10 || value == 13 }
    private fn(is_hspace(value)) { return value == 32 || value == 9 }
    private fn(is_alpha(value)) { return (value >= 65 && value <= 90) || (value >= 97 && value <= 122) }
    private fn(is_digit(value)) { return value >= 48 && value <= 57 }
    private fn(name_start(value)) { return this.is_alpha(value) }
    private fn(name_byte(value)) { return this.is_alpha(value) || this.is_digit(value) || value == 95 || value == 45 }
    private fn(attr_byte(value)) { return this.name_byte(value) || value == 46 || value == 58 }

    private fn(default_frontmatter()) {
        return {"present":false,"source":"","body":"","start":null,"end":null}
    }

    private fn(point(lines, offset)) {
        low := 0
        high := lines.length()
        while(low + 1 < high) {
            middle := ((low + high) / 2).floor().to_int()
            if(lines[middle] <= offset) { low = middle } else { high = middle }
        }
        return {"offset":offset,"line":low + 1,"column":offset - lines[low] + 1}
    }

    private fn(position(lines, begin, end)) {
        return {"start":this.point(lines, begin),"end":this.point(lines, end)}
    }

    private fn(line_info(source)) {
        starts := [0]
        cursor := 0
        line_begin := 0
        normalized := source.raw.replace("\r\n", "\n").replace("\r", "\n")
        for(line : normalized.split("\n")) {
            width := line.encode("utf-8").length()
            cursor += width
            if(cursor < source.length) {
                if(width > source.limits.line_bytes) { return {"ok":false,"starts":starts,"offset":line_begin} }
                if(source.data[cursor] == 13 && cursor + 1 < source.length && source.data[cursor + 1] == 10) { cursor += 2 } else { cursor += 1 }
                if(cursor < source.length) { starts.push(cursor); line_begin = cursor }
            }
        }
        if(source.length - line_begin > source.limits.line_bytes) { return {"ok":false,"starts":starts,"offset":line_begin} }
        return {"ok":true,"starts":starts,"offset":0}
    }

    private fn(line_end(source, begin)) {
        tail := source.raw.substr(begin)
        next := tail.index_of("\n")
        carriage := tail.index_of("\r")
        if(next < 0 || (carriage >= 0 && carriage < next)) { next = carriage }
        if(next < 0) { return source.length }
        return begin + next
    }

    private fn(after_line(source, end)) {
        if(end >= source.length) { return end }
        if(source.data[end] == 13 && end + 1 < source.length && source.data[end + 1] == 10) { return end + 2 }
        return end + 1
    }

    private fn(at_line_start(source, offset)) {
        return offset == 0 || source.data[offset - 1] == 10 || source.data[offset - 1] == 13
    }

    private fn(escaped(source, offset)) {
        count := 0
        cursor := offset
        while(cursor > 0 && source.data[cursor - 1] == 92) { count += 1; cursor -= 1 }
        return count % 2 == 1
    }

    private fn(diagnostic(code, message, path, lines, offset)) {
        where := this.point(lines, offset)
        return {"code":code,"message":message,"severity":"error","path":path,"line":where.line,"column":where.column,"offset":offset}
    }

    private fn(result(ok, source, path, frontmatter, imports, exports, nodes, dependencies, diagnostics)) {
        return {"ok":ok,"source":source,"path":path,"frontmatter":frontmatter,"imports":imports,"exports":exports,"nodes":nodes,"dependencies":dependencies,"diagnostics":diagnostics}
    }

    private fn(empty_failure(source, path, frontmatter, diagnostic)) {
        return this.result(false, source, path, frontmatter, [], [], [], [], [diagnostic])
    }

    private fn(node(kind, raw, name, value, attributes, children, position)) {
        return {"type":kind,"source":raw,"name":name,"value":value,"attributes":attributes,"children":children,"position":position}
    }

    private fn(attribute(name, kind, value, raw, position)) {
        return {"name":name,"kind":kind,"value":value,"source":raw,"position":position}
    }

    private fn(markdown_node(source, lines, begin, end, count)) {
        if(count >= 1024) { return {"ok":false,"count":count,"node":null,"diagnostic":this.diagnostic("too_many_syntax_objects", "document exceeds 1024 syntax objects", null, lines, begin)} }
        item := this.node("markdown", this.text(source, begin, end), null, null, [], [], this.position(lines, begin, end))
        return {"ok":true,"count":count + 1,"node":item,"diagnostic":null}
    }

    private fn(frontmatter(source, lines)) {
        absent := {"ok":true,"frontmatter":this.default_frontmatter(),"end":0,"diagnostic":null}
        if(source.length < 3 || source.data[0] != 45 || source.data[1] != 45 || source.data[2] != 45) { return absent }
        first_end := this.line_end(source, 0)
        if(first_end != 3) { return absent }
        body_begin := this.after_line(source, first_end)
        cursor := body_begin
        while(cursor < source.length) {
            end := this.line_end(source, cursor)
            if(end - cursor == 3 && source.data[cursor] == 45 && source.data[cursor + 1] == 45 && source.data[cursor + 2] == 45) {
                finish := this.after_line(source, end)
                front := {"present":true,"source":this.text(source, 0, finish),"body":this.text(source, body_begin, cursor),"start":this.point(lines, 0),"end":this.point(lines, finish)}
                return {"ok":true,"frontmatter":front,"end":finish,"diagnostic":null}
            }
            cursor = this.after_line(source, end)
        }
        issue := this.diagnostic("unclosed_frontmatter", "frontmatter opening delimiter has no closing delimiter", null, lines, 0)
        return {"ok":false,"frontmatter":this.default_frontmatter(),"end":0,"diagnostic":issue}
    }

    private fn(regex_keyword(word)) {
        return word == "return" || word == "case" || word == "throw" || word == "typeof" || word == "void" || word == "delete" || word == "new" || word == "yield" || word == "await" || word == "instanceof" || word == "in" || word == "else" || word == "do"
    }

    private fn(scan_js(source, begin, expression, declaration_kind)) {
        cursor := begin
        length := source.length
        curly := 0
        if(expression) { curly = 1 }
        round := 0
        square := 0
        mode := "code"
        quote := 0
        regex_class := false
        regex_allowed := true
        template_targets := []
        round_controls := []
        curly_statements := []
        control_pending := false
        statement_pending := false
        line_comment_continuation := false
        specifier_seen := false
        needs_specifier := declaration_kind == "import"
        export_prefix_incomplete := declaration_kind == "export"
        maximum := curly
        while(cursor < length) {
            value := source.data[cursor]
            next := -1
            if(cursor + 1 < length) { next = source.data[cursor + 1] }

            if(mode == "line_comment") {
                if(value == 10 || value == 13) {
                    mode = "code"
                    if(!expression && curly == 0 && round == 0 && square == 0) {
                        if(line_comment_continuation) { line_comment_continuation = false; cursor = this.after_line(source, cursor); continue }
                        return {"ok":true,"end":this.after_line(source, cursor),"maximum":maximum}
                    }
                }
                cursor += 1
                continue
            }
            if(mode == "block_comment") {
                if(value == 42 && next == 47) { cursor += 2; mode = "code" } else { cursor += 1 }
                continue
            }
            if(mode == "quote") {
                if(value == 92) { cursor += 2; continue }
                if(value == quote) { mode = "code"; regex_allowed = false; specifier_seen = true; needs_specifier = false }
                cursor += 1
                continue
            }
            if(mode == "regex") {
                if(value == 92) { cursor += 2; continue }
                if(value == 91) { regex_class = true }
                else if(value == 93) { regex_class = false }
                else if(value == 47 && !regex_class) {
                    cursor += 1
                    while(cursor < length && (this.is_alpha(source.data[cursor]) || this.is_digit(source.data[cursor]))) { cursor += 1 }
                    mode = "code"
                    regex_allowed = false
                    continue
                }
                cursor += 1
                continue
            }
            if(mode == "template") {
                if(value == 92) { cursor += 2; continue }
                if(value == 96) { mode = "code"; cursor += 1; regex_allowed = false; continue }
                if(value == 36 && next == 123) {
                    curly += 1
                    curly_statements.push(false)
                    template_targets.push(curly - 1)
                    if(curly + round + square > maximum) { maximum = curly + round + square }
                    mode = "code"
                    cursor += 2
                    regex_allowed = true
                    continue
                }
                cursor += 1
                continue
            }

            if(value == 39 || value == 34) { export_prefix_incomplete = false; mode = "quote"; quote = value; cursor += 1; continue }
            if(value == 96) { export_prefix_incomplete = false; mode = "template"; cursor += 1; continue }
            if(value == 47 && next == 47) {
                line_comment_continuation = !expression && curly == 0 && round == 0 && square == 0 && ((declaration_kind == "import" && !specifier_seen) || (declaration_kind == "export" && (needs_specifier || export_prefix_incomplete)))
                mode = "line_comment"; cursor += 2; continue
            }
            if(value == 47 && next == 42) { mode = "block_comment"; cursor += 2; continue }
            if(value == 47 && regex_allowed) { export_prefix_incomplete = false; mode = "regex"; regex_class = false; cursor += 1; continue }
            if(value == 47) { export_prefix_incomplete = false; regex_allowed = true; cursor += 1; continue }

            if(this.is_alpha(value) || value == 95 || value == 36) {
                word_begin := cursor
                cursor += 1
                while(cursor < length) {
                    word_byte := source.data[cursor]
                    if(!(this.is_alpha(word_byte) || this.is_digit(word_byte) || word_byte == 95 || word_byte == 36)) { break }
                    cursor += 1
                }
                word := this.text(source, word_begin, cursor)
                regex_allowed = this.regex_keyword(word)
                control_pending = word == "if" || word == "while" || word == "for" || word == "switch" || word == "catch" || word == "with"
                statement_pending = word == "else" || word == "do" || word == "try" || word == "finally"
                if(word == "from" && declaration_kind == "export") { needs_specifier = true; specifier_seen = false }
                if(export_prefix_incomplete && word != "default") { export_prefix_incomplete = false }
                continue
            }
            if(this.is_digit(value)) {
                export_prefix_incomplete = false
                cursor += 1
                while(cursor < length) {
                    number_byte := source.data[cursor]
                    if(!(this.is_alpha(number_byte) || this.is_digit(number_byte) || number_byte == 46 || number_byte == 95)) { break }
                    cursor += 1
                }
                regex_allowed = false
                continue
            }

            if(!this.is_space(value)) { export_prefix_incomplete = false }
            if(value == 123) { curly += 1; curly_statements.push(statement_pending); statement_pending = false; regex_allowed = true }
            else if(value == 125) {
                curly -= 1
                if(expression && curly == 0) { return {"ok":true,"end":cursor + 1,"maximum":maximum} }
                if(curly < 0) { return {"ok":false,"end":cursor,"maximum":maximum} }
                closed_statement := false
                if(curly_statements.length() > 0) { closed_statement = curly_statements[curly_statements.length() - 1]; curly_statements.pop() }
                if(template_targets.length() > 0 && curly == template_targets[template_targets.length() - 1]) {
                    template_targets.pop()
                    mode = "template"
                } else { regex_allowed = closed_statement; statement_pending = closed_statement }
            } else if(value == 40) { round += 1; round_controls.push(control_pending); control_pending = false; statement_pending = false; regex_allowed = true }
            else if(value == 41) {
                round -= 1
                closed_control := false
                if(round_controls.length() > 0) { closed_control = round_controls[round_controls.length() - 1]; round_controls.pop() }
                regex_allowed = closed_control
                statement_pending = closed_control
                if(round < 0) { return {"ok":false,"end":cursor,"maximum":maximum} }
            }
            else if(value == 91) { square += 1; regex_allowed = true }
            else if(value == 93) { square -= 1; regex_allowed = false; if(square < 0) { return {"ok":false,"end":cursor,"maximum":maximum} } }
            else if((value == 43 || value == 45) && next == value) { cursor += 2; regex_allowed = false; continue }
            else if(value == 46) { regex_allowed = false }
            else if(value == 59 || value == 44 || value == 58 || value == 63 || value == 61 || value == 33 || value == 38 || value == 124 || value == 43 || value == 45 || value == 42 || value == 37 || value == 94 || value == 126 || value == 60 || value == 62) { regex_allowed = true }

            nesting := curly + round + square
            if(nesting > maximum) { maximum = nesting }
            if(maximum > 64) { return {"ok":false,"end":cursor,"maximum":maximum,"limit":true} }
            if(!expression && value == 59 && curly == 0 && round == 0 && square == 0) { return {"ok":true,"end":cursor + 1,"maximum":maximum} }
            if(!expression && (value == 10 || value == 13) && curly == 0 && round == 0 && square == 0) { return {"ok":true,"end":this.after_line(source, cursor),"maximum":maximum} }
            cursor += 1
        }
        if(expression) { return {"ok":false,"end":length,"maximum":maximum} }
        if(mode == "quote" || mode == "block_comment" || mode == "template" || mode == "regex" || curly != 0 || round != 0 || square != 0) { return {"ok":false,"end":length,"maximum":maximum} }
        return {"ok":true,"end":length,"maximum":maximum}
    }

    private fn(expression_parts(source, lines, begin)) {
        scanned := this.scan_js(source, begin + 1, true, null)
        if(!scanned.ok) {
            code := "unclosed_expression"
            message := "expression container is not closed"
            if(scanned.has("limit") && scanned.limit) { code = "nesting_limit"; message = "expression nesting exceeds 64" }
            return {"ok":false,"end":scanned.end,"raw":null,"value":null,"position":null,"diagnostic":this.diagnostic(code, message, null, lines, begin)}
        }
        finish := scanned.end
        value := this.text(source, begin + 1, finish - 1)
        return {"ok":true,"end":finish,"raw":this.text(source, begin, finish),"value":value,"position":this.position(lines, begin, finish),"diagnostic":null}
    }

    private fn(expression_node(source, lines, begin, count)) {
        parts := this.expression_parts(source, lines, begin)
        if(!parts.ok) { return {"ok":false,"end":parts.end,"count":count,"node":null,"diagnostic":parts.diagnostic} }
        if(count >= 1024) { return {"ok":false,"end":parts.end,"count":count,"node":null,"diagnostic":this.diagnostic("too_many_syntax_objects", "document exceeds 1024 syntax objects", null, lines, begin)} }
        item := this.node("expression", parts.raw, null, parts.value, [], [], parts.position)
        return {"ok":true,"end":parts.end,"count":count + 1,"node":item,"diagnostic":null}
    }

    private fn(backtick_matches(source)) {
        matches := {}
        last := {}
        cursor := source.length - 1
        while(cursor >= 0) {
            cursor = source.raw.substr(0, cursor + 1).last_index_of("`")
            if(cursor < 0) { break }
            finish := cursor + 1
            while(cursor >= 0 && source.data[cursor] == 96) { cursor -= 1 }
            begin := cursor + 1
            run_key := (finish - begin).to_string()
            next := -1
            if(last.has(run_key)) { next = last[run_key] }
            matches[begin.to_string()] = next
            last[run_key] = begin
        }
        return matches
    }

    private fn(inline_code(source, lines, begin, syntax_count, backticks)) {
        run := 0
        while(begin + run < source.length && source.data[begin + run] == 96) { run += 1 }
        if(run > 256) { return {"ok":false,"fatal":true,"end":begin + run,"count":syntax_count,"node":null,"diagnostic":this.diagnostic("delimiter_run_too_long", "backtick run exceeds 256 bytes", null, lines, begin)} }
        cursor := backticks[begin.to_string()]
        if(cursor >= 0) {
            finish := cursor + run
            if(syntax_count >= 1024) { return {"ok":false,"fatal":true,"end":finish,"count":syntax_count,"node":null,"diagnostic":this.diagnostic("too_many_syntax_objects", "document exceeds 1024 syntax objects", null, lines, begin)} }
            item := this.node("inline_code", this.text(source, begin, finish), null, this.text(source, begin + run, cursor), [], [], this.position(lines, begin, finish))
            return {"ok":true,"fatal":false,"end":finish,"count":syntax_count + 1,"node":item,"diagnostic":null}
        }
        return {"ok":false,"fatal":false,"end":begin + run,"count":syntax_count,"node":null,"diagnostic":null}
    }

    private fn(fence(source, lines, begin, syntax_count)) {
        cursor := begin
        spaces := 0
        while(cursor < source.length && spaces < 3 && source.data[cursor] == 32) { spaces += 1; cursor += 1 }
        if(cursor >= source.length) { return {"matched":false} }
        marker := source.data[cursor]
        if(marker != 96 && marker != 126) { return {"matched":false} }
        count := 0
        while(cursor + count < source.length && source.data[cursor + count] == marker) { count += 1 }
        if(count < 3) { return {"matched":false} }
        if(count > 256) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("delimiter_run_too_long", "fence marker run exceeds 256 bytes", null, lines, cursor)} }
        opening_end := this.line_end(source, begin)
        scan := this.after_line(source, opening_end)
        finish := source.length
        while(scan < source.length) {
            line_cursor := scan
            close_spaces := 0
            while(line_cursor < source.length && close_spaces < 3 && source.data[line_cursor] == 32) { close_spaces += 1; line_cursor += 1 }
            close_count := 0
            while(line_cursor + close_count < source.length && source.data[line_cursor + close_count] == marker) { close_count += 1 }
            if(close_count > 256) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("delimiter_run_too_long", "fence marker run exceeds 256 bytes", null, lines, line_cursor)} }
            line_finish := this.line_end(source, scan)
            tail := line_cursor + close_count
            while(tail < line_finish && this.is_hspace(source.data[tail])) { tail += 1 }
            if(close_count >= count && tail == line_finish) { finish = this.after_line(source, line_finish); scan = source.length }
            else { scan = this.after_line(source, line_finish) }
        }
        info_begin := cursor + count
        while(info_begin < opening_end && this.is_hspace(source.data[info_begin])) { info_begin += 1 }
        info_end := opening_end
        while(info_end > info_begin && this.is_hspace(source.data[info_end - 1])) { info_end -= 1 }
        fence_name := [null]
        if(info_begin < info_end) { fence_name[0] = this.text(source, info_begin, info_end) }
        if(syntax_count >= 1024) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("too_many_syntax_objects", "document exceeds 1024 syntax objects", null, lines, begin)} }
        item := this.node("code_fence", this.text(source, begin, finish), fence_name[0], null, [], [], this.position(lines, begin, finish))
        return {"matched":true,"ok":true,"count":syntax_count + 1,"end":finish,"node":item,"diagnostic":null}
    }

    private fn(keyword_at(source, offset, keyword)) {
        encoded := keyword.encode("utf-8")
        if(offset + encoded.length() > source.length) { return false }
        return this.text(source, offset, offset + encoded.length()) == keyword
    }

    private fn(quoted_value(source, begin)) {
        quote := source.data[begin]
        cursor := begin + 1
        while(cursor < source.length) {
            value := source.data[cursor]
            if(value == 92) { cursor += 2 }
            else if(value == quote) { return {"ok":true,"value":this.text(source, begin + 1, cursor),"end":cursor + 1} }
            else { cursor += 1 }
        }
        return {"ok":false,"value":null,"end":source.length}
    }

    private fn(jsx_quoted_value(source, begin)) {
        quote := source.data[begin]
        cursor := begin + 1
        while(cursor < source.length) {
            if(source.data[cursor] == quote) { return {"ok":true,"value":this.text(source, begin + 1, cursor),"end":cursor + 1} }
            cursor += 1
        }
        return {"ok":false,"value":null,"end":source.length}
    }

    private fn(hex_value(value)) {
        if(value >= 48 && value <= 57) { return value - 48 }
        if(value >= 65 && value <= 70) { return value - 55 }
        if(value >= 97 && value <= 102) { return value - 87 }
        return -1
    }

    private fn(decode_specifier(source, begin, end, quote)) {
        output := []
        cursor := begin
        while(cursor < end) {
            value := source.data[cursor]
            if(value != 92) { output.push(value); cursor += 1; continue }
            if(cursor + 1 >= end) { return {"ok":false,"value":null} }
            escaped := source.data[cursor + 1]
            if(escaped == 92 || escaped == quote || escaped == 39 || escaped == 34) { output.push(escaped); cursor += 2; continue }
            if(escaped == 120 && cursor + 3 < end) {
                high := this.hex_value(source.data[cursor + 2])
                low := this.hex_value(source.data[cursor + 3])
                if(high < 0 || low < 0 || high * 16 + low > 127) { return {"ok":false,"value":null} }
                output.push(high * 16 + low); cursor += 4; continue
            }
            if(escaped == 117 && cursor + 5 < end && source.data[cursor + 2] == 48 && source.data[cursor + 3] == 48) {
                unicode_high := this.hex_value(source.data[cursor + 4])
                unicode_low := this.hex_value(source.data[cursor + 5])
                if(unicode_high < 0 || unicode_low < 0 || unicode_high * 16 + unicode_low > 127) { return {"ok":false,"value":null} }
                output.push(unicode_high * 16 + unicode_low); cursor += 6; continue
            }
            return {"ok":false,"value":null}
        }
        return {"ok":true,"value":bytes(output).decode("utf-8")}
    }

    private fn(specifier_quote(source, begin)) {
        quoted := this.quoted_value(source, begin)
        if(!quoted.ok) { return {"ok":false,"specifier":null,"end":quoted.end,"escape":false} }
        decoded := this.decode_specifier(source, begin + 1, quoted.end - 1, source.data[begin])
        if(!decoded.ok) { return {"ok":false,"specifier":null,"end":quoted.end,"escape":true} }
        return {"ok":true,"specifier":decoded.value,"end":quoted.end,"escape":false}
    }

    private fn(skip_js_space(source, begin, end)) {
        cursor := begin
        while(cursor < end) {
            while(cursor < end && this.is_space(source.data[cursor])) { cursor += 1 }
            if(cursor + 1 >= end || source.data[cursor] != 47) { break }
            next := source.data[cursor + 1]
            if(next == 47) {
                cursor += 2
                while(cursor < end && source.data[cursor] != 10 && source.data[cursor] != 13) { cursor += 1 }
                continue
            }
            if(next == 42) {
                cursor += 2
                closed := false
                while(cursor + 1 < end) {
                    if(source.data[cursor] == 42 && source.data[cursor + 1] == 47) { cursor += 2; closed = true; break }
                    cursor += 1
                }
                if(!closed) { return {"ok":false,"end":cursor} }
                continue
            }
            break
        }
        return {"ok":true,"end":cursor}
    }

    private fn(import_specifier(source, begin, end)) {
        skipped := this.skip_js_space(source, begin + 6, end)
        if(!skipped.ok) { return {"kind":"import","specifier":null,"escape":false} }
        cursor := skipped.end
        if(cursor < end && (source.data[cursor] == 39 || source.data[cursor] == 34)) {
            side := this.specifier_quote(source, cursor)
            if(side.escape) { return {"kind":"side_effect","specifier":null,"escape":true} }
            if(side.ok && side.end <= end) { return {"kind":"side_effect","specifier":side.specifier,"escape":false} }
        }
        mode := "code"
        quote := 0
        while(cursor < end) {
            value := source.data[cursor]
            next := -1
            if(cursor + 1 < end) { next = source.data[cursor + 1] }
            if(mode == "quote") { if(value == 92) { cursor += 2 } else { if(value == quote) { mode = "code" }; cursor += 1 }; continue }
            if(mode == "line_comment") { if(value == 10 || value == 13) { mode = "code" }; cursor += 1; continue }
            if(mode == "block_comment") { if(value == 42 && next == 47) { cursor += 2; mode = "code" } else { cursor += 1 }; continue }
            if(value == 39 || value == 34 || value == 96) { mode = "quote"; quote = value; cursor += 1; continue }
            if(value == 47 && next == 47) { mode = "line_comment"; cursor += 2; continue }
            if(value == 47 && next == 42) { mode = "block_comment"; cursor += 2; continue }
            if(this.keyword_at(source, cursor, "from")) {
                before_ok := cursor == begin || !this.name_byte(source.data[cursor - 1])
                after := cursor + 4
                after_ok := after >= end || !this.name_byte(source.data[after])
                if(before_ok && after_ok) {
                    after_space := this.skip_js_space(source, after, end)
                    if(!after_space.ok) { return {"kind":"import","specifier":null,"escape":false} }
                    after = after_space.end
                    if(after < end && (source.data[after] == 39 || source.data[after] == 34)) {
                        from_quoted := this.specifier_quote(source, after)
                        if(from_quoted.escape) { return {"kind":"import","specifier":null,"escape":true} }
                        if(from_quoted.ok && from_quoted.end <= end) { return {"kind":"import","specifier":from_quoted.specifier,"escape":false} }
                    }
                }
            }
            cursor += 1
        }
        return {"kind":"import","specifier":null,"escape":false}
    }

    private fn(export_specifier(source, begin, end)) {
        return this.import_specifier(source, begin, end)
    }

    private fn(extension(specifier)) {
        for(suffix : [".mdx",".jsx",".tsx",".js",".ts",".md"]) { if(specifier.ends_with(suffix)) { return suffix } }
        return ""
    }

    private fn(import_record(source, lines, begin, end, info)) {
        specifier := info.specifier
        local := false
        extension := ""
        dependency := false
        if(specifier != null) {
            local = specifier.starts_with("./") || specifier.starts_with("../")
            extension = this.extension(specifier)
            dependency = local && (extension == ".mdx" || extension == ".md") && !specifier.contains("?") && !specifier.contains("#")
        }
        return {"kind":info.kind,"specifier":specifier,"source":this.text(source, begin, end),"local":local,"extension":extension,"dependency":dependency,"position":this.position(lines, begin, end)}
    }

    private fn(jsx_name(source, begin, end, attribute)) {
        if(begin >= end || !this.name_start(source.data[begin])) { return {"ok":false,"end":begin} }
        cursor := begin + 1
        while(cursor < end && this.name_byte(source.data[cursor])) { cursor += 1 }
        if(cursor < end && source.data[cursor] == 58) {
            cursor += 1
            if(cursor >= end || !this.name_start(source.data[cursor])) { return {"ok":false,"end":cursor} }
            cursor += 1
            while(cursor < end && this.name_byte(source.data[cursor])) { cursor += 1 }
            if(cursor < end && (source.data[cursor] == 58 || source.data[cursor] == 46)) { return {"ok":false,"end":cursor} }
        } else if(cursor < end && source.data[cursor] == 46) {
            if(attribute) { return {"ok":false,"end":cursor} }
            while(cursor < end && source.data[cursor] == 46) {
                cursor += 1
                if(cursor >= end || !this.name_start(source.data[cursor])) { return {"ok":false,"end":cursor} }
                cursor += 1
                while(cursor < end && this.name_byte(source.data[cursor])) { cursor += 1 }
            }
            if(cursor < end && source.data[cursor] == 58) { return {"ok":false,"end":cursor} }
        }
        return {"ok":true,"end":cursor}
    }

    private fn(jsx(source, lines, begin, depth, syntax_count, backticks)) {
        if(depth > 48) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("nesting_limit", "JSX nesting exceeds 48", null, lines, begin)} }
        length := source.length
        if(begin + 1 >= length || source.data[begin] != 60) { return {"matched":false} }
        if(begin + 3 < length && source.data[begin + 1] == 33 && source.data[begin + 2] == 45 && source.data[begin + 3] == 45) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("html_comment_unsupported", "HTML comments are not supported", null, lines, begin)} }
        cursor := begin + 1
        fragment := false
        if(source.data[cursor] == 62) { fragment = true; cursor += 1 }
        else {
            if(source.data[cursor] == 47 || !this.name_start(source.data[cursor])) { return {"matched":false} }
        }
        tag_name := [null]
        if(!fragment) {
            name_begin := cursor
            parsed_name := this.jsx_name(source, cursor, length, false)
            if(!parsed_name.ok) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("invalid_jsx_name", "invalid JSX element name", null, lines, parsed_name.end)} }
            cursor = parsed_name.end
            tag_name[0] = this.text(source, name_begin, cursor)
        }

        attributes := []
        self_closing := false
        if(!fragment) {
            while(cursor < length) {
                while(cursor < length && this.is_space(source.data[cursor])) { cursor += 1 }
                if(cursor >= length) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("unclosed_jsx", "JSX opening tag is not closed", null, lines, begin)} }
                if(source.data[cursor] == 62) { cursor += 1; break }
                if(source.data[cursor] == 47 && cursor + 1 < length && source.data[cursor + 1] == 62) { cursor += 2; self_closing = true; break }
                attr_begin := cursor
                if(source.data[cursor] == 123) {
                    expression := this.expression_parts(source, lines, cursor)
                    if(!expression.ok) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":expression.diagnostic} }
                    interior := expression.value.trim()
                    if(!interior.starts_with("...")) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("invalid_jsx_attribute", "bare JSX attribute expression must be a spread", null, lines, attr_begin)} }
                    if(syntax_count >= 1024) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("too_many_syntax_objects", "document exceeds 1024 syntax objects", null, lines, attr_begin)} }
                    attributes.push(this.attribute(null, "spread", interior.substr(3).trim(), expression.raw, expression.position))
                    syntax_count += 1
                    cursor = expression.end
                    continue
                }
                parsed_attribute_name := this.jsx_name(source, cursor, length, true)
                if(!parsed_attribute_name.ok) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("invalid_jsx_attribute", "invalid JSX attribute name", null, lines, parsed_attribute_name.end)} }
                name_end := parsed_attribute_name.end
                attr_name := this.text(source, cursor, name_end)
                cursor = name_end
                while(cursor < length && this.is_space(source.data[cursor])) { cursor += 1 }
                kind := "boolean"
                attr_value := [null]
                if(cursor < length && source.data[cursor] == 61) {
                    cursor += 1
                    while(cursor < length && this.is_space(source.data[cursor])) { cursor += 1 }
                    if(cursor >= length) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("invalid_jsx_attribute", "JSX attribute value is missing", null, lines, attr_begin)} }
                    if(source.data[cursor] == 39 || source.data[cursor] == 34) {
                        quoted := this.jsx_quoted_value(source, cursor)
                        if(!quoted.ok) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("unclosed_jsx_attribute", "quoted JSX attribute is not closed", null, lines, cursor)} }
                        kind = "string"; attr_value[0] = quoted.value; cursor = quoted.end
                    } else if(source.data[cursor] == 123) {
                        attribute_expression := this.expression_parts(source, lines, cursor)
                        if(!attribute_expression.ok) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":attribute_expression.diagnostic} }
                        kind = "expression"; attr_value[0] = attribute_expression.value; cursor = attribute_expression.end
                    } else { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("invalid_jsx_attribute", "unquoted JSX attributes are not supported", null, lines, cursor)} }
                }
                if(syntax_count >= 1024) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("too_many_syntax_objects", "document exceeds 1024 syntax objects", null, lines, attr_begin)} }
                attributes.push(this.attribute(attr_name, kind, attr_value[0], this.text(source, attr_begin, cursor), this.position(lines, attr_begin, cursor)))
                syntax_count += 1
            }
        }

        if(self_closing) {
            if(syntax_count >= 1024) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("too_many_syntax_objects", "document exceeds 1024 syntax objects", null, lines, begin)} }
            item := this.node("jsx_element", this.text(source, begin, cursor), tag_name[0], null, attributes, [], this.position(lines, begin, cursor))
            return {"matched":true,"ok":true,"count":syntax_count + 1,"end":cursor,"node":item}
        }

        children := []
        markdown_begin := cursor
        while(cursor < length) {
            value_byte := source.data[cursor]
            special := false
            child := [null]
            child_end := cursor
            if(this.at_line_start(source, cursor)) {
                child_fence := this.fence(source, lines, cursor, syntax_count)
                if(child_fence.matched) {
                    if(!child_fence.ok) { return child_fence }
                    special = true; child[0] = child_fence.node; child_end = child_fence.end; syntax_count = child_fence.count
                }
            }
            if(!special && value_byte == 60 && !this.escaped(source, cursor)) {
                if(cursor + 1 < length && source.data[cursor + 1] == 47) {
                    close := cursor + 2
                    if(fragment) {
                        while(close < length && this.is_space(source.data[close])) { close += 1 }
                        if(close >= length || source.data[close] != 62) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("mismatched_jsx", "fragment closing tag does not match", null, lines, cursor)} }
                        close += 1
                    } else {
                        close_begin := close
                        closing_name := this.jsx_name(source, close, length, false)
                        if(!closing_name.ok) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("invalid_jsx_name", "invalid JSX closing name", null, lines, closing_name.end)} }
                        close = closing_name.end
                        close_name := this.text(source, close_begin, close)
                        while(close < length && this.is_space(source.data[close])) { close += 1 }
                        if(close_name != tag_name[0] || close >= length || source.data[close] != 62) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("mismatched_jsx", "JSX closing tag does not match", null, lines, cursor)} }
                        close += 1
                    }
                    if(markdown_begin < cursor) {
                        markdown := this.markdown_node(source, lines, markdown_begin, cursor, syntax_count)
                        if(!markdown.ok) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":markdown.diagnostic} }
                        children.push(markdown.node); syntax_count = markdown.count
                    }
                    if(syntax_count >= 1024) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("too_many_syntax_objects", "document exceeds 1024 syntax objects", null, lines, begin)} }
                    kind := "jsx_element"
                    if(fragment) { kind = "jsx_fragment" }
                    item := this.node(kind, this.text(source, begin, close), tag_name[0], null, attributes, children, this.position(lines, begin, close))
                    return {"matched":true,"ok":true,"count":syntax_count + 1,"end":close,"node":item}
                }
                parsed_jsx := this.jsx(source, lines, cursor, depth + 1, syntax_count, backticks)
                if(parsed_jsx.matched) {
                    if(!parsed_jsx.ok) { return parsed_jsx }
                    special = true; child[0] = parsed_jsx.node; child_end = parsed_jsx.end; syntax_count = parsed_jsx.count
                }
            } else if(!special && value_byte == 123 && !this.escaped(source, cursor)) {
                child_expression := this.expression_node(source, lines, cursor, syntax_count)
                if(!child_expression.ok) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":child_expression.diagnostic} }
                special = true; child[0] = child_expression.node; child_end = child_expression.end; syntax_count = child_expression.count
            } else if(!special && value_byte == 96) {
                inline := this.inline_code(source, lines, cursor, syntax_count, backticks)
                if(inline.fatal) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":inline.diagnostic} }
                if(inline.ok) { special = true; child[0] = inline.node; child_end = inline.end; syntax_count = inline.count }
                else { cursor = inline.end; continue }
            }
            if(special) {
                if(markdown_begin < cursor) {
                    markdown_child := this.markdown_node(source, lines, markdown_begin, cursor, syntax_count)
                    if(!markdown_child.ok) { return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":markdown_child.diagnostic} }
                    children.push(markdown_child.node); syntax_count = markdown_child.count
                }
                children.push(child[0])
                cursor = child_end
                markdown_begin = cursor
            } else {
                finish_plain := this.plain_end(source, cursor, false)
                if(finish_plain > cursor) { cursor = finish_plain } else { cursor += 1 }
            }
        }
        return {"matched":true,"ok":false,"count":syntax_count,"diagnostic":this.diagnostic("unclosed_jsx", "JSX element is not closed", null, lines, begin)}
    }

    private fn(plain_end(source, cursor, statements)) {
        tail := source.raw.substr(cursor)
        distance := source.length - cursor
        markers := ["<", "{", "`", "~"]
        if(statements) { markers.push("import"); markers.push("export") }
        for(marker : markers) {
            found := tail.index_of(marker)
            if(found >= 0) {
                if(marker != "<" && marker != "{") {
                    prefix := tail.substr(0, found)
                    line_start := max(prefix.last_index_of("\n"), prefix.last_index_of("\r")) + 1
                    if(line_start > 0) { found = line_start }
                }
                if(found < distance) { distance = found }
            }
        }
        return cursor + distance
    }

    private fn(parse_core(value, path, provided_bytes, limits)) {
        lines := [0]
        issue := [null]
        if(type(value) != "string") {
            issue[0] = this.diagnostic("invalid_type", "source must be a string", path, lines, 0)
            return this.empty_failure(null, path, this.default_frontmatter(), issue[0])
        }
        encoded := value.encode("utf-8")
        if(provided_bytes != null) { encoded = provided_bytes }
        if(encoded.length() > limits.bytes) {
            issue[0] = this.diagnostic("source_too_large", "source exceeds " + limits.bytes.to_string() + " bytes", path, lines, 0)
            return this.empty_failure(null, path, this.default_frontmatter(), issue[0])
        }
        source := {"raw":value,"data":encoded,"length":encoded.length(),"limits":limits}
        line_scan := this.line_info(source)
        lines = line_scan.starts
        if(!line_scan.ok) {
            issue[0] = this.diagnostic("physical_line_too_long", "physical line exceeds " + limits.line_bytes.to_string() + " bytes", path, lines, line_scan.offset)
            return this.empty_failure(value, path, this.default_frontmatter(), issue[0])
        }
        if(lines.length() > limits.lines) {
            issue[0] = this.diagnostic("too_many_lines", "source exceeds " + limits.lines.to_string() + " lines", path, lines, 0)
            return this.empty_failure(value, path, this.default_frontmatter(), issue[0])
        }
        backticks := this.backtick_matches(source)
        front := this.frontmatter(source, lines)
        if(!front.ok) { front_issue := front.diagnostic; front_issue["path"] = path; return this.empty_failure(value, path, front.frontmatter, front_issue) }

        imports := []
        exports := []
        nodes := []
        diagnostics := []
        cursor := front.end
        markdown_begin := cursor
        structural := false
        syntax_count := 0
        paragraph_open := false
        line_nonspace := false
        while(cursor < source.length) {
            special := false
            parsed_node := [null]
            finish := cursor
            issue[0] = null

            if(this.at_line_start(source, cursor)) {
                fenced := this.fence(source, lines, cursor, syntax_count)
                if(fenced.matched) {
                    if(!fenced.ok) { issue[0] = fenced.diagnostic }
                    else { special = true; parsed_node[0] = fenced.node; finish = fenced.end; syntax_count = fenced.count; paragraph_open = false; line_nonspace = false }
                }
                else {
                    lead := cursor
                    while(lead < source.length && this.is_hspace(source.data[lead])) { lead += 1 }
                    if(!paragraph_open && lead == cursor && this.keyword_at(source, lead, "import") && lead + 6 < source.length && this.is_hspace(source.data[lead + 6])) {
                        scanned := this.scan_js(source, lead + 6, false, "import")
                        if(!scanned.ok) { issue[0] = this.diagnostic("malformed_import", "import declaration is structurally incomplete", path, lines, lead) }
                        else {
                            info := this.import_specifier(source, lead, scanned.end)
                            if(info.escape) { issue[0] = this.diagnostic("unsupported_specifier_escape", "import specifier uses an unsupported escape", path, lines, lead) }
                            else if(info.specifier == null) { issue[0] = this.diagnostic("malformed_import", "static import has no quoted specifier", path, lines, lead) }
                            else if(info.specifier.encode("utf-8").length() > 4000) { issue[0] = this.diagnostic("specifier_too_long", "import specifier exceeds 4000 bytes", path, lines, lead) }
                            else if(syntax_count >= 1024) { issue[0] = this.diagnostic("too_many_syntax_objects", "document exceeds 1024 syntax objects", path, lines, lead) }
                            else {
                                record := this.import_record(source, lines, lead, scanned.end, info)
                                imports.push(record)
                                parsed_node[0] = this.node("import", record.source, null, record.specifier, [], [], record.position)
                                syntax_count += 1; finish = scanned.end; special = true; paragraph_open = false; line_nonspace = false
                            }
                        }
                    } else if(!paragraph_open && lead == cursor && this.keyword_at(source, lead, "export") && lead + 6 < source.length && this.is_hspace(source.data[lead + 6])) {
                        scanned_export := this.scan_js(source, lead + 6, false, "export")
                        if(!scanned_export.ok) { issue[0] = this.diagnostic("malformed_export", "export declaration is structurally incomplete", path, lines, lead) }
                        else {
                            raw := this.text(source, lead, scanned_export.end)
                            export_info := this.export_specifier(source, lead, scanned_export.end)
                            specifier := export_info.specifier
                            if(export_info.escape) { issue[0] = this.diagnostic("unsupported_specifier_escape", "export specifier uses an unsupported escape", path, lines, lead) }
                            else if(specifier != null && specifier.encode("utf-8").length() > 4000) { issue[0] = this.diagnostic("specifier_too_long", "export specifier exceeds 4000 bytes", path, lines, lead) }
                            else if(syntax_count >= 1024) { issue[0] = this.diagnostic("too_many_syntax_objects", "document exceeds 1024 syntax objects", path, lines, lead) }
                            else {
                                record_export := {"source":raw,"specifier":specifier,"position":this.position(lines, lead, scanned_export.end)}
                                exports.push(record_export)
                                parsed_node[0] = this.node("export", raw, null, specifier, [], [], record_export.position)
                                syntax_count += 1; finish = scanned_export.end; special = true; paragraph_open = false; line_nonspace = false
                            }
                        }
                    }
                }
            }
            if(issue[0] == null && !special && source.data[cursor] == 96) {
                inline := this.inline_code(source, lines, cursor, syntax_count, backticks)
                if(inline.fatal) { issue[0] = inline.diagnostic }
                else if(inline.ok) { special = true; parsed_node[0] = inline.node; finish = inline.end; syntax_count = inline.count; paragraph_open = true; line_nonspace = true }
                else { cursor = inline.end; paragraph_open = true; line_nonspace = true; continue }
            }
            if(issue[0] == null && !special && source.data[cursor] == 123 && !this.escaped(source, cursor)) {
                expression := this.expression_node(source, lines, cursor, syntax_count)
                if(!expression.ok) { expression_issue := expression.diagnostic; expression_issue["path"] = path; issue[0] = expression_issue }
                else { special = true; parsed_node[0] = expression.node; finish = expression.end; syntax_count = expression.count; paragraph_open = true; line_nonspace = true }
            }
            if(issue[0] == null && !special && source.data[cursor] == 60 && !this.escaped(source, cursor)) {
                parsed_jsx := this.jsx(source, lines, cursor, 1, syntax_count, backticks)
                if(parsed_jsx.matched) {
                    if(!parsed_jsx.ok) { jsx_issue := parsed_jsx.diagnostic; jsx_issue["path"] = path; issue[0] = jsx_issue }
                    else { special = true; parsed_node[0] = parsed_jsx.node; finish = parsed_jsx.end; syntax_count = parsed_jsx.count; paragraph_open = true; line_nonspace = true }
                }
            }
            if(issue[0] != null) { diagnostics.push(issue[0]); structural = true; cursor = source.length; continue }
            if(special) {
                if(markdown_begin < cursor) {
                    markdown := this.markdown_node(source, lines, markdown_begin, cursor, syntax_count)
                    if(!markdown.ok) { diagnostic_markdown := markdown.diagnostic; diagnostic_markdown["path"] = path; diagnostics.push(diagnostic_markdown); structural = true; cursor = source.length; continue }
                    nodes.push(markdown.node); syntax_count = markdown.count
                }
                nodes.push(parsed_node[0])
                if(imports.length() > 256) { diagnostics.push(this.diagnostic("too_many_imports", "document exceeds 256 imports", path, lines, cursor)); structural = true; cursor = source.length; continue }
                cursor = finish
                markdown_begin = cursor
            } else {
                ordinary := source.data[cursor]
                if(ordinary == 10 || ordinary == 13) {
                    if(!line_nonspace) { paragraph_open = false } else { paragraph_open = true }
                    line_nonspace = false
                    if(ordinary == 13 && cursor + 1 < source.length && source.data[cursor + 1] == 10) { cursor += 2 } else { cursor += 1 }
                } else {
                    if(!this.is_hspace(ordinary)) {
                        line_nonspace = true; paragraph_open = true
                        tail := source.raw.substr(cursor)
                        distance := this.plain_end(source, cursor, true) - cursor
                        if(distance > 0) {
                            span := tail.substr(0, distance)
                            parts := span.replace("\r\n", "\n").replace("\r", "\n").split("\n")
                            if(parts.length() > 1) {
                                final := parts[parts.length() - 1].replace(" ", "").replace("\t", "")
                                previous := parts[parts.length() - 2].replace(" ", "").replace("\t", "")
                                line_nonspace = final != ""
                                paragraph_open = line_nonspace || previous != ""
                            }
                            cursor += distance
                        } else { cursor += 1 }
                    } else { cursor += 1 }
                }
            }
        }
        if(!structural && markdown_begin < source.length) {
            final_markdown := this.markdown_node(source, lines, markdown_begin, source.length, syntax_count)
            if(!final_markdown.ok) { final_issue := final_markdown.diagnostic; final_issue["path"] = path; diagnostics.push(final_issue); structural = true }
            else { nodes.push(final_markdown.node); syntax_count = final_markdown.count }
        }
        if(structural) { return this.result(false, value, path, front.frontmatter, [], [], [], [], diagnostics) }
        return this.result(true, value, path, front.frontmatter, imports, exports, nodes, [], diagnostics)
    }

    private fn(normalize(path)) {
        if(os() == "windows") { path = path.replace("\\", "/") }
        absolute := path.starts_with("/")
        parts := path.split("/")
        clean := []
        for(part : parts) {
            if(part == "" || part == ".") { continue }
            if(part == "..") {
                if(clean.length() == 0) { return null }
                clean.pop()
            } else { clean.push(part) }
        }
        result := clean.join("/")
        if(absolute) { result = "/" + result }
        return result
    }

    private fn(dirname(path)) {
        parts := path.split("/")
        if(parts.length() <= 1) { return "" }
        parts.pop()
        result := parts.join("/")
        if(result == "" && path.starts_with("/")) { return "/" }
        return result
    }

    private fn(resolve(from, specifier)) {
        base := this.dirname(from)
        joined := specifier
        if(base != "") { joined = base + "/" + specifier }
        return this.normalize(joined)
    }

    private fn(project_path(path)) {
        root := pwd()
        if(os() == "windows") { root = root.replace("\\", "/"); path = path.replace("\\", "/") }
        normalized := this.normalize(path)
        if(normalized == null) { return null }
        drive_absolute := os() == "windows" && normalized.length() >= 3 && normalized.substr(1, 2) == ":/"
        if(os() == "windows" && (path.starts_with("//") || (path.contains(":") && !drive_absolute))) { return null }
        if(normalized.starts_with("/") || drive_absolute) {
            prefix := root
            if(!prefix.ends_with("/")) { prefix += "/" }
            if(!normalized.starts_with(prefix)) { return null }
            normalized = normalized.substr(prefix.encode("utf-8").length())
        }
        return normalized
    }

    private fn(register(path)) { @dep('$[path]') }

    private fn(graph_diagnostic(code, message, path)) {
        return {"code":code,"message":message,"severity":"error","path":path,"line":1,"column":1,"offset":0}
    }

    private fn(graph_state(root_result, dependencies, dependency_paths, diagnostics, diagnostic_keys, visited, visiting, files, aggregate, limits)) {
        return {"root":root_result,"dependencies":dependencies,"dependency_paths":dependency_paths,"diagnostics":diagnostics,"diagnostic_keys":diagnostic_keys,"visited":visited,"visiting":visiting,"files":files,"aggregate":aggregate,"limits":limits}
    }

    private fn(add_graph_diagnostic(state, diagnostic)) {
        key := diagnostic.code + "|" + diagnostic.path.to_string() + "|" + diagnostic.offset.to_string()
        if(state.diagnostic_keys.contains(key)) { return state }
        diagnostics := state.diagnostics
        keys := state.diagnostic_keys
        diagnostics.push(diagnostic)
        keys.push(key)
        return this.graph_state(state.root, state.dependencies, state.dependency_paths, diagnostics, keys, state.visited, state.visiting, state.files, state.aggregate, state.limits)
    }

    private fn(walk(path, depth, root, state)) {
        if(depth > 16) { return this.add_graph_diagnostic(state, this.graph_diagnostic("dependency_depth_limit", "dependency depth exceeds 16", path)) }
        if(state.visiting.contains(path)) { return this.add_graph_diagnostic(state, this.graph_diagnostic("dependency_cycle", "dependency cycle detected", path)) }
        if(state.visited.contains(path)) { return state }
        if(state.files >= 64) { return this.add_graph_diagnostic(state, this.graph_diagnostic("dependency_file_limit", "recursive file count exceeds 64", path)) }
        if(!exists(path)) { return this.add_graph_diagnostic(state, this.graph_diagnostic("missing_file", "input file does not exist", path)) }
        if(is_dir(path)) { return this.add_graph_diagnostic(state, this.graph_diagnostic("input_is_directory", "input path is a directory", path)) }
        this.register(path)
        source_text := open(path)
        source_bytes := source_text.encode("utf-8")
        next_aggregate := state.aggregate + source_bytes.length()
        state = this.graph_state(state.root, state.dependencies, state.dependency_paths, state.diagnostics, state.diagnostic_keys, state.visited, state.visiting, state.files + 1, next_aggregate, state.limits)
        if(next_aggregate > state.limits.aggregate) { return this.add_graph_diagnostic(state, this.graph_diagnostic("aggregate_source_limit", "recursive source exceeds " + state.limits.aggregate.to_string() + " bytes", path)) }
        parsed := this.parse_core(source_text, path, source_bytes, state.limits)
        if(path == root) { state = this.graph_state(parsed, state.dependencies, state.dependency_paths, state.diagnostics, state.diagnostic_keys, state.visited, state.visiting, state.files, state.aggregate, state.limits) }
        for(issue : parsed.diagnostics) { state = this.add_graph_diagnostic(state, issue) }
        if(!parsed.ok) {
            failed_visited := state.visited; failed_visited.push(path)
            return this.graph_state(state.root, state.dependencies, state.dependency_paths, state.diagnostics, state.diagnostic_keys, failed_visited, state.visiting, state.files, state.aggregate, state.limits)
        }
        visiting := state.visiting; visiting.push(path)
        state = this.graph_state(state.root, state.dependencies, state.dependency_paths, state.diagnostics, state.diagnostic_keys, state.visited, visiting, state.files, state.aggregate, state.limits)
        for(record : parsed.imports) {
            if(!record.dependency) { continue }
            resolved := this.resolve(path, record.specifier)
            if(resolved == null || resolved == "") { state = this.add_graph_diagnostic(state, this.graph_diagnostic("path_escape", "dependency escapes the project root", path)); continue }
            if(resolved == root || state.visiting.contains(resolved)) { state = this.add_graph_diagnostic(state, this.graph_diagnostic("dependency_cycle", "dependency cycle detected", resolved)); continue }
            if(state.dependency_paths.contains(resolved)) { continue }
            dependencies := state.dependencies
            dependency_paths := state.dependency_paths
            dependencies.push({"path":resolved,"from":path,"specifier":record.specifier,"depth":depth + 1})
            dependency_paths.push(resolved)
            state = this.graph_state(state.root, dependencies, dependency_paths, state.diagnostics, state.diagnostic_keys, state.visited, state.visiting, state.files, state.aggregate, state.limits)
            if(!exists(resolved)) { state = this.add_graph_diagnostic(state, this.graph_diagnostic("missing_dependency", "imported dependency does not exist", resolved)); continue }
            if(is_dir(resolved)) { state = this.add_graph_diagnostic(state, this.graph_diagnostic("dependency_is_directory", "imported dependency is a directory", resolved)); continue }
            if(record.extension == ".mdx") { state = this.walk(resolved, depth + 1, root, state) }
            else { this.register(resolved) }
        }
        next_visiting := []
        for(active : state.visiting) { if(active != path) { next_visiting.push(active) } }
        visited := state.visited
        if(!visited.contains(path)) { visited.push(path) }
        return this.graph_state(state.root, state.dependencies, state.dependency_paths, state.diagnostics, state.diagnostic_keys, visited, next_visiting, state.files, state.aggregate, state.limits)
    }

    private fn(render_read(path)) {
        handle := file(path)
        handle.open("r")
        value := handle.read_val()
        handle.close()
        return value
    }

    private fn(render_write(path, value)) {
        handle := file(path)
        handle.open("w")
        handle.write_val(value)
        handle.save()
        handle.close()
    }

    private fn(render_options()) {
        path := ".nift/mdx-render.json"
        if(!exists(path)) { throw error("MDX rendering requires .nift/mdx-render.json with explicit trusted policy", "user.mdx_policy") }
        this.register(path)
        options := this.render_read(path)
        if(!options.keys().contains("policy") || options.policy != "trusted") { throw error("MDX rendering requires explicit trusted policy", "user.mdx_policy") }
        return options
    }

    fn(prepare(documents)) {
        options := this.render_options()
        if(type(documents) != "array" || documents.length() == 0) { throw error("prepare requires a non-empty document array", "user.mdx_request") }
        items := []
        index := 0
        for(document : documents) {
            if(!document.ok) { throw error("Cannot render a rejected MDX document", "user.mdx_parser") }
            id := "inline-" + index.to_string()
            if(document.path != null) { id = document.path }
            items.push({"id":id,"source":document.source,"path":document.path,"dependencies":document.dependencies,"frontmatter":document.frontmatter})
            index += 1
        }
        // Check process authority before creating request files; a denied run
        // is a fatal Nift capability error and cannot be caught for cleanup.
        backend := run("node", "--version")
        if(backend.exit_code != 0) { throw error("Node runtime is unavailable", "user.mdx_process") }
        make_dir(".nift/mdx-render")
        token := ""
        random := secure_random_bytes(16)
        index = 0
        while(index < 16) { token += random[index].to_string() + "-"; index += 1 }
        request_path := ".nift/mdx-render/" + token + "request.json"
        response_path := ".nift/mdx-render/" + token + "response.json"
        this.render_write(request_path, {"version":1,"documents":items,"options":options})
        helper := module_path() + "/../renderer/cli.mjs"
        response := run("node", helper, "--request", request_path, "--response", response_path)
        remove(request_path)
        if(!exists(response_path)) { throw error("MDX helper produced no response: " + response.stderr, "user.mdx_process") }
        result := this.render_read(response_path)
        remove(response_path)
        if(response.exit_code != 0 || !result.ok) { throw error(result.stringify(), "user.mdx_render") }
        return result
    }

    fn(html(document)) {
        if(document.keys().contains("kind") && document.kind == "mdx-prepared") { return document.html }
        options := this.render_options()
        if(!document.ok) { throw error("Cannot render a rejected MDX document", "user.mdx_parser") }
        if(document.path == null) {
            response := this.prepare([document])
            return response.results[0].html
        }
        normalized := this.project_path(document.path)
        if(normalized == null || normalized == "") { throw error("MDX document path escapes project", "user.mdx_path") }
        path := ".nift/mdx-html/" + normalized + ".json"
        if(!exists(path)) { throw error("Run mdx.prepare in a project pre-build script before mdx.html", "user.mdx_not_prepared") }
        this.register(path)
        result := this.render_read(path)
        if(result.source != document.source || result.options.stringify() != options.stringify()) { throw error("Prepared MDX source is stale; run mdx.prepare", "user.mdx_stale") }
        for(dependency : result.dependencies) { this.register(dependency) }
        return result.html
    }

    fn(prepared(path)) {
        options := this.render_options()
        normalized := this.project_path(path)
        if(normalized == null || normalized == "") { throw error("Prepared path escapes project", "user.mdx_path") }
        record_path := ".nift/mdx-prepared/" + normalized + ".json"
        if(!exists(record_path)) { throw error("Run renderer/prepare.mjs in the pre-build hook", "user.mdx_not_prepared") }
        record := this.render_read(record_path)
        this.register(normalized)
        this.register(record_path)
        for(dependency : record.dependencies) { this.register(dependency) }
        if(record.source != open(normalized) || record.options.stringify() != options.stringify()) { throw error("Prepared rendering is stale", "user.mdx_stale") }
        return {"kind":"mdx-prepared","html":record.html,"path":normalized}
    }

    fn(parse(source)) { return this.parse_core(source, null, null, this.limits()) }

    fn(input(path)) {
        if(type(path) != "string") { return this.empty_failure(null, null, this.default_frontmatter(), this.graph_diagnostic("invalid_path_type", "path must be a string", null)) }
        if(path == "" || path.contains(bytes([0]).decode("utf-8"))) { return this.empty_failure(null, null, this.default_frontmatter(), this.graph_diagnostic("invalid_path", "path must be non-empty and contain no NUL", null)) }
        if(path.encode("utf-8").length() > 4096) { return this.empty_failure(null, null, this.default_frontmatter(), this.graph_diagnostic("path_too_long", "path exceeds 4096 bytes", null)) }
        normalized := this.project_path(path)
        if(normalized == null || normalized == "") { return this.empty_failure(null, null, this.default_frontmatter(), this.graph_diagnostic("path_escape", "path escapes the project root", null)) }
        if(!exists(normalized)) { return this.empty_failure(null, normalized, this.default_frontmatter(), this.graph_diagnostic("missing_file", "input file does not exist", normalized)) }
        if(is_dir(normalized)) { return this.empty_failure(null, normalized, this.default_frontmatter(), this.graph_diagnostic("input_is_directory", "input path is a directory", normalized)) }
        state := this.graph_state(null, [], [], [], [], [], [], 0, 0, this.limits())
        state = this.walk(normalized, 0, normalized, state)
        if(state.root == null) { return this.empty_failure(null, normalized, this.default_frontmatter(), state.diagnostics[0]) }
        root_result := state.root
        return this.result(root_result.ok && state.diagnostics.length() == 0, root_result.source, root_result.path, root_result.frontmatter, root_result.imports, root_result.exports, root_result.nodes, state.dependencies, state.diagnostics)
    }
}

mdx := mdx()
export(mdx)
