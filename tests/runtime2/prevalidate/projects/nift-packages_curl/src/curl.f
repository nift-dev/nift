/*
    curl package for Nift. Runtime dependency: the `curl` executable on PATH.
    Public API: the `curl` facade (request/verb helpers, sessions, capability
    inspection) plus the deprecated v0.x compatibility aliases.
    All implementation helpers are private. Nift itself carries no
    libcurl/TLS/networking dependency; the package drives the user's `curl`
    through Nift's structured process API (argv, never shell concatenation).
*/

curl_backend_requested := "auto"
curl_backend_locked := false
curl_backend_selected := ""
curl_temp_seq := 0
curl_session_seq := 0
curl_sessions := map()

struct(curl) {

    private fn(process_available()) {
        return getenv("NIFT_NO_PROCESS") == null && which("curl") != null
    }

    fn(available()) { return this.backend() != null }

    fn(backends()) {
        if(this.process_available()) { return ["process"] }
        return []
    }

    fn(backend()) {
        if(curl_backend_locked) {
            if(curl_backend_selected != "") { return curl_backend_selected }
            return null
        }
        if(curl_backend_requested == "process") {
            if(this.process_available()) { return "process" }
            return null
        }
        if(curl_backend_requested == "auto" && this.process_available()) { return "process" }
        return null
    }

    fn(use_backend(name)) {
        if(curl_backend_locked) {
            return {"ok":false,"error":"curl backend is already selected","error_code":"backend_locked","backend":this.backend()}
        }
        if(name != "auto" && name != "process" && name != "ffi" && name != "native") {
            return {"ok":false,"error":"unknown curl backend: " + name,"error_code":"unknown_backend","backend":this.backend()}
        }
        if(name != "auto" && name != "process") {
            return {"ok":false,"error":"curl backend is not implemented: " + name,"error_code":"backend_unavailable","backend":this.backend()}
        }
        if(name == "process" && !this.process_available()) {
            return {"ok":false,"error":"curl process backend is unavailable","error_code":"backend_unavailable","backend":this.backend()}
        }
        curl_backend_requested = name
        return {"ok":true,"error":"","error_code":"","backend":this.backend()}
    }


    fn(version()) {
        if(!this.process_available()) { return "" }
        r := run("curl", "--version")
        if(r.exit_code != 0) { return "" }
        return r.stdout.split("\n")[0]
    }

    fn(features()) {
        if(!this.process_available()) { return "" }
        r := run("curl", "--version")
        if(r.exit_code != 0) { return "" }
        return r.stdout
    }

    fn(capabilities()) {
        caps := {
            "buffered":true,
            "output_file":true,
            "binary":true,
            "multipart":true,
            "forms":true,
            "cookies":true,
            "sessions":true,
            "redirects":true,
            "timeouts":true,
            "auth":true,
            "proxy":true,
            "tls_controls":true,
            "tls":false,
            "https":false,
            "http2":false,
            "http3":false,
            "brotli":false,
            "zstd":false,
            "compression":false,
            "ipv6":false,
            "unix_sockets":false,
            "streaming":false,
            "websocket":false
        }
        if(!this.process_available()) { return caps }
        r := run("curl", "--version")
        if(r.exit_code != 0) { return caps }
        feat := ""
        prot := ""
        for(line : r.stdout.split("\n")) {
            if(line.starts_with("Features:")) { feat = line.to_lower() }
            else if(line.starts_with("Protocols:")) { prot = line.to_lower() }
        }
        if(feat.contains("http2")) { caps["http2"] = true }
        if(feat.contains("http3")) { caps["http3"] = true }
        if(feat.contains("brotli")) { caps["brotli"] = true }
        if(feat.contains("zstd")) { caps["zstd"] = true }
        if(feat.contains("ipv6")) { caps["ipv6"] = true }
        if(feat.contains("unixsockets")) { caps["unix_sockets"] = true }
        if(feat.contains("libz") || feat.contains("brotli") || feat.contains("zstd")) { caps["compression"] = true }
        if(feat.contains("ssl") || feat.contains("tls")) { caps["tls"] = true }
        if(prot.contains("https")) { caps["https"] = true; caps["tls"] = true }
        return caps
    }


    private fn(temp_root()) {
        root := ""
        tmpdir := getenv("TMPDIR")
        if(tmpdir != null && tmpdir != "") { root = tmpdir }
        if(root == "") {
            temp := getenv("TEMP")
            if(temp != null && temp != "") { root = temp }
        }
        if(root == "") {
            tmp := getenv("TMP")
            if(tmp != null && tmp != "") { root = tmp }
        }
        if(root == "") { root = pwd() }
        return root
    }

    private fn(temp_file()) {
        // On Windows an MSYS2/cygwin mktemp creates a file under a POSIX path
        // this native process cannot open, and rejecting its output would
        // orphan the file, so use the PowerShell temp helper there instead.
        // Elsewhere mktemp is preferred. A helper path is accepted only when
        // this process can actually reach it.
        if(os() == "windows") {
            if(which("powershell.exe") != null) {
                p := run("powershell.exe", "-NoProfile", "-NonInteractive", "-Command", "[System.IO.Path]::GetTempFileName()")
                if(p.exit_code == 0 && p.stdout.trim() != "" && exists(p.stdout.trim())) { return p.stdout.trim() }
            }
        } else if(which("mktemp") != null) {
            m := run("mktemp")
            if(m.exit_code == 0 && m.stdout.trim() != "" && exists(m.stdout.trim())) { return m.stdout.trim() }
        }
        attempts := 0
        while(attempts < 1000) {
            curl_temp_seq += 1
            candidate := this.temp_root() + "/.nift-curl-" + curl_temp_seq.to_string() + ".tmp"
            if(!exists(candidate)) {
                touch(candidate)
                return candidate
            }
            attempts += 1
        }
        return ""
    }

    private fn(write_temp(content)) {
        temp := this.temp_file()
        if(temp == "") { return "" }
        f := file(temp)
        f.open("w")
        f.write(content)
        f.save()
        f.close()
        return temp
    }

    private fn(write_temp_bytes(value)) {
        temp := this.temp_file()
        if(temp == "") { return "" }
        out := ofstream(temp)
        out.write_bytes(value)
        close(out)
        return temp
    }

    private fn(cleanup(temps)) {
        for(p : temps) {
            if(p != "") { remove(p) }
        }
        return null
    }


    private fn(parse_headers(text)) {
        h := map()
        lines := text.split("\r\n")
        if(lines.size() == 1) { lines = text.split("\n") }
        for(line : lines) {
            normalized := line.to_lower().trim()
            if(normalized.index_of("http/") == 0) {
                // curl -D writes every response block; retain only the final block.
                h = map()
            } else if(line.contains(":")) {
                colon := line.index_of(":")
                if(colon > 0) {
                    hname := line.substr(0, colon).to_lower().trim()
                    value := line.substr(colon + 1).trim()
                    if(hname.index_of("http/") != 0) {
                        if(h.contains(hname)) {
                            cur := h.get(hname)
                            if(type(cur) == "array") {
                                merged := []
                                for(item : cur) { merged.push(item) }
                                merged.push(value)
                                h.set(hname, merged)
                            } else {
                                h.set(hname, [cur, value])
                            }
                        } else {
                            h.set(hname, value)
                        }
                    }
                }
            }
        }
        // Arrays preserve repeated fields such as Set-Cookie without inventing a
        // delimiter that is invalid for some HTTP headers.
        entries := []
        for((k, v) : h) {
            if(type(v) == "array") { entries.push({"key": k, "value": v}) }
            else { entries.push({"key": k, "value": [v]}) }
        }
        return entries.from_entries()
    }

    private fn(header_text(v)) {
        if(type(v) == "string") { return v }
        return v.to_string()
    }

    private fn(headers_args(headers)) {
        out := []
        if(headers != null && type(headers) == "object") {
            for(k : headers.keys()) {
                v := headers.get(k)
                if(type(v) == "array") {
                    for(item : v) { out.push("-H"); out.push(k + ": " + this.header_text(item)) }
                } else {
                    out.push("-H"); out.push(k + ": " + this.header_text(v))
                }
            }
        }
        return out
    }


    private fn(opt_at(rest, i)) {
        if(i < rest.size()) { return rest[i] }
        return null
    }

    private fn(merge_opts(opts, method)) {
        if(opts == null || type(opts) != "object") { return {"method": method} }
        return opts.merge({"method": method})
    }

    private fn(is_number(v)) {
        t := type(v)
        return t == "int" || t == "float"
    }

    private fn(str_of(v)) {
        if(type(v) == "string") { return v }
        return v.to_string()
    }

    private fn(has_crlf(s)) {
        return s.contains("\r") || s.contains("\n")
    }

    private fn(headers_valid(headers)) {
        for(k : headers.keys()) {
            if(this.has_crlf(this.str_of(k))) { return false }
            v := headers.get(k)
            if(type(v) == "array") {
                for(item : v) { if(this.has_crlf(this.str_of(item))) { return false } }
            } else if(this.has_crlf(this.str_of(v))) { return false }
        }
        return true
    }

    private fn(cookies_valid(cookies)) {
        for(k : cookies.keys()) {
            if(this.has_crlf(this.str_of(k))) { return false }
            v := cookies.get(k)
            if(type(v) == "array") {
                for(item : v) { if(this.has_crlf(this.str_of(item))) { return false } }
            } else if(this.has_crlf(this.str_of(v))) { return false }
        }
        return true
    }

    private fn(intended_method(opts)) {
        if(type(opts) == "object" && opts.has("method") && type(opts.method) == "string") { return opts.method }
        return "GET"
    }

    private fn(url_with_query(url, query)) {
        if(type(query) != "object" || query.keys().size() == 0) { return url }
        parts := []
        for((k, v) : query) {
            if(type(v) == "array") {
                for(item : v) { parts.push(url_encode(this.str_of(k)) + "=" + url_encode(this.str_of(item))) }
            } else {
                parts.push(url_encode(this.str_of(k)) + "=" + url_encode(this.str_of(v)))
            }
        }
        if(parts.size() == 0) { return url }
        sep := "?"
        if(url.contains("?")) { sep = "&" }
        return url + sep + parts.join("&")
    }

    private fn(auth_valid(auth)) {
        if(type(auth) != "object") { return false }
        if(auth.has("bearer") && type(auth.bearer) == "string" && this.has_crlf(auth.bearer)) { return false }
        if(auth.has("token") && type(auth.token) == "string" && this.has_crlf(auth.token)) { return false }
        if(auth.has("basic") && type(auth.basic) == "object") {
            if(auth.basic.has("user") && this.has_crlf(this.str_of(auth.basic.user))) { return false }
            if(auth.basic.has("password") && this.has_crlf(this.str_of(auth.basic.password))) { return false }
        }
        if(auth.has("user") && type(auth.user) == "string" && this.has_crlf(auth.user)) { return false }
        if(auth.has("password") && this.has_crlf(this.str_of(auth.password))) { return false }
        return true
    }

    private fn(auth_args(auth)) {
        out := []
        if(type(auth) != "object") { return out }
        if(auth.has("bearer") && type(auth.bearer) == "string") {
            out.push("-H"); out.push("Authorization: Bearer " + auth.bearer)
            return out
        }
        if(auth.has("token") && type(auth.token) == "string") {
            out.push("-H"); out.push("Authorization: Bearer " + auth.token)
            return out
        }
        if(auth.has("basic") && type(auth.basic) == "object") {
            u := ""
            p := ""
            if(auth.basic.has("user")) { u = this.str_of(auth.basic.user) }
            if(auth.basic.has("password")) { p = this.str_of(auth.basic.password) }
            out.push("-u"); out.push(u + ":" + p)
            return out
        }
        if(auth.has("user") && type(auth.user) == "string") {
            secret := ""
            if(auth.has("password")) { secret = this.str_of(auth.password) }
            out.push("-u"); out.push(auth.user + ":" + secret)
            return out
        }
        return out
    }

    private fn(cookie_header_args(cookies)) {
        out := []
        if(type(cookies) != "object") { return out }
        parts := []
        for((k, v) : cookies) { parts.push(this.str_of(k) + "=" + this.str_of(v)) }
        if(parts.size() > 0) { out.push("-H"); out.push("Cookie: " + parts.join("; ")) }
        return out
    }

    private fn(form_args(form)) {
        out := []
        if(type(form) != "object") { return out }
        for((k, v) : form) {
            if(type(v) == "array") {
                for(item : v) { out.push("--data-urlencode"); out.push(this.str_of(k) + "=" + this.str_of(item)) }
            } else {
                out.push("--data-urlencode"); out.push(this.str_of(k) + "=" + this.str_of(v))
            }
        }
        return out
    }

    private fn(multipart_args(multipart)) {
        out := []
        if(type(multipart) != "object") { return out }
        for((k, v) : multipart) {
            if(type(v) == "object" && v.has("file")) {
                spec := this.str_of(k) + "=@" + this.str_of(v.file)
                if(v.has("content_type")) { spec = spec + ";type=" + this.str_of(v.content_type) }
                if(v.has("filename")) { spec = spec + ";filename=" + this.str_of(v.filename) }
                out.push("-F"); out.push(spec)
            } else {
                out.push("-F"); out.push(this.str_of(k) + "=" + this.str_of(v))
            }
        }
        return out
    }


    private fn(session_state(s)) {
        if(type(s) != "object" || !s.has("kind") || s.kind != "curl_session") { return null }
        if(!s.has("_session_id") || !curl_sessions.contains(s._session_id)) { return null }
        return curl_sessions.get(s._session_id)
    }

    fn(session(opts)) {
        if(type(opts) == "object") { return this.session_new(opts) }
        return this.session_new({})
    }

    private fn(session_new(opts)) {
        curl_session_seq += 1
        id := curl_session_seq
        jar := ""
        owned := false
        if(opts.has("cookie_jar") && type(opts.cookie_jar) == "string" && opts.cookie_jar != "") {
            jar = opts.cookie_jar
        } else if(opts.has("persist_cookies") && opts.persist_cookies == true) {
            jar = this.temp_file()
            if(jar != "") { owned = true }
        }
        defaults := opts.omit(["cookie_jar", "persist_cookies"])
        curl_sessions.set(id, {"defaults":defaults, "cookie_jar":jar, "jar_owned":owned})
        return {"kind":"curl_session","_session_id":id}
    }

    fn(session_close(s)) {
        state := this.session_state(s)
        if(state == null) {
            return {"ok":false,"error":"invalid curl session","error_code":"invalid_session"}
        }
        if(state.jar_owned && state.cookie_jar != "") { remove(state.cookie_jar) }
        curl_sessions.remove(s._session_id)
        return {"ok":true,"error":"","error_code":""}
    }

    private fn(merge_session(defaults, opts)) {
        merged := {}
        for((k, v) : defaults) { merged[k] = v }
        for((k, v) : opts) {
            if(k == "headers" && merged.has("headers") && type(merged.headers) == "object" && type(v) == "object") {
                h := {}
                for((hk, hv) : merged.headers) { h[hk] = hv }
                for((hk, hv) : v) { h[hk] = hv }
                merged["headers"] = h
            } else if(k == "cookies" && merged.has("cookies") && type(merged.cookies) == "object" && type(v) == "object") {
                c := {}
                for((ck, cv) : merged.cookies) { c[ck] = cv }
                for((ck, cv) : v) { c[ck] = cv }
                merged["cookies"] = c
            } else {
                merged[k] = v
            }
        }
        return merged
    }


    private fn(build(url, opts, jar)) {
        args := ["-sS"]
        temps := []
        if(opts.has("headers") && type(opts.headers) == "object") {
            if(!this.headers_valid(opts.headers)) { return {"error":"header name or value contains a line break","error_code":"invalid_header","args":args,"temps":temps,"method":"GET"} }
            for(t : this.headers_args(opts.headers)) { args.push(t) }
        }
        if(opts.has("auth")) {
            if(!this.auth_valid(opts.auth)) { return {"error":"auth user/password/token contains a line break","error_code":"invalid_header","args":args,"temps":temps,"method":"GET"} }
            for(t : this.auth_args(opts.auth)) { args.push(t) }
        }
        if(opts.has("authorization") && type(opts.authorization) == "string") {
            if(this.has_crlf(opts.authorization)) { return {"error":"authorization contains a line break","error_code":"invalid_header","args":args,"temps":temps,"method":"GET"} }
            args.push("-H"); args.push("Authorization: " + opts.authorization)
        }
        if(opts.has("cookies") && type(opts.cookies) == "object") {
            if(!this.cookies_valid(opts.cookies)) { return {"error":"cookie name or value contains a line break","error_code":"invalid_cookie","args":args,"temps":temps,"method":"GET"} }
            for(t : this.cookie_header_args(opts.cookies)) { args.push(t) }
        }
        effective_jar := jar
        if(opts.has("cookie_jar") && type(opts.cookie_jar) == "string") { effective_jar = opts.cookie_jar }
        if(effective_jar != "") {
            args.push("-b"); args.push(effective_jar)
            args.push("-c"); args.push(effective_jar)
        }

        method := "GET"
        if(opts.has("method") && type(opts.method) == "string") { method = opts.method }
        if(this.has_crlf(method)) { return {"error":"method contains a line break","error_code":"invalid_method","args":args,"temps":temps,"method":"GET"} }
        body_temp := ""
        if(opts.has("multipart") && type(opts.multipart) == "object") {
            for(t : this.multipart_args(opts.multipart)) { args.push(t) }
            if(method == "GET") { method = "POST" }
        } else if(opts.has("form") && type(opts.form) == "object") {
            for(t : this.form_args(opts.form)) { args.push(t) }
            if(method == "GET") { method = "POST" }
        } else if(opts.has("upload") && type(opts.upload) == "string" && opts.upload != "") {
            args.push("-T"); args.push(opts.upload)
            if(!opts.has("method")) { method = "PUT" }
        } else if(opts.has("json")) {
            body_temp = this.write_temp(opts.json.stringify())
            if(body_temp == "") { return {"error":"cannot create request body temporary file","error_code":"temporary_file","args":args,"temps":temps,"method":method} }
            temps.push(body_temp)
            args.push("--data-binary"); args.push("@" + body_temp)
            args.push("-H"); args.push("Content-Type: application/json")
            if(method == "GET") { method = "POST" }
        } else if(opts.has("body_file") && type(opts.body_file) == "string" && opts.body_file != "") {
            args.push("--data-binary"); args.push("@" + opts.body_file)
            if(method == "GET") { method = "POST" }
        } else if(opts.has("body") && type(opts.body) == "bytes") {
            body_temp = this.write_temp_bytes(opts.body)
            if(body_temp == "") { return {"error":"cannot create request body temporary file","error_code":"temporary_file","args":args,"temps":temps,"method":method} }
            temps.push(body_temp)
            args.push("--data-binary"); args.push("@" + body_temp)
            if(method == "GET") { method = "POST" }
        } else if(opts.has("body") && type(opts.body) == "string" && opts.body != "") {
            body_temp = this.write_temp(opts.body)
            if(body_temp == "") { return {"error":"cannot create request body temporary file","error_code":"temporary_file","args":args,"temps":temps,"method":method} }
            temps.push(body_temp)
            args.push("--data-binary"); args.push("@" + body_temp)
            if(method == "GET") { method = "POST" }
        }

        if(method == "HEAD") {
            args.push("--head")
        } else if(method != "GET") {
            args.push("-X"); args.push(method)
        }
        if(opts.has("follow_redirects") && opts.follow_redirects == true) { args.push("-L") }
        if(opts.has("max_redirects") && this.is_number(opts.max_redirects)) { args.push("--max-redirs"); args.push(this.str_of(opts.max_redirects)) }
        if(opts.has("timeout") && this.is_number(opts.timeout)) { args.push("--max-time"); args.push(this.str_of(opts.timeout)) }
        if(opts.has("connect_timeout") && this.is_number(opts.connect_timeout)) { args.push("--connect-timeout"); args.push(this.str_of(opts.connect_timeout)) }
        if(opts.has("proxy") && type(opts.proxy) == "string" && opts.proxy != "") { args.push("-x"); args.push(opts.proxy) }
        if(opts.has("cacert") && type(opts.cacert) == "string" && opts.cacert != "") { args.push("--cacert"); args.push(opts.cacert) }
        if(opts.has("capath") && type(opts.capath) == "string" && opts.capath != "") { args.push("--capath"); args.push(opts.capath) }
        if(opts.has("cert") && type(opts.cert) == "string" && opts.cert != "") { args.push("--cert"); args.push(opts.cert) }
        if(opts.has("key") && type(opts.key) == "string" && opts.key != "") { args.push("--key"); args.push(opts.key) }
        if(opts.has("insecure") && opts.insecure == true) { args.push("-k") }
        if(opts.has("compressed") && opts.compressed == true) { args.push("--compressed") }
        if(opts.has("http_version") && type(opts.http_version) == "string") {
            v := opts.http_version
            if(v == "1.1" || v == "1") { args.push("--http1.1") }
            else if(v == "2") { args.push("--http2") }
            else if(v == "2-prior-knowledge") { args.push("--http2-prior-knowledge") }
            else if(v == "3") { args.push("--http3") }
        }
        if(opts.has("user_agent") && type(opts.user_agent) == "string") { args.push("-A"); args.push(opts.user_agent) }

        final_url := url
        if(opts.has("query")) { final_url = this.url_with_query(url, opts.query) }

        args.push("-w"); args.push("%{http_code}\t%{url_effective}")

        header_temp := this.temp_file()
        if(header_temp == "") { return {"error":"cannot create response header temporary file","error_code":"temporary_file","args":args,"temps":temps,"method":method} }
        temps.push(header_temp)
        args.push("-D"); args.push(header_temp)

        output := ""
        resp_body_temp := ""
        if(opts.has("output") && type(opts.output) == "string" && opts.output != "") {
            output = opts.output
            args.push("-o"); args.push(output)
        } else {
            resp_body_temp = this.temp_file()
            if(resp_body_temp == "") { return {"error":"cannot create response body temporary file","error_code":"temporary_file","args":args,"temps":temps,"method":method} }
            temps.push(resp_body_temp)
            args.push("-o"); args.push(resp_body_temp)
        }
        args.push("--")
        args.push(final_url)
        return {"args":args,"temps":temps,"body_temp":body_temp,"header_temp":header_temp,"resp_body_temp":resp_body_temp,"output":output,"method":method,"url":final_url,"error":"","error_code":""}
    }

    private fn(failure(url, method, message, code, backend, exit_code)) {
        return {"ok":false,"status":0,"headers":{},"body":null,"body_bytes":null,"output":null,"url":url,"effective_url":"","method":method,"error":message,"error_code":code,"backend":backend,"exit_code":exit_code}
    }

    private fn(perform(url, opts)) {
        backend := this.backend()
        if(backend != null) { curl_backend_selected = backend }
        curl_backend_locked = true
        if(backend == null) {
            return this.failure(url, this.intended_method(opts), "curl process backend is unavailable", "backend_unavailable", null, 127)
        }
        jar := ""
        merged := opts
        if(opts.has("session")) {
            state := this.session_state(opts.session)
            if(state == null) {
                return this.failure(url, this.intended_method(opts), "invalid curl session", "invalid_session", backend, null)
            }
            jar = state.cookie_jar
            merged = this.merge_session(state.defaults, opts.omit(["session"]))
        }
        built := this.build(url, merged, jar)
        if(built.error_code != "") {
            this.cleanup(built.temps)
            return this.failure(url, built.method, built.error, built.error_code, backend, null)
        }
        a := built.args
        r := run("curl", ...a)
        if(r.exit_code != 0) {
            this.cleanup(built.temps)
            error_code := "transport_failure"
            if(r.exit_code == 28) { error_code = "timeout" }
            else if(r.exit_code == 23 || r.exit_code == 26) { error_code = "file_error" }
            return this.failure(url, built.method, r.stderr, error_code, backend, r.exit_code)
        }
        status := 0
        effective_url := ""
        w := r.stdout.trim()
        if(w != "") {
            parts := w.split("\t")
            if(parts[0] != "") { status = parts[0].to_int() }
            if(parts.size() > 1) { effective_url = parts[1] }
        }
        headers_obj := {}
        if(built.header_temp != "") { headers_obj = this.parse_headers(open(built.header_temp)) }
        resp := {"ok":true,"status":status,"headers":headers_obj,"body":null,"body_bytes":null,"output":null,"url":built.url,"effective_url":effective_url,"method":built.method,"error":"","error_code":"","backend":backend,"exit_code":r.exit_code}
        if(built.output != "") {
            resp["output"] = built.output
        } else if(built.resp_body_temp != "" && built.method != "HEAD") {
            if(merged.has("binary") && merged.binary == true) {
                resp["body_bytes"] = open_bytes(built.resp_body_temp)
            } else {
                resp["body"] = open(built.resp_body_temp)
            }
        }
        this.cleanup(built.temps)
        return resp
    }


    fn(request(url, ...rest)) {
        if(rest.size() > 0 && type(rest[0]) == "object") { return this.perform(url, rest[0]) }
        return this.perform(url, {})
    }

    fn(get(url, ...rest)) { return this.request(url, this.merge_opts(this.opt_at(rest, 0), "GET")) }
    fn(post(url, ...rest)) { return this.request(url, this.merge_opts(this.opt_at(rest, 0), "POST")) }
    fn(put(url, ...rest)) { return this.request(url, this.merge_opts(this.opt_at(rest, 0), "PUT")) }
    fn(patch(url, ...rest)) { return this.request(url, this.merge_opts(this.opt_at(rest, 0), "PATCH")) }
    fn(delete(url, ...rest)) { return this.request(url, this.merge_opts(this.opt_at(rest, 0), "DELETE")) }
    fn(head(url, ...rest)) { return this.request(url, this.merge_opts(this.opt_at(rest, 0), "HEAD")) }

    fn(download(url, path, ...rest)) {
        opts := {}
        if(rest.size() > 0 && type(rest[0]) == "object") { opts = rest[0] }
        return this.request(url, opts.merge({"output": path}))
    }

    fn(upload(url, path, ...rest)) {
        opts := {}
        if(rest.size() > 0 && type(rest[0]) == "object") { opts = rest[0] }
        return this.request(url, opts.merge({"upload": path}))
    }
}

curl := curl()

export(curl)
