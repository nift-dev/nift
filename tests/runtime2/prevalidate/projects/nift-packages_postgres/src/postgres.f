/*
    PostgreSQL database package for Nift. v0.1.0 backend: the psql executable.
    Public API: the exported `postgres` struct. Helpers stay private.

    Connection parameters are passed as separate psql argv options (-h/-p/-U/-d)
    rather than a single libpq conninfo string, so a value containing spaces can
    never inject additional libpq keywords.

    Parameter binding is structural rather than quote-escaping. String values
    are emitted as a hex bytea decoded to UTF-8
    (convert_from(decode('..','hex'),'UTF8')); numbers, booleans and null are
    emitted as validated scalar literals. This is independent of the server's
    standard_conforming_strings setting, and values containing quotes,
    backslashes, newlines and Unicode are preserved byte-exactly.

    $n placeholders are substituted only outside string literals (including
    dollar-quoted strings), quoted identifiers and comments. This remains
    textual substitution, not a server prepared statement: never assemble
    dynamic SQL from untrusted fragments, bind scalar values only.
*/

struct(postgres) {
    private fn(hex_encode(text)) {
        data := text.encode("utf-8")
        digits := "0123456789ABCDEF"
        length := data.size()
        output := ""
        i := 0
        while(i < length) {
            byte := data[i]
            output += digits.substr((byte / 16).floor().to_int(), 1) + digits.substr(byte % 16, 1)
            i += 1
        }
        return output
    }

    private fn(string_literal(text)) {
        if(text == "") { return "''" }
        return "convert_from(decode('" + this.hex_encode(text) + "','hex'),'UTF8')"
    }

    private fn(literal(value)) {
        t := type(value)
        if(t == "null") { return "NULL" }
        if(t == "bool") { if(value) { return "true" } return "false" }
        if(t == "int" || t == "float") { return value.to_string() }
        if(t == "string") { return this.string_literal(value) }
        return null
    }

    private fn(prepare(params)) {
        // All parameter encoding happens here, before the scan loop below runs.
        // A collection map (not a loop) evaluates each literal so string
        // encoding never executes with a loop on the call stack.
        return params.map(x => this.literal(x))
    }

    private fn(is_digit(c)) { return c >= "0" && c <= "9" }

    private fn(tag_start(c)) {
        return (c >= "a" && c <= "z") || (c >= "A" && c <= "Z") || c == "_"
    }

    private fn(tag_char(c)) { return this.tag_start(c) || this.is_digit(c) }

    private fn(dollar_opener(sql, i)) {
        // Recognise a PostgreSQL dollar-quote opener `$tag$` (empty tag for
        // `$$`) starting at i. Returns the tag, or null when this `$` does not
        // begin a dollar-quoted string.
        j := i + 1
        if(j >= sql.length()) { return null }
        first := sql.substr(j, 1)
        if(first == "$") { return "" }
        if(!this.tag_start(first)) { return null }
        tag := ""
        while(j < sql.length()) {
            c := sql.substr(j, 1)
            if(c == "$") { return tag }
            if(!this.tag_char(c)) { return null }
            tag += c
            j += 1
        }
        return null
    }

    private fn(bind(sql, params)) {
        literals := this.prepare(params)
        bound := ""
        i := 0
        len := sql.length()
        state := 0
        dollar_tag := ""
        while(i < len) {
            c := sql.substr(i, 1)
            n := ""
            if(i + 1 < len) { n = sql.substr(i + 1, 1) }
            if(state == 0) {
                if(c == "'") { bound += c; state = 1 }
                else if(c == "\"") { bound += c; state = 2 }
                else if(c == "-" && n == "-") { bound += "--"; i += 1; state = 3 }
                else if(c == "/" && n == "*") { bound += "/*"; i += 1; state = 4 }
                else if(c == "$") {
                    if(n != "" && this.is_digit(n)) {
                        j := i + 1
                        digits := ""
                        while(j < len) {
                            d := sql.substr(j, 1)
                            if(this.is_digit(d)) { digits += d; j += 1 }
                            else { break }
                        }
                        num := digits.to_int()
                        if(num >= 1 && num <= literals.size()) {
                            value := literals[num - 1]
                            if(value == null) { return null }
                            bound += value
                        } else {
                            bound += "$" + digits
                        }
                        i = j - 1
                    }
                    else {
                        tag := this.dollar_opener(sql, i)
                        if(tag != null) {
                            dollar_tag = tag
                            bound += "$" + tag + "$"
                            i = i + tag.length() + 1
                            state = 5
                        }
                        else { bound += "$" }
                    }
                }
                else { bound += c }
            }
            else if(state == 1) {
                bound += c
                if(c == "\\") { if(n != "") { bound += n; i += 1 } }
                else if(c == "'") {
                    if(n == "'") { bound += n; i += 1 }
                    else { state = 0 }
                }
            }
            else if(state == 2) {
                bound += c
                if(c == "\"") {
                    if(n == "\"") { bound += n; i += 1 }
                    else { state = 0 }
                }
            }
            else if(state == 3) { bound += c; if(c == "\n") { state = 0 } }
            else if(state == 4) {
                bound += c
                if(c == "*" && n == "/") { bound += n; i += 1; state = 0 }
            }
            else if(state == 5) {
                if(c == "$") {
                    closing := "$" + dollar_tag + "$"
                    if(sql.substr(i, closing.length()) == closing) {
                        bound += closing
                        i = i + closing.length() - 1
                        state = 0
                    }
                    else { bound += c }
                }
                else { bound += c }
            }
            i += 1
        }
        return bound
    }

    private fn(process_available()) {
        return getenv("NIFT_NO_PROCESS") == null && which("psql") != null
    }

    private fn(unavailable()) {
        if(getenv("NIFT_NO_PROCESS") != null) {
            return {"ok":false,"rows":[],"columns":[],"error":"postgres process execution is disabled by --no-process","error_code":"backend_unavailable","exit_code":127}
        }
        return {"ok":false,"rows":[],"columns":[],"error":"psql executable not found","error_code":"backend_unavailable","exit_code":127}
    }

    private fn(cli_exec(db, sql, params)) {
        bound := this.bind(sql, params)
        if(bound == null) { return {"ok":false,"rows":[],"columns":[],"error":"unsupported parameter type","error_code":"invalid_parameters","exit_code":2} }
        result := run("psql", db.conn, "-q", "-c", bound)
        return {"ok":result.exit_code == 0,"rows":[],"columns":[],"error":result.stderr,"error_code":"","exit_code":result.exit_code}
    }

    private fn(parse_rows(text)) {
        columns := []
        rows := []
        lines := text.split("\n")
        if(lines.size() == 0) { return {"columns": columns, "rows": rows} }
        header := lines[0]
        columns = header.split("\t")
        i := 1
        while(i < lines.size()) {
            line := lines[i]
            if(line != "") {
                cells := line.split("\t")
                entries := []
                j := 0
                while(j < columns.size()) {
                    val := ""
                    if(j < cells.size()) { val = cells[j] }
                    entries.push({"key": columns[j], "value": val})
                    j++
                }
                rows.push(entries.from_entries())
            }
            i++
        }
        return {"columns": columns, "rows": rows}
    }

    private fn(cli_query(db, sql, params)) {
        bound := this.bind(sql, params)
        if(bound == null) { return {"ok":false,"rows":[],"columns":[],"error":"unsupported parameter type","error_code":"invalid_parameters","exit_code":2} }
        result := run("psql", db.conn, "-q", "-A", "-F", "\t", "-c", bound)
        if(result.exit_code != 0) { return {"ok":false,"rows":[],"columns":[],"error":result.stderr,"error_code":"","exit_code":result.exit_code} }
        parsed := this.parse_rows(result.stdout)
        return {"ok":true,"rows":parsed.rows,"columns":parsed.columns,"error":"","error_code":"","exit_code":0}
    }

    fn(available()) { return this.process_available() }

    fn(version()) {
        if(!this.process_available()) { return "" }
        r := run("psql", "--version")
        if(r.exit_code != 0) { return "" }
        return r.stdout.trim()
    }

    fn(open(desc)) {
        if(desc == null) { return {"conn":[]} }
        host := desc.get("host", "localhost")
        port := desc.get("port", 5432)
        database := desc.get("database", "")
        user := desc.get("user", "")
        args := []
        if(host != "") { args.push("-h"); args.push(host) }
        if(port != "") { args.push("-p"); args.push(port.to_string()) }
        if(user != "") { args.push("-U"); args.push(user) }
        if(database != "") { args.push("-d"); args.push(database) }
        return {"conn":args}
    }

    fn(exec(db, sql, ...params)) {
        if(!this.process_available()) { return this.unavailable() }
        return this.cli_exec(db, sql, params)
    }

    fn(query(db, sql, ...params)) {
        if(!this.process_available()) { return this.unavailable() }
        return this.cli_query(db, sql, params)
    }

    fn(transaction(db, statements)) {
        if(!this.process_available()) { return {"ok":false,"error":this.unavailable().error,"error_code":"backend_unavailable","exit_code":127} }
        sql := "BEGIN;"
        for(statement : statements) { sql += statement + ";" }
        sql += "COMMIT;"
        result := run("psql", db.conn, "-q", "-v", "ON_ERROR_STOP=1", "-c", sql)
        if(result.exit_code != 0) { return {"ok":false,"error":result.stderr,"error_code":"","exit_code":result.exit_code} }
        return {"ok":true,"error":"","error_code":"","exit_code":0}
    }
}

postgres := postgres()
export(postgres)
