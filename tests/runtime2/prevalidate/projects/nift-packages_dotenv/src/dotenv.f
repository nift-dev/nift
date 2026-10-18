/*
    Deterministic dotenv parsing with strict UTF-8 and no interpolation.
    Public API: the exported `dotenv` facade. All helpers are private.

    The input bytes are converted once into a plain byte-integer array and the
    parser reads elements inline (never through a per-byte helper call), which
    keeps parsing effectively linear in the input size.
*/

struct(dotenv) {
    private fn(fail()) {
        invalid_value := null
        return invalid_value.length()
    }

    private fn(to_ints(bytes_value)) {
        ints := []
        i := 0
        while(i < bytes_value.size()) { ints.push(bytes_value[i]); i += 1 }
        return ints
    }

    private fn(space(byte)) {
        return byte == 32 || byte == 9
    }

    private fn(key_start(byte)) {
        return (byte >= 65 && byte <= 90) || (byte >= 97 && byte <= 122) || byte == 95
    }

    private fn(key_byte(byte)) {
        return this.key_start(byte) || (byte >= 48 && byte <= 57)
    }

    private fn(diagnostic(code, message, line, column, severity)) {
        return {"code":code,"message":message,"line":line,"column":column,"severity":severity}
    }

    private fn(result(ok, values, entries, diagnostics)) {
        return {"ok":ok,"values":values,"entries":entries,"diagnostics":diagnostics}
    }

    private fn(line_result(entry, diagnostic)) {
        return {"entry":entry,"diagnostic":diagnostic}
    }

    private fn(too_many_lines(data)) {
        if(data.size() == 0) { return false }
        lines := 1
        i := 0
        while(i < data.size()) {
            current := data[i]
            if(current == 13) {
                if(i + 1 < data.size() && data[i + 1] == 10) { i += 1 }
                if(i + 1 < data.size()) { lines += 1 }
            }
            else if(current == 10 && i + 1 < data.size()) { lines += 1 }
            if(lines > 1024) { return true }
            i += 1
        }
        return false
    }

    private fn(utf8_error(data)) {
        i := 0
        while(i < data.size()) {
            first := data[i]
            if(first <= 127) { i += 1; continue }

            count := 0
            second_min := 128
            second_max := 191
            if(first >= 194 && first <= 223) { count = 2 }
            else if(first == 224) { count = 3; second_min = 160 }
            else if(first >= 225 && first <= 236) { count = 3 }
            else if(first == 237) { count = 3; second_max = 159 }
            else if(first >= 238 && first <= 239) { count = 3 }
            else if(first == 240) { count = 4; second_min = 144 }
            else if(first >= 241 && first <= 243) { count = 4 }
            else if(first == 244) { count = 4; second_max = 143 }
            else { return i }

            if(i + count > data.size()) { return i }
            second := data[i + 1]
            if(second < second_min || second > second_max) { return i + 1 }
            j := 2
            while(j < count) {
                continuation := data[i + j]
                if(continuation < 128 || continuation > 191) { return i + j }
                j += 1
            }
            i += count
        }
        return -1
    }

    private fn(location(data, offset)) {
        line := 1
        column := 1
        i := 0
        while(i < offset) {
            current := data[i]
            if(current == 13) {
                if(i + 1 < offset && data[i + 1] == 10) { i += 1 }
                line += 1
                column = 1
            }
            else if(current == 10) {
                line += 1
                column = 1
            }
            else { column += 1 }
            i += 1
        }
        return {"line":line,"column":column}
    }

    private fn(text(data, begin, end)) {
        return bytes(data.slice(begin, end)).decode("utf-8")
    }

    private fn(export_prefix(data, cursor, end)) {
        if(end - cursor < 7) { return false }
        return data[cursor] == 101 && data[cursor + 1] == 120 && data[cursor + 2] == 112 && data[cursor + 3] == 111 && data[cursor + 4] == 114 && data[cursor + 5] == 116 && this.space(data[cursor + 6])
    }

    private fn(parse_line(data, begin, end, line)) {
        scan := begin
        while(scan < end) {
            if(data[scan] == 0) {
                diagnostic := this.diagnostic("nul_byte", "NUL byte is not allowed", line, scan - begin + 1, "error")
                return this.line_result(null, diagnostic)
            }
            scan += 1
        }

        cursor := begin
        while(cursor < end && this.space(data[cursor])) { cursor += 1 }
        if(cursor == end || data[cursor] == 35) { return this.line_result(null, null) }

        if(this.export_prefix(data, cursor, end)) {
            cursor += 6
            while(cursor < end && this.space(data[cursor])) { cursor += 1 }
        }

        if(cursor == end || !this.key_start(data[cursor])) {
            diagnostic := this.diagnostic("invalid_key_start", "variable name must start with an ASCII letter or underscore", line, cursor - begin + 1, "error")
            return this.line_result(null, diagnostic)
        }
        key_begin := cursor
        while(cursor < end && this.key_byte(data[cursor])) { cursor += 1 }
        key_end := cursor
        if(cursor < end && !this.space(data[cursor]) && data[cursor] != 61) {
            diagnostic := this.diagnostic("invalid_key_character", "variable name contains an invalid character", line, cursor - begin + 1, "error")
            return this.line_result(null, diagnostic)
        }
        while(cursor < end && this.space(data[cursor])) { cursor += 1 }
        if(cursor == end || data[cursor] != 61) {
            diagnostic := this.diagnostic("missing_equals", "expected '=' after variable name", line, cursor - begin + 1, "error")
            return this.line_result(null, diagnostic)
        }
        cursor += 1
        while(cursor < end && this.space(data[cursor])) { cursor += 1 }

        value := ""
        quoted := false
        if(cursor < end && data[cursor] == 39) {
            quoted = true
            quote_column := cursor - begin + 1
            cursor += 1
            value_begin := cursor
            while(cursor < end && data[cursor] != 39) { cursor += 1 }
            if(cursor == end) {
                diagnostic := this.diagnostic("unterminated_single_quote", "unterminated single-quoted value", line, quote_column, "error")
                return this.line_result(null, diagnostic)
            }
            else {
                value = this.text(data, value_begin, cursor)
                cursor += 1
            }
        }
        else if(cursor < end && data[cursor] == 34) {
            quoted = true
            quote_column := cursor - begin + 1
            cursor += 1
            decoded := []
            closed := false
            while(cursor < end) {
                current := data[cursor]
                if(current == 34) {
                    closed = true
                    cursor += 1
                    break
                }
                if(current == 92) {
                    escape_column := cursor - begin + 1
                    cursor += 1
                    if(cursor == end) {
                        diagnostic := this.diagnostic("invalid_escape", "incomplete escape in double-quoted value", line, escape_column, "error")
                        return this.line_result(null, diagnostic)
                    }
                    escaped := data[cursor]
                    if(escaped == 110) { decoded.push(10) }
                    else if(escaped == 114) { decoded.push(13) }
                    else if(escaped == 116) { decoded.push(9) }
                    else if(escaped == 34) { decoded.push(34) }
                    else if(escaped == 92) { decoded.push(92) }
                    else {
                        diagnostic := this.diagnostic("invalid_escape", "unsupported escape in double-quoted value", line, escape_column, "error")
                        return this.line_result(null, diagnostic)
                    }
                    cursor += 1
                    continue
                }
                decoded.push(current)
                cursor += 1
            }
            if(!closed) {
                diagnostic := this.diagnostic("unterminated_double_quote", "unterminated double-quoted value", line, quote_column, "error")
                return this.line_result(null, diagnostic)
            }
            value = bytes(decoded).decode("utf-8")
        }
        else {
            value_begin := cursor
            value_end := end
            while(cursor < end) {
                if(data[cursor] == 35 && (cursor == value_begin || data[cursor - 1] == 32 || data[cursor - 1] == 9)) {
                    value_end = cursor
                    break
                }
                cursor += 1
            }
            while(value_end > value_begin && this.space(data[value_end - 1])) { value_end -= 1 }
            value = this.text(data, value_begin, value_end)
        }

        if(quoted && cursor < end) {
            had_space := false
            while(cursor < end && this.space(data[cursor])) { had_space = true; cursor += 1 }
            if(cursor < end && (data[cursor] != 35 || !had_space)) {
                diagnostic := this.diagnostic("trailing_content", "unexpected content after quoted value", line, cursor - begin + 1, "error")
                return this.line_result(null, diagnostic)
            }
        }

        key := this.text(data, key_begin, key_end)
        entry := {"key":key,"value":value,"line":line,"column":key_begin - begin + 1}
        return this.line_result(entry, null)
    }

    private fn(parse_core(source)) {
        if(!is_bytes(source)) { return this.fail() }
        if(source.length() > 24576) {
            diagnostic := this.diagnostic("input_too_large", "input exceeds 24576-byte limit", 1, 1, "error")
            return this.result(false, {}, [], [diagnostic])
        }
        data := this.to_ints(source)
        if(this.too_many_lines(data)) {
            diagnostic := this.diagnostic("input_too_large", "input exceeds 1024-logical-line limit", 1, 1, "error")
            return this.result(false, {}, [], [diagnostic])
        }
        invalid := this.utf8_error(data)
        if(invalid >= 0) {
            where := this.location(data, invalid)
            diagnostic := this.diagnostic("invalid_utf8", "input is not valid UTF-8", where.line, where.column, "error")
            return {"ok":false,"values":{},"entries":[],"diagnostics":[diagnostic]}
        }

        ok := true
        values := {}
        entries := []
        diagnostics := []
        offset := 0
        line := 1
        while(offset < data.size()) {
            line_end := offset
            while(line_end < data.size() && data[line_end] != 10 && data[line_end] != 13) { line_end += 1 }
            parsed := this.parse_line(data, offset, line_end, line)
            if(parsed.diagnostic != null) {
                ok = false
                diagnostics.push(parsed.diagnostic)
            }
            if(parsed.entry != null) {
                parsed_entry := parsed.entry
                key := parsed_entry.key
                value := parsed_entry.value
                entry := parsed_entry.omit(["column"])
                if(values.has(key)) {
                    diagnostic := this.diagnostic("duplicate_key", "duplicate variable; last value wins", line, parsed_entry.column, "warning")
                    diagnostics.push(diagnostic)
                }
                values[key] = value
                entries.push(entry)
            }
            if(line_end < data.size() && data[line_end] == 13 && line_end + 1 < data.size() && data[line_end + 1] == 10) {
                offset = line_end + 2
            }
            else if(line_end < data.size()) { offset = line_end + 1 }
            else { offset = data.size() }
            line += 1
        }
        return this.result(ok, values, entries, diagnostics)
    }

    fn(parse(source)) {
        if(type(source) != "string") { return this.fail() }
        if(source.length() > 24576) {
            diagnostic := this.diagnostic("input_too_large", "input exceeds 24576-byte limit", 1, 1, "error")
            return this.result(false, {}, [], [diagnostic])
        }
        return this.parse_core(source.encode("utf-8"))
    }

    fn(parse_bytes(bytes)) {
        return this.parse_core(bytes)
    }

    fn(read(path)) {
        if(type(path) != "string") { return this.fail() }
        return this.parse_core(open_bytes(path))
    }
}

dotenv := dotenv()
export(dotenv)