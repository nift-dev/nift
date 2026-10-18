/*
    Redis database package for Nift. v0.1.0 backend: the redis-cli executable.
    Public API: the exported `redis` struct. Helpers stay private.
    redis-cli --json gives machine-readable replies; a temp file bridges the
    JSON output into Nift values.

    Command arguments are passed as independent argv elements (never a shell).
    Because redis-cli does not accept a `--` option terminator, the only
    position that can be mistaken for an option is the command name itself
    (trailing keys/values follow the command token and are treated as data), so
    a command name starting with '-' is rejected.
*/

redis_temp_seq := 0
redis_nul := bytes([0]).decode("utf-8")

struct(redis) {
    private fn(process_available()) {
        return getenv("NIFT_NO_PROCESS") == null && which("redis-cli") != null
    }

    fn(available()) { return this.process_available() }

    fn(version()) {
        if(!this.process_available()) { return "" }
        r := run("redis-cli", "--version")
        if(r.exit_code != 0) { return "" }
        return r.stdout.trim()
    }

    private fn(failure(message, code, exit_code)) {
        return {"ok":false,"data":null,"error":message,"error_code":code,"exit_code":exit_code}
    }

    private fn(is_scalar(value)) {
        t := type(value)
        return t == "string" || t == "int" || t == "float" || t == "bool"
    }

    private fn(has_nul(value)) {
        return type(value) == "string" && value.contains(redis_nul)
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
            redis_temp_seq += 1
            candidate := this.temp_root() + "/.nift-redis-" + redis_temp_seq.to_string() + ".json"
            if(!exists(candidate)) {
                touch(candidate)
                return candidate
            }
            attempts += 1
        }
        return ""
    }

    private fn(cli_json(args)) {
        if(!this.process_available()) {
            return this.failure("redis process backend is unavailable", "backend_unavailable", 127)
        }
        for(arg : args) {
            if(!this.is_scalar(arg) || this.has_nul(arg)) {
                return this.failure("redis arguments must be scalar values without NUL", "invalid_input", 2)
            }
        }
        temp := this.temp_file()
        if(temp == "") { return this.failure("cannot create redis temporary file", "temporary_file", null) }
        result := cmd("redis-cli", "--json", ...args).stdout(temp).run()
        if(result.exit_code != 0) {
            remove(temp)
            message := result.stderr.trim()
            if(message == "") { message = "redis-cli failed" }
            return this.failure(message, "operation_failed", result.exit_code)
        }
        text := open(temp).trim()
        if(text.empty()) { remove(temp); return {"ok":true,"data":null,"error":"","error_code":"","exit_code":0} }
        data := inject(temp)
        remove(temp)
        return {"ok":true,"data":data,"error":"","error_code":"","exit_code":0}
    }

    fn(open(desc)) {
        args := []
        host := "127.0.0.1"
        port := 6379
        db := 0
        if(desc != null) {
            host = desc.get("host", "127.0.0.1")
            port = desc.get("port", 6379)
            db = desc.get("db", 0)
        }
        args.push("-h"); args.push(host)
        args.push("-p"); args.push(port.to_string())
        args.push("-n"); args.push(db.to_string())
        return {"conn": args}
    }

    fn(get(client, key)) { return this.cli_json(client.conn + ["GET", key]) }
    fn(set(client, key, value)) { return this.cli_json(client.conn + ["SET", key, value]) }
    fn(del(client, key)) { return this.cli_json(client.conn + ["DEL", key]) }
    fn(exists(client, key)) { return this.cli_json(client.conn + ["EXISTS", key]) }
    fn(expire(client, key, seconds)) { return this.cli_json(client.conn + ["EXPIRE", key, seconds.to_string()]) }

    fn(command(client, ...rest)) {
        if(rest.size() == 0) { return this.failure("redis command name is required", "invalid_input", 2) }
        cmd_name := rest[0]
        if(type(cmd_name) != "string" || cmd_name == "" || cmd_name.substr(0, 1) == "-") {
            return this.failure("redis command name must be a non-empty string not starting with '-'", "invalid_input", 2)
        }
        return this.cli_json(client.conn + rest)
    }
}

redis := redis()
export(redis)
