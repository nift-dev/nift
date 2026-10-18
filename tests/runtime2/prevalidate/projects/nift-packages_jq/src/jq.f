/*
    jq package for Nift. Runtime dependency: the `jq` executable on PATH.
    Public API: only the exported `jq` facade. Output remains jq-produced text.
*/

jq_backend_requested := "auto"
jq_backend_locked := false
jq_backend_selected := ""

struct(jq) {
    private fn(process_available()) {
        return getenv("NIFT_NO_PROCESS") == null && which("jq") != null
    }

    fn(available()) { return this.backend() != null }

    fn(backends()) {
        if(this.process_available()) { return ["process"] }
        return []
    }

    fn(backend()) {
        if(jq_backend_locked) {
            if(jq_backend_selected != "") { return jq_backend_selected }
            return null
        }
        if(jq_backend_requested == "process") {
            if(this.process_available()) { return "process" }
            return null
        }
        if(jq_backend_requested == "auto" && this.process_available()) { return "process" }
        return null
    }

    fn(use_backend(name)) {
        if(type(name) != "string") {
            return {"ok":false,"error":"jq backend name must be a string","error_code":"invalid_backend","backend":this.backend()}
        }
        if(jq_backend_locked) {
            return {"ok":false,"error":"jq backend is already selected","error_code":"backend_locked","backend":this.backend()}
        }
        if(name != "auto" && name != "process" && name != "ffi" && name != "native") {
            return {"ok":false,"error":"unknown jq backend: " + name,"error_code":"unknown_backend","backend":this.backend()}
        }
        if(name != "auto" && name != "process") {
            return {"ok":false,"error":"jq backend is not implemented: " + name,"error_code":"backend_unavailable","backend":this.backend()}
        }
        if(name == "process" && !this.process_available()) {
            return {"ok":false,"error":"jq process backend is unavailable","error_code":"backend_unavailable","backend":this.backend()}
        }
        jq_backend_requested = name
        return {"ok":true,"error":"","error_code":"","backend":this.backend()}
    }

    fn(capabilities()) {
        return {"raw":true,"compact":true,"slurp":true,"extra_args":true,"file_input":true,"text_output":true}
    }

    fn(version()) {
        if(!this.process_available()) { return "" }
        result := run("jq", "--version")
        if(result.exit_code != 0) { return "" }
        return result.stdout.trim()
    }

    private fn(temp_root()) {
        fs_root := getenv("NIFT_FS_ROOT")
        if(fs_root != null && fs_root != "") { return fs_root }
        tmpdir := getenv("TMPDIR")
        if(tmpdir != null && tmpdir != "") { return tmpdir }
        temp := getenv("TEMP")
        if(temp != null && temp != "") { return temp }
        tmp := getenv("TMP")
        if(tmp != null && tmp != "") { return tmp }
        return pwd()
    }

    private fn(temp_file()) {
        if(which("mktemp") != null) {
            template := this.temp_root() + "/.nift-jq-XXXXXX"
            made := run("mktemp", template)
            if(made.exit_code == 0 && made.stdout.trim() != "") { return made.stdout.trim() }
        }
        fs_root := getenv("NIFT_FS_ROOT")
        if((fs_root == null || fs_root == "") && os() == "windows" && which("powershell.exe") != null) {
            made_windows := run("powershell.exe", "-NoProfile", "-NonInteractive", "-Command", "[System.IO.Path]::GetTempFileName()")
            if(made_windows.exit_code == 0 && made_windows.stdout.trim() != "") { return made_windows.stdout.trim() }
        }
        return ""
    }

    private fn(write_temp(content)) {
        temp := this.temp_file()
        if(temp == "") { return "" }
        output := file(temp)
        output.open("w")
        output.write(content)
        output.save()
        output.close()
        return temp
    }

    private fn(base_result(ok, stdout, stderr, exit_code, error, error_code, backend)) {
        return {"ok":ok,"stdout":stdout,"stderr":stderr,"exit_code":exit_code,"error":error,"error_code":error_code,"backend":backend}
    }

    private fn(invalid(message)) {
        return this.base_result(false, "", "", null, message, "invalid_options", this.backend())
    }

    private fn(options_args(options)) {
        if(options == null) { return [] }
        if(type(options) != "object") { return null }
        allowed := ["raw", "compact", "slurp", "extra_args"]
        for(key : options.keys()) {
            if(!allowed.contains(key)) { return null }
        }
        args := []
        for(flag : ["raw", "compact", "slurp"]) {
            value := options.get(flag, false)
            if(type(value) != "bool") { return null }
            if(value) {
                if(flag == "raw") { args.push("--raw-output") }
                else if(flag == "compact") { args.push("--compact-output") }
                else { args.push("--slurp") }
            }
        }
        extra := options.get("extra_args", [])
        if(type(extra) != "array") { return null }
        for(arg : extra) {
            if(type(arg) != "string") { return null }
            args.push(arg)
        }
        return args
    }

    private fn(execute(input_path, expression, options)) {
        if(type(input_path) != "string" || type(expression) != "string") {
            return this.invalid("input path and expression must be strings")
        }
        args := this.options_args(options)
        if(args == null) {
            return this.invalid("options must contain only boolean raw/compact/slurp values and a string-array extra_args")
        }
        backend := this.backend()
        if(backend != null) { jq_backend_selected = backend }
        jq_backend_locked = true
        if(backend == null || !this.process_available()) {
            return this.base_result(false, "", "", 127, "jq process backend is unavailable", "backend_unavailable", backend)
        }
        args.push("--")
        args.push(expression)
        args.push(input_path)
        result := run("jq", ...args)
        if(result.exit_code != 0) {
            message := result.stderr
            if(message == "") { message = "jq process failed with exit code " + result.exit_code.to_string() }
            return this.base_result(false, result.stdout, result.stderr, result.exit_code, message, "process_failed", backend)
        }
        return this.base_result(true, result.stdout, result.stderr, result.exit_code, "", "", backend)
    }

    private fn(opt_at(rest)) {
        if(rest.size() > 0) { return rest[0] }
        return null
    }

    fn(run(input_json, expression, ...rest)) {
        if(type(input_json) != "string") { return this.invalid("input_json must be a string") }
        options := this.opt_at(rest)
        if(rest.size() > 1) { return this.invalid("run accepts at most one options object") }
        args := this.options_args(options)
        if(args == null) {
            return this.invalid("options must contain only boolean raw/compact/slurp values and a string-array extra_args")
        }
        backend := this.backend()
        if(backend != null) { jq_backend_selected = backend }
        jq_backend_locked = true
        if(backend == null || !this.process_available()) {
            return this.base_result(false, "", "", 127, "jq process backend is unavailable", "backend_unavailable", backend)
        }
        temp := this.write_temp(input_json)
        if(temp == "") {
            return this.base_result(false, "", "", null, "cannot securely create jq input temporary file", "temporary_file", backend)
        }
        result := this.execute(temp, expression, options)
        remove(temp)
        return result
    }

    fn(query(input_json, expression, ...rest)) {
        return this.run(input_json, expression, ...rest)
    }

    fn(file(input_path, expression, ...rest)) {
        if(rest.size() > 1) { return this.invalid("file accepts at most one options object") }
        if(rest.size() > 0) { return this.execute(input_path, expression, rest[0]) }
        return this.execute(input_path, expression, null)
    }
}

jq := jq()
export(jq)
