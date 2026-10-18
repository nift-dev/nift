/*
    Thin Pandoc process facade. Public API: only the exported `pandoc` value.
    User-controlled values are passed only through structured argv. The fixed
    Windows temporary-file helper contains no user-controlled command text.
*/

pandoc_backend_requested := "auto"
pandoc_backend_locked := false
pandoc_backend_selected := ""

struct(pandoc) {
    private fn(process_available()) {
        return getenv("NIFT_NO_PROCESS") == null && which("pandoc") != null
    }

    fn(available()) { return this.backend() != null }

    fn(backends()) {
        if(this.process_available()) { return ["process"] }
        return []
    }

    fn(backend()) {
        if(pandoc_backend_locked) {
            if(pandoc_backend_selected != "") { return pandoc_backend_selected }
            return null
        }
        if(pandoc_backend_requested == "process") {
            if(this.process_available()) { return "process" }
            return null
        }
        if(pandoc_backend_requested == "auto" && this.process_available()) { return "process" }
        return null
    }

    fn(use_backend(name)) {
        if(type(name) != "string") {
            return {"ok":false,"error":"pandoc backend name must be a string","error_code":"invalid_backend","backend":this.backend()}
        }
        if(pandoc_backend_locked) {
            return {"ok":false,"error":"pandoc backend is already selected","error_code":"backend_locked","backend":this.backend()}
        }
        if(name != "auto" && name != "process" && name != "ffi" && name != "native") {
            return {"ok":false,"error":"unknown pandoc backend: " + name,"error_code":"unknown_backend","backend":this.backend()}
        }
        if(name != "auto" && name != "process") {
            return {"ok":false,"error":"pandoc backend is not implemented: " + name,"error_code":"backend_unavailable","backend":this.backend()}
        }
        if(name == "process" && !this.process_available()) {
            return {"ok":false,"error":"pandoc process backend is unavailable","error_code":"backend_unavailable","backend":this.backend()}
        }
        pandoc_backend_requested = name
        return {"ok":true,"error":"","error_code":"","backend":this.backend()}
    }

    fn(capabilities()) {
        return {"buffered_text":true,"output_file":true,"binary_output_file":true,"streaming":false}
    }

    fn(version()) {
        if(!this.process_available()) { return "" }
        result := run("pandoc", "--version")
        if(result.exit_code != 0) { return "" }
        return result.stdout.split("\n")[0].trim()
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
            template := this.temp_root() + "/.nift-pandoc-XXXXXX"
            made := run("mktemp", template)
            if(made.exit_code == 0 && made.stdout.trim() != "") { return made.stdout.trim() }
        }
        fs_root := getenv("NIFT_FS_ROOT")
        if((fs_root == null || fs_root == "") && os() == "windows" && which("powershell.exe") != null) {
            made := run("powershell.exe", "-NoProfile", "-NonInteractive", "-Command", "[System.IO.Path]::GetTempFileName()")
            if(made.exit_code == 0 && made.stdout.trim() != "") { return made.stdout.trim() }
        }
        return ""
    }

    private fn(failure(message, code, backend, exit_code, stderr, output, text)) {
        return {"ok":false,"text":text,"output":output,"stderr":stderr,"exit_code":exit_code,"error":message,"error_code":code,"backend":backend}
    }

    private fn(validate_options(options)) {
        if(options == null) { return {"ok":true,"args":[],"output":null} }
        if(type(options) != "object") {
            return {"ok":false,"error":"pandoc options must be an object","error_code":"invalid_options"}
        }
        allowed := ["from", "to", "standalone", "metadata", "template", "output", "extra_args"]
        for(key : options.keys()) {
            if(!allowed.contains(key)) {
                return {"ok":false,"error":"unknown pandoc option: " + key,"error_code":"invalid_options"}
            }
        }
        args := []
        for(option_name : ["from", "to"]) {
            if(options.has(option_name)) {
                value := options.get(option_name)
                if(type(value) != "string" || value == "") {
                    return {"ok":false,"error":"pandoc option '" + option_name + "' must be a non-empty string","error_code":"invalid_options"}
                }
                args.push("--" + option_name); args.push(value)
            }
        }
        if(options.has("standalone")) {
            standalone := options.get("standalone")
            if(type(standalone) != "bool") {
                return {"ok":false,"error":"pandoc option 'standalone' must be a bool","error_code":"invalid_options"}
            }
            if(standalone) { args.push("--standalone") }
        }
        if(options.has("metadata")) {
            metadata := options.get("metadata")
            if(type(metadata) != "object") {
                return {"ok":false,"error":"pandoc option 'metadata' must be an object","error_code":"invalid_options"}
            }
            keys := metadata.keys()
            keys.sort()
            for(key : keys) {
                value := metadata.get(key)
                if(type(value) != "string" && type(value) != "int" && type(value) != "float" && type(value) != "bool") {
                    return {"ok":false,"error":"pandoc metadata values must be strings, numbers, or bools","error_code":"invalid_options"}
                }
                args.push("--metadata"); args.push(key + "=" + value.to_string())
            }
        }
        for(option_name : ["template", "output"]) {
            if(options.has(option_name)) {
                value := options.get(option_name)
                if(type(value) != "string" || value == "") {
                    return {"ok":false,"error":"pandoc option '" + option_name + "' must be a non-empty string","error_code":"invalid_options"}
                }
                args.push("--" + option_name); args.push(value)
            }
        }
        if(options.has("extra_args")) {
            extra := options.get("extra_args")
            if(type(extra) != "array") {
                return {"ok":false,"error":"pandoc option 'extra_args' must be an array of strings","error_code":"invalid_options"}
            }
            for(arg : extra) {
                if(type(arg) != "string") {
                    return {"ok":false,"error":"pandoc option 'extra_args' must be an array of strings","error_code":"invalid_options"}
                }
                // Pandoc uniquely abbreviates the single output long option
                // (`--o`, `--ou`, `--out`, ... `--output`) and accepts `-o`/
                // `-oFILE`. Reject every spelling so extra_args can never select
                // an output destination and invalidate the result contract.
                if(arg.index_of("-o") == 0 || arg.index_of("--o") == 0) {
                    return {"ok":false,"error":"pandoc output must be selected with the 'output' option","error_code":"invalid_options"}
                }
                args.push(arg)
            }
        }
        if(options.has("output")) { return {"ok":true,"args":args,"output":options.get("output")} }
        return {"ok":true,"args":args,"output":null}
    }

    private fn(execute(input_path, validated)) {
        selected := this.backend()
        if(selected != null) { pandoc_backend_selected = selected }
        pandoc_backend_locked = true
        if(selected == null || !this.process_available()) {
            return this.failure("pandoc process backend is unavailable", "backend_unavailable", selected, 127, "", validated.output, null)
        }
        args := validated.args
        args.push("--")
        args.push(input_path)
        result := run("pandoc", ...args)
        if(result.exit_code != 0) {
            message := result.stderr.trim()
            if(message == "") { message = "pandoc conversion failed" }
            if(validated.output == null) {
                return this.failure(message, "conversion_failed", selected, result.exit_code, result.stderr, null, result.stdout)
            }
            return this.failure(message, "conversion_failed", selected, result.exit_code, result.stderr, validated.output, null)
        }
        if(validated.output == null) {
            return {"ok":true,"text":result.stdout,"output":null,"stderr":result.stderr,"exit_code":result.exit_code,"error":"","error_code":"","backend":selected}
        }
        return {"ok":true,"text":null,"output":validated.output,"stderr":result.stderr,"exit_code":result.exit_code,"error":"","error_code":"","backend":selected}
    }

    fn(convert(source, options)) {
        if(type(source) != "string") {
            return this.failure("pandoc source must be a string", "invalid_source", this.backend(), null, "", null, null)
        }
        validated := this.validate_options(options)
        if(!validated.ok) { return this.failure(validated.error, validated.error_code, this.backend(), null, "", null, null) }
        selected := this.backend()
        if(selected != null) { pandoc_backend_selected = selected }
        pandoc_backend_locked = true
        if(selected == null || !this.process_available()) {
            return this.failure("pandoc process backend is unavailable", "backend_unavailable", selected, 127, "", validated.output, null)
        }
        temp := this.temp_file()
        if(temp == "") {
            return this.failure("cannot create pandoc input temporary file", "temporary_file", this.backend(), null, "", validated.output, null)
        }
        input := file(temp)
        input.open("w")
        input.write(source)
        input.save()
        input.close()
        result := this.execute(temp, validated)
        remove(temp)
        return result
    }

    fn(file(input_path, options)) {
        if(type(input_path) != "string" || input_path == "") {
            return this.failure("pandoc input path must be a non-empty string", "invalid_input", this.backend(), null, "", null, null)
        }
        validated := this.validate_options(options)
        if(!validated.ok) { return this.failure(validated.error, validated.error_code, this.backend(), null, "", null, null) }
        return this.execute(input_path, validated)
    }
}

pandoc := pandoc()
export(pandoc)
