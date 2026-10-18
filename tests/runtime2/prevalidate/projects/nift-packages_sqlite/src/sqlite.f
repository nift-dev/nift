/*
    SQLite database package for Nift. v0.1.0 backend: sqlite3 CLI.

    Public API: the exported `sqlite` struct. All implementation helpers are
    private to this module.
*/

sqlite_backend_requested := "auto"
sqlite_backend_locked := false
sqlite_backend_selected := ""
sqlite_query_seq := 0
sqlite_next_handle_id := 0
sqlite_handle_open := map()
sqlite_handle_backend := map()
sqlite_handle_path := map()
sqlite_nul := bytes([0]).decode("utf-8")

struct(sqlite) {
    private fn(process_available()) {
        return getenv("NIFT_NO_PROCESS") == null && which("sqlite3") != null
    }

    fn(available()) { return this.process_available() }

    fn(backends()) {
        if(this.process_available()) { return ["process"] }
        return []
    }

    fn(backend()) {
        if(sqlite_backend_locked) {
            if(sqlite_backend_selected != "") { return sqlite_backend_selected }
            return null
        }
        if(sqlite_backend_requested == "process") {
            if(this.process_available()) { return "process" }
            return null
        }
        if(sqlite_backend_requested == "auto" && this.process_available()) { return "process" }
        return null
    }

    fn(use_backend(backend_name)) {
        if(sqlite_backend_locked) {
            return {"ok":false,"error":"sqlite backend is already selected","error_code":"backend_locked","backend":this.backend()}
        }
        if(backend_name != "auto" && backend_name != "process" && backend_name != "ffi" && backend_name != "native") {
            return {"ok":false,"error":"unknown sqlite backend: " + backend_name,"error_code":"unknown_backend","backend":this.backend()}
        }
        if(backend_name != "auto" && backend_name != "process") {
            return {"ok":false,"error":"sqlite backend is not implemented: " + backend_name,"error_code":"backend_unavailable","backend":this.backend()}
        }
        if(backend_name == "process" && !this.process_available()) {
            return {"ok":false,"error":"sqlite process backend is unavailable","error_code":"backend_unavailable","backend":this.backend()}
        }
        sqlite_backend_requested = backend_name
        return {"ok":true,"error":"","error_code":"","backend":this.backend()}
    }

    fn(capabilities()) {
        return {"persistent_connections":false,"prepared_statements":false,"blobs":false,"cancellation":false,"transaction_batches":true}
    }

    private fn(is_digit(c)) { return c >= "0" && c <= "9" }

    private fn(has_nul(value)) {
        return type(value) == "string" && value.contains(sqlite_nul)
    }

    private fn(literal(value)) {
        t := type(value)
        if(t == "null") { return "NULL" }
        if(t == "bool") { if(value) { return "1" } return "0" }
        if(t == "int" || t == "float") { return value.to_string() }
        if(t == "string") {
            if(this.has_nul(value)) { return null }
            return "'" + value.replace("'", "''") + "'"
        }
        return null
    }

    private fn(bind(sql, params)) {
        bound := ""
        i := 0
        len := sql.length()
        state := 0
        p := 0
        while(i < len) {
            c := sql.substr(i, 1)
            n := ""
            if(i + 1 < len) { n = sql.substr(i + 1, 1) }
            if(state == 0) {
                if(c == "'") { bound += c; state = 1 }
                else if(c == "\"") { bound += c; state = 2 }
                else if(c == "`") { bound += c; state = 5 }
                else if(c == "[") { bound += c; state = 6 }
                else if(c == "-" && n == "-") { bound += "--"; i += 1; state = 3 }
                else if(c == "/" && n == "*") { bound += "/*"; i += 1; state = 4 }
                else if(c == "?") {
                    if(n != "" && this.is_digit(n)) { return null }
                    p += 1
                    if(p <= params.size()) {
                        literal := this.literal(params[p - 1])
                        if(literal == null) { return null }
                        bound += literal
                    } else {
                        bound += "?"
                    }
                }
                else { bound += c }
            }
            else if(state == 1) {
                bound += c
                if(c == "'") {
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
            else if(state == 5) {
                bound += c
                if(c == "`") {
                    if(n == "`") { bound += n; i += 1 }
                    else { state = 0 }
                }
            }
            else if(state == 6) { bound += c; if(c == "]") { state = 0 } }
            else if(state == 3) { bound += c; if(c == "\n") { state = 0 } }
            else if(state == 4) {
                bound += c
                if(c == "*" && n == "/") { bound += n; i += 1; state = 0 }
            }
            i += 1
        }
        if(p != params.size()) { return null }
        return bound
    }

    private fn(run_sql(db, sql)) {
        result := run("sqlite3", "-bail", db.path, sql)
        if(result.exit_code != 0) { return {"ok":false,"error":result.stderr,"error_code":"database_error","backend":"process","stdout":result.stdout,"exit_code":result.exit_code} }
        return {"ok":true,"error":"","error_code":"","backend":"process","stdout":result.stdout,"exit_code":result.exit_code}
    }

    private fn(cli_exec(db, sql, params)) {
        bound := this.bind(sql, params)
        if(bound == null) { return {"ok":false,"error":"parameter count/type mismatch","error_code":"invalid_parameters","backend":"process","exit_code":2} }
        return this.run_sql(db, bound)
    }

    private fn(temp_root()) {
        root := getenv("TMPDIR")
        if(root == null || root == "") { root = getenv("TEMP") }
        if(root == null || root == "") { root = getenv("TMP") }
        if(root == null || root == "") { root = pwd() }
        return root
    }

    private fn(temp_file()) {
        if(which("mktemp") != null) {
            m := run("mktemp", "--suffix=.json")
            if(m.exit_code == 0 && m.stdout.trim() != "") { return m.stdout.trim() }
            m = run("mktemp")
            if(m.exit_code == 0 && m.stdout.trim() != "") { return m.stdout.trim() }
        }
        if(os() == "windows" && which("powershell.exe") != null) {
            p := run("powershell.exe", "-NoProfile", "-NonInteractive", "-Command", "[System.IO.Path]::GetTempFileName()")
            if(p.exit_code == 0 && p.stdout.trim() != "") { return p.stdout.trim() }
        }
        attempts := 0
        while(attempts < 1000) {
            sqlite_query_seq += 1
            candidate := this.temp_root() + "/.nift-sqlite-" + sqlite_query_seq.to_string() + ".json"
            if(!exists(candidate)) {
                touch(candidate)
                return candidate
            }
            attempts += 1
        }
        return ""
    }

    private fn(cli_query(db, sql, params)) {
        bound := this.bind(sql, params)
        if(bound == null) { return {"ok":false,"rows":[],"error":"parameter count/type mismatch","error_code":"invalid_parameters","backend":"process","exit_code":2} }
        temp := this.temp_file()
        if(temp == "") { return {"ok":false,"rows":[],"error":"cannot create query temporary file","error_code":"temporary_file","backend":"process","exit_code":null} }
        result := cmd("sqlite3", "-json", db.path, bound).stdout(temp).run()
        if(result.exit_code != 0) { remove(temp); return {"ok":false,"rows":[],"error":result.stderr,"error_code":"database_error","backend":"process","exit_code":result.exit_code} }
        if(open(temp).trim().empty()) { remove(temp); return {"ok":true,"rows":[],"error":"","error_code":"","backend":"process","exit_code":0} }
        rows := inject(temp)
        remove(temp)
        return {"ok":true,"rows":rows,"error":"","error_code":"","backend":"process","exit_code":0}
    }

    private fn(cli_transaction(db, statements)) {
        sql := "BEGIN;"
        for(statement : statements) {
            if(type(statement) == "string") { sql += statement + ";" }
            else if(type(statement) == "object" && statement.has("sql")) {
                params := statement.get("params", [])
                if(type(params) != "array") { return {"ok":false,"error":"transaction params must be an array","error_code":"invalid_parameters","backend":"process","exit_code":2} }
                bound := this.bind(statement.get("sql"), params)
                if(bound == null) { return {"ok":false,"error":"parameter count/type mismatch","error_code":"invalid_parameters","backend":"process","exit_code":2} }
                sql += bound + ";"
            }
            else { return {"ok":false,"error":"transaction statements must be strings or {sql, params} objects","error_code":"invalid_parameters","backend":"process","exit_code":2} }
        }
        sql += "COMMIT;"
        return this.run_sql(db, sql)
    }

    private fn(safe_db_path(db)) {
        if(type(db.path) != "string" || db.path == "") { return false }
        if(db.path.substr(0, 1) == "-") { return false }
        return true
    }

    private fn(invalid_path_result(rows)) {
        if(rows) { return {"ok":false,"rows":[],"error":"sqlite database path must be non-empty and not start with '-'","error_code":"invalid_input","backend":null,"exit_code":null} }
        return {"ok":false,"error":"sqlite database path must be non-empty and not start with '-'","error_code":"invalid_input","backend":null,"exit_code":null}
    }

    private fn(handle_backend_for(handle_id)) {
        if(!sqlite_handle_backend.contains(handle_id)) { return null }
        value := sqlite_handle_backend.get(handle_id)
        if(value == "") { return null }
        return value
    }

    private fn(handle_is_open_for(handle_id)) {
        return sqlite_handle_open.contains(handle_id) && sqlite_handle_open.get(handle_id)
    }

    private fn(is_handle(db)) {
        if(type(db) != "object") { return false }
        if(!db.has("kind") || !db.has("path") || !db.has("_handle_id")) { return false }
        if(db.kind != "sqlite_database") { return false }
        if(!sqlite_handle_open.contains(db._handle_id)) { return false }
        return sqlite_handle_path.get(db._handle_id) == db.path
    }

    private fn(invalid_handle_result(rows)) {
        if(rows) { return {"ok":false,"rows":[],"error":"invalid sqlite database handle","error_code":"invalid_handle","backend":null,"exit_code":null} }
        return {"ok":false,"error":"invalid sqlite database handle","error_code":"invalid_handle","backend":null,"exit_code":null}
    }

    private fn(closed_handle_result(db, rows)) {
        backend := this.handle_backend_for(db._handle_id)
        if(rows) { return {"ok":false,"rows":[],"error":"sqlite database is closed","error_code":"closed_handle","backend":backend,"exit_code":null} }
        return {"ok":false,"error":"sqlite database is closed","error_code":"closed_handle","backend":backend,"exit_code":null}
    }

    private fn(unavailable_result(db, rows)) {
        backend := this.handle_backend_for(db._handle_id)
        if(rows) { return {"ok":false,"rows":[],"error":"sqlite process backend is unavailable","error_code":"backend_unavailable","backend":backend,"exit_code":127} }
        return {"ok":false,"error":"sqlite process backend is unavailable","error_code":"backend_unavailable","backend":backend,"exit_code":127}
    }

    fn(open(path)) {
        backend := this.backend()
        if(backend != null) { sqlite_backend_selected = backend }
        sqlite_backend_locked = true
        sqlite_next_handle_id += 1
        id := sqlite_next_handle_id
        sqlite_handle_open.set(id, true)
        sqlite_handle_path.set(id, path)
        if(backend == null) { sqlite_handle_backend.set(id, "") }
        else { sqlite_handle_backend.set(id, backend) }
        return {"kind":"sqlite_database","path":path,"_handle_id":id}
    }

    fn(close(db)) {
        if(!this.is_handle(db)) { return this.invalid_handle_result(false) }
        backend := this.handle_backend_for(db._handle_id)
        if(!sqlite_handle_open.get(db._handle_id)) {
            return {"ok":false,"error":"sqlite database is already closed","error_code":"already_closed","backend":backend}
        }
        sqlite_handle_open.set(db._handle_id, false)
        return {"ok":true,"error":"","error_code":"","backend":backend}
    }

    fn(handle_backend(db)) {
        if(!this.is_handle(db)) { return null }
        return this.handle_backend_for(db._handle_id)
    }

    fn(is_open(db)) {
        if(!this.is_handle(db)) { return false }
        return this.handle_is_open_for(db._handle_id)
    }

    fn(same_handle(first_handle, second_handle)) {
        if(!this.is_handle(first_handle) || !this.is_handle(second_handle)) { return false }
        return first_handle._handle_id == second_handle._handle_id
    }

    fn(exec(db, sql, ...params)) {
        if(!this.is_handle(db)) { return this.invalid_handle_result(false) }
        if(!this.safe_db_path(db)) { return this.invalid_path_result(false) }
        if(!this.handle_is_open_for(db._handle_id)) { return this.closed_handle_result(db, false) }
        if(this.handle_backend_for(db._handle_id) != "process" || !this.process_available()) { return this.unavailable_result(db, false) }
        return this.cli_exec(db, sql, params)
    }

    fn(query(db, sql, ...params)) {
        if(!this.is_handle(db)) { return this.invalid_handle_result(true) }
        if(!this.safe_db_path(db)) { return this.invalid_path_result(true) }
        if(!this.handle_is_open_for(db._handle_id)) { return this.closed_handle_result(db, true) }
        if(this.handle_backend_for(db._handle_id) != "process" || !this.process_available()) { return this.unavailable_result(db, true) }
        return this.cli_query(db, sql, params)
    }

    fn(transaction(db, statements)) {
        if(!this.is_handle(db)) { return this.invalid_handle_result(false) }
        if(!this.safe_db_path(db)) { return this.invalid_path_result(false) }
        if(!this.handle_is_open_for(db._handle_id)) { return this.closed_handle_result(db, false) }
        if(this.handle_backend_for(db._handle_id) != "process" || !this.process_available()) { return this.unavailable_result(db, false) }
        return this.cli_transaction(db, statements)
    }
}

sqlite := sqlite()
export(sqlite)
