/*
    ImageMagick image processing package for Nift. v0.1.0 backend: the magick
    executable. The package repository is named "imagemagick" but the exported
    module-style value is `magick` (deliberate: package name != exported name).
    Public API: the `magick` struct. Helpers stay private.

    Every caller-controlled value is passed as an independent argv element; the
    package never builds a shell command. Path operands that begin with '-'
    would be interpreted by ImageMagick as options, so they are rejected with a
    structured `invalid_input` failure rather than allowed through.
*/

struct(magick) {
    private fn(process_available()) {
        return getenv("NIFT_NO_PROCESS") == null && which("magick") != null
    }

    private fn(is_number(value)) {
        t := type(value)
        return t == "int" || t == "float"
    }

    private fn(safe_path(value)) {
        if(type(value) != "string" || value == "") { return false }
        lead := value.substr(0, 1)
        if(lead == "-" || lead == "+" || lead == "@") { return false }
        return true
    }

    private fn(safe_option(value)) {
        if(type(value) != "string" || value == "") { return false }
        if(value.substr(0, 1) == "-") { return false }
        return true
    }

    private fn(failure(message, code, exit_code)) {
        return {"ok":false,"error":message,"error_code":code,"exit_code":exit_code}
    }

    private fn(guard()) {
        if(!this.process_available()) {
            return this.failure("magick process backend is unavailable", "backend_unavailable", 127)
        }
        return null
    }

    private fn(run_command(argv)) {
        guard_result := this.guard()
        if(guard_result != null) { return guard_result }
        result := run("magick", ...argv)
        if(result.exit_code != 0) {
            message := result.stderr.trim()
            if(message == "") { message = "magick operation failed" }
            return this.failure(message, "operation_failed", result.exit_code)
        }
        return {"ok":true,"error":"","error_code":"","exit_code":0}
    }

    fn(available()) { return this.process_available() }

    fn(version()) {
        if(!this.process_available()) { return "" }
        r := run("magick", "--version")
        if(r.exit_code != 0) { return "" }
        return r.stdout.split("\n")[0]
    }

    fn(resize(input, output, opts)) {
        if(!this.safe_path(input) || !this.safe_path(output)) {
            return this.failure("input/output must be non-empty paths not starting with '-'", "invalid_input", 2)
        }
        if(type(opts) != "object") {
            return this.failure("resize requires an options object", "invalid_options", 2)
        }
        size := ""
        if(opts.has("percent")) {
            if(!this.is_number(opts.get("percent")) || !(opts.get("percent") > 0)) { return this.failure("resize percent must be a positive number", "invalid_options", 2) }
            size = opts.get("percent").to_string() + "%"
        } else if(opts.has("width") && opts.has("height")) {
            if(!this.is_number(opts.get("width")) || !this.is_number(opts.get("height")) || !(opts.get("width") > 0) || !(opts.get("height") > 0)) { return this.failure("resize width/height must be positive numbers", "invalid_options", 2) }
            size = opts.get("width").to_string() + "x" + opts.get("height").to_string()
        } else if(opts.has("width")) {
            if(!this.is_number(opts.get("width")) || !(opts.get("width") > 0)) { return this.failure("resize width must be a positive number", "invalid_options", 2) }
            size = opts.get("width").to_string() + "x"
        } else if(opts.has("height")) {
            if(!this.is_number(opts.get("height")) || !(opts.get("height") > 0)) { return this.failure("resize height must be a positive number", "invalid_options", 2) }
            size = "x" + opts.get("height").to_string()
        }
        if(size == "") { return this.failure("resize requires width/height/percent", "invalid_options", 2) }
        return this.run_command([input, "-resize", size, output])
    }

    fn(crop(input, output, opts)) {
        if(!this.safe_path(input) || !this.safe_path(output)) {
            return this.failure("input/output must be non-empty paths not starting with '-'", "invalid_input", 2)
        }
        if(type(opts) != "object") { return this.failure("crop requires an options object", "invalid_options", 2) }
        w := opts.get("width", 0)
        h := opts.get("height", 0)
        x := opts.get("left", 0)
        y := opts.get("top", 0)
        if(!this.is_number(w) || !this.is_number(h) || !(w > 0) || !(h > 0)) { return this.failure("crop width/height must be positive numbers", "invalid_options", 2) }
        if(!this.is_number(x) || !this.is_number(y) || x < 0 || y < 0) { return this.failure("crop left/top must be non-negative numbers", "invalid_options", 2) }
        return this.run_command([input, "-crop", w.to_string() + "x" + h.to_string() + "+" + x.to_string() + "+" + y.to_string(), output])
    }

    fn(rotate(input, output, degrees)) {
        if(!this.safe_path(input) || !this.safe_path(output)) {
            return this.failure("input/output must be non-empty paths not starting with '-'", "invalid_input", 2)
        }
        if(!this.is_number(degrees)) { return this.failure("rotate degrees must be a number", "invalid_options", 2) }
        return this.run_command([input, "-rotate", degrees.to_string(), output])
    }

    fn(convert(input, output)) {
        if(!this.safe_path(input) || !this.safe_path(output)) {
            return this.failure("input/output must be non-empty paths not starting with '-'", "invalid_input", 2)
        }
        return this.run_command([input, output])
    }

    fn(compose(base, overlay, output, opts)) {
        if(!this.safe_path(base) || !this.safe_path(overlay) || !this.safe_path(output)) {
            return this.failure("base/overlay/output must be non-empty paths not starting with '-'", "invalid_input", 2)
        }
        argv := [base, overlay]
        if(opts != null) {
            if(type(opts) != "object") { return this.failure("compose options must be an object", "invalid_options", 2) }
            if(opts.has("gravity")) {
                if(!this.safe_option(opts.get("gravity"))) { return this.failure("compose gravity must be a non-empty value not starting with '-'", "invalid_options", 2) }
                argv.push("-gravity"); argv.push(opts.get("gravity"))
            }
            if(opts.has("geometry")) {
                if(!this.safe_option(opts.get("geometry"))) { return this.failure("compose geometry must be a non-empty value not starting with '-'", "invalid_options", 2) }
                argv.push("-geometry"); argv.push(opts.get("geometry"))
            }
        }
        argv.push("-composite")
        argv.push(output)
        return this.run_command(argv)
    }

    fn(identify(input)) {
        if(!this.safe_path(input)) {
            return this.identify_failure("input must be a non-empty path not starting with '-'", "invalid_input", 2)
        }
        guard_result := this.guard()
        if(guard_result != null) { return this.identify_failure(guard_result.error, guard_result.error_code, guard_result.exit_code) }
        result := run("magick", "identify", "-format", "%w|%h|%m|%[bit-depth]|%[colorspace]", input)
        if(result.exit_code != 0) {
            message := result.stderr.trim()
            if(message == "") { message = "magick identify failed" }
            return this.identify_failure(message, "operation_failed", result.exit_code)
        }
        parts := result.stdout.trim().split("|")
        width := ""; height := ""; format := ""; depth := ""; colorspace := ""
        if(parts.size() > 0) { width = parts[0] }
        if(parts.size() > 1) { height = parts[1] }
        if(parts.size() > 2) { format = parts[2] }
        if(parts.size() > 3) { depth = parts[3] }
        if(parts.size() > 4) { colorspace = parts[4] }
        return {"ok":true,"width":width,"height":height,"format":format,"depth":depth,"colorspace":colorspace,"error":"","error_code":"","exit_code":0}
    }

    private fn(identify_failure(message, code, exit_code)) {
        return {"ok":false,"width":"","height":"","format":"","depth":"","colorspace":"","error":message,"error_code":code,"exit_code":exit_code}
    }
}

magick := magick()
export(magick)
