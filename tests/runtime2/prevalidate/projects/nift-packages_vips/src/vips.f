/*
    libvips image processing package for Nift. v0.1.0 backend: the vips
    executable (plus vipsheader for metadata). Public API: the exported `vips`
    struct. Helpers stay private.

    Every caller-controlled value is an independent argv element; the package
    never builds a shell command. Path operands that begin with '-'/'+'/'@'
    would be interpreted by the vips tools as options, so they are rejected with
    a structured `invalid_input` failure. On machines without vips, available()
    is false and operations fail closed with `backend_unavailable`.

    The libvips CLI option grammar is not exercised on this host (vips is not
    installed); path rejection is used as the safe disambiguation.
*/

struct(vips) {
    private fn(process_available()) {
        return getenv("NIFT_NO_PROCESS") == null && which("vips") != null
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

    private fn(failure(message, code, exit_code)) {
        return {"ok":false,"error":message,"error_code":code,"exit_code":exit_code}
    }

    private fn(guard()) {
        if(!this.process_available()) {
            return this.failure("vips process backend is unavailable", "backend_unavailable", 127)
        }
        return null
    }

    private fn(convert_one(op, args)) {
        guard_result := this.guard()
        if(guard_result != null) { return guard_result }
        result := run("vips", op, ...args)
        if(result.exit_code != 0) {
            message := result.stderr.trim()
            if(message == "") { message = "vips " + op + " failed" }
            return this.failure(message, "operation_failed", result.exit_code)
        }
        return {"ok":true,"error":"","error_code":"","exit_code":0}
    }

    fn(available()) { return this.process_available() }

    fn(version()) {
        if(!this.process_available()) { return "" }
        r := run("vips", "--version")
        if(r.exit_code != 0) { return "" }
        return r.stdout.trim()
    }

    fn(resize(input, output, scale)) {
        if(!this.safe_path(input) || !this.safe_path(output)) { return this.failure("input/output must be non-empty paths not starting with '-', '+' or '@'", "invalid_input", 2) }
        if(!this.is_number(scale) || !(scale > 0)) { return this.failure("resize scale must be a positive number", "invalid_options", 2) }
        return this.convert_one("resize", [input, output, scale.to_string()])
    }

    fn(thumbnail(input, output, width)) {
        if(!this.safe_path(input) || !this.safe_path(output)) { return this.failure("input/output must be non-empty paths not starting with '-', '+' or '@'", "invalid_input", 2) }
        if(!this.is_number(width) || !(width > 0)) { return this.failure("thumbnail width must be a positive number", "invalid_options", 2) }
        return this.convert_one("thumbnail", [input, output, width.to_string()])
    }

    fn(convert(input, output)) {
        if(!this.safe_path(input) || !this.safe_path(output)) { return this.failure("input/output must be non-empty paths not starting with '-', '+' or '@'", "invalid_input", 2) }
        return this.convert_one("copy", [input, output])
    }

    fn(crop(input, output, left, top, width, height)) {
        if(!this.safe_path(input) || !this.safe_path(output)) { return this.failure("input/output must be non-empty paths not starting with '-', '+' or '@'", "invalid_input", 2) }
        if(!this.is_number(left) || !this.is_number(top) || left < 0 || top < 0) { return this.failure("crop left/top must be non-negative numbers", "invalid_options", 2) }
        if(!this.is_number(width) || !this.is_number(height) || !(width > 0) || !(height > 0)) { return this.failure("crop width/height must be positive numbers", "invalid_options", 2) }
        return this.convert_one("crop", [input, output, left.to_string(), top.to_string(), width.to_string(), height.to_string()])
    }

    fn(rotate(input, output, angle)) {
        if(!this.safe_path(input) || !this.safe_path(output)) { return this.failure("input/output must be non-empty paths not starting with '-', '+' or '@'", "invalid_input", 2) }
        if(angle == 90 || angle == 180 || angle == 270) {
            angle_name := "d90"
            if(angle == 180) { angle_name = "d180" }
            else if(angle == 270) { angle_name = "d270" }
            return this.convert_one("rot", [input, output, angle_name])
        }
        return this.failure("rotate angle must be 90, 180 or 270", "invalid_options", 2)
    }

    private fn(header_field(field, input)) {
        r := run("vipsheader", "-f", field, input)
        if(r.exit_code != 0) { return null }
        return r.stdout.trim()
    }

    private fn(metadata_failure(message, code, exit_code)) {
        return {"ok":false,"width":"","height":"","bands":"","format":"","error":message,"error_code":code,"exit_code":exit_code}
    }

    fn(metadata(input)) {
        if(!this.safe_path(input)) { return this.metadata_failure("input must be a non-empty path not starting with '-', '+' or '@'", "invalid_input", 2) }
        if(!this.process_available()) { return this.metadata_failure("vips process backend is unavailable", "backend_unavailable", 127) }
        if(which("vipsheader") == null) { return this.metadata_failure("vipsheader executable not found", "backend_unavailable", 127) }
        width := this.header_field("width", input)
        height := this.header_field("height", input)
        bands := this.header_field("bands", input)
        format := this.header_field("format", input)
        if(width == null || height == null || bands == null || format == null) {
            return this.metadata_failure("vipsheader failed to read image metadata", "operation_failed", null)
        }
        return {"ok":true,"width":width,"height":height,"bands":bands,"format":format,"error":"","error_code":"","exit_code":0}
    }
}

vips := vips()
export(vips)
