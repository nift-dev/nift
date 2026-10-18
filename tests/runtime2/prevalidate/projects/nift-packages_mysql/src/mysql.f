/*
    MySQL database package for Nift. v0.1.0 backend: the mysql executable.
    Public API: the exported `mysql` struct. Helpers stay private.

    Parameter binding is structural rather than quote-escaping. String values
    are emitted as hexadecimal literals converted to utf8mb4
    (CONVERT(X'..' USING utf8mb4)); numbers, booleans and null are emitted as
    validated scalar literals. Parameter data therefore never enters a context
    where quote or backslash escaping can be broken, and values containing
    quotes, backslashes, NUL, newlines or Unicode are preserved byte-exactly.

    $n placeholders are substituted only outside string literals, quoted
    identifiers and comments. This remains textual substitution, not a server
    prepared statement: never assemble dynamic SQL from untrusted fragments,
    bind scalar values only.
*/

struct(mysql) {
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
        return "CONVERT(X'" + this.hex_encode(text) + "' USING utf8mb4)"
    }

    private fn(literal(value)) {
        t := type(value)
        if(t == "null") { return "NULL" }
        if(t == "bool") { if(value) { return "TRUE" } return "FALSE" }
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

    private fn(bind(sql, params)) {
        literals := this.prepare(params)
        bound := ""
        i := 0
        len := sql.length()
        state := 0
        while(i < len) {
            c := sql.substr(i, 1)
            n := ""
            if(i + 1 < len) { n = sql.substr(i + 1, 1) }
            if(state == 0) {
                if(c == "'") { bound += c; state = 1 }
                else if(c == "\"") { bound += c; state = 2 }
                else if(c == "`") { bound += c; state = 5 }
                else if(c == "-" && n == "-") { bound += "--"; i += 1; state = 3 }
                else if(c == "#") { bound += c; state = 6 }
                else if(c == "/" && n == "*") { bound += "/*"; i += 1; state = 4 }
                else if(c == "$") {
                    j := i + 1
                    digits := ""
                    while(j < len) {
                        d := sql.substr(j, 1)
                        if(d == "0" || d == "1" || d == "2" || d == "3" || d == "4" || d == "5" || d == "6" || d == "7" || d == "8" || d == "9") { digits += d; j += 1 }
                        else { break }
                    }
                    if(digits == "") { bound += "$" }
                    else {
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
                if(c == "\\") { if(n != "") { bound += n; i += 1 } }
                else if(c == "\"") {
                    if(n == "\"") { bound += n; i += 1 }
                    else { state = 0 }
                }
            }
            else if(state == 5) {
                bound += c
                if(c == "`") {
                    if(n == "`") { bound += n; i += 1 }
                    else { state = 0 }
                }
            }
            else if(state == 3) { bound += c; if(c == "\n") { state = 0 } }
            else if(state == 6) { bound += c; if(c == "\n") { state = 0 } }
            else if(state == 4) {
                bound += c
                if(c == "*" && n == "/") { bound += n; i += 1; state = 0 }
            }
            i += 1
        }
        return bound
    }

    private fn(process_available()) {
        return getenv("NIFT_NO_PROCESS") == null && which("mysql") != null
    }

    private fn(unavailable()) {
        if(getenv("NIFT_NO_PROCESS") != null) {
            return {"ok":false,"rows":[],"columns":[],"error":"mysql process execution is disabled by --no-process","error_code":"backend_unavailable","exit_code":127}
        }
        return {"ok":false,"rows":[],"columns":[],"error":"mysql executable not found","error_code":"backend_unavailable","exit_code":127}
    }

    private fn(cli_exec(db, sql, params)) {
        bound := this.bind(sql, params)
        if(bound == null) { return {"ok":false,"rows":[],"columns":[],"error":"unsupported parameter type","error_code":"invalid_parameters","exit_code":2} }
        args := db.conn
        result := run("mysql", args, "--batch", "--skip-column-names", "--execute", bound)
        return {"ok":result.exit_code == 0,"rows":[],"columns":[],"error":result.stderr,"error_code":"","exit_code":result.exit_code}
    }

    private fn(parse_rows(text)) {
        lines := text.split("\n")
        rows := []
        i := 0
        while(i < lines.size()) {
            line := lines[i]
            if(line != "") { rows.push(line.split("\t")) }
            i++
        }
        return rows
    }

    private fn(cli_query(db, sql, params)) {
        bound := this.bind(sql, params)
        if(bound == null) { return {"ok":false,"rows":[],"columns":[],"error":"unsupported parameter type","error_code":"invalid_parameters","exit_code":2} }
        args := db.conn
        header := run("mysql", args, "--batch", "--column-names", "--execute", bound)
        if(header.exit_code != 0) { return {"ok":false,"rows":[],"columns":[],"error":header.stderr,"error_code":"","exit_code":header.exit_code} }
        data := run("mysql", args, "--batch", "--skip-column-names", "--execute", bound)
        columns := header.stdout.split("\n")[0].split("\t")
        rows := this.parse_rows(data.stdout)
        return {"ok":true,"rows":rows,"columns":columns,"error":"","error_code":"","exit_code":0}
    }

    fn(available()) { return this.process_available() }

    fn(version()) {
        if(!this.process_available()) { return "" }
        r := run("mysql", "--version")
        if(r.exit_code != 0) { return "" }
        return r.stdout.trim()
    }

    fn(open(desc)) {
        args := []
        if(desc == null) { return {"conn": args} }
        host := desc.get("host", "127.0.0.1")
        port := desc.get("port", 3306)
        user := desc.get("user", "")
        database := desc.get("database", "")
        args.push("--host=" + host)
        args.push("--port=" + port.to_string())
        if(user != "") { args.push("--user=" + user) }
        if(database != "") { args.push("--database=" + database) }
        return {"conn": args}
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
        args := db.conn
        result := run("mysql", args, "--batch", "--skip-column-names", "--execute", sql)
        if(result.exit_code != 0) { return {"ok":false,"error":result.stderr,"error_code":"","exit_code":result.exit_code} }
        return {"ok":true,"error":"","error_code":"","exit_code":0}
    }
}

mysql := mysql()
export(mysql)
