/*
    Backend-neutral HTTP server package for Nift. v0.1.0 process bootstrap,
    with an opt-in native backend built on the socket package facade.
*/

@import("socket")

http_backend_requested := "auto"
http_backend_locked := false
http_backend_selected := ""
http_next_server_id := 0
http_server_backends := map()
http_server_configs := map()
http_server_route_counts := map()
http_server_routes := map()
http_spool_paths := map()
http_worker_response_path := ""
http_worker_stream_path := ""
http_worker_stream_done_path := ""
http_worker_request_id := ""
http_worker_request_method := ""
http_worker_stream_published := false
http_native_states := map()
http_native_parse_lib := ""
http_native_parse_outbuf := ""

struct(http) {
    private fn(helper_path()) {
        return pwd() + "/.nift/packages/http/helper/http_helper.py"
    }

    private fn(python_path()) {
        override := getenv("NIFT_HTTP_PYTHON")
        if(override != null && override != "") { return override }
        return which("python3")
    }

    private fn(nift_path()) {
        override := getenv("NIFT_HTTP_NIFT")
        if(override != null && override != "") { return override }
        return which("nift")
    }

    private fn(available_now()) {
        if(getenv("NIFT_NO_PROCESS") != null) { return false }
        python := this.python_path()
        return python != null && this.nift_path() != null && exists(this.helper_path())
    }

    fn(available()) {
        return this.available_now()
    }

    fn(backends()) {
        result := []
        if(this.available_now()) { result.push("process") }
        result.push("native")
        return result
    }

    private fn(resolved_backend()) {
        if(http_backend_locked) {
            if(http_backend_selected != "") { return http_backend_selected }
            return null
        }
        if(http_backend_requested == "process") {
            if(this.available_now()) { return "process" }
            return null
        }
        if(http_backend_requested == "native") { return "native" }
        if(http_backend_requested == "auto") {
            if(this.available_now()) { return "process" }
            return "native"
        }
        return null
    }

    fn(backend()) {
        return this.resolved_backend()
    }

    fn(use_backend(name)) {
        if(http_backend_locked) {
            return {"ok":false,"error":"http backend is already selected","error_code":"backend_locked","backend":this.resolved_backend()}
        }
        if(name != "auto" && name != "process" && name != "ffi" && name != "native") {
            return {"ok":false,"error":"unknown http backend: " + name,"error_code":"unknown_backend","backend":this.resolved_backend()}
        }
        if(name != "auto" && name != "process" && name != "native") {
            return {"ok":false,"error":"http backend is not implemented: " + name,"error_code":"backend_unavailable","backend":this.resolved_backend()}
        }
        if(name == "process" && !this.available_now()) {
            return {"ok":false,"error":"http process backend is unavailable","error_code":"backend_unavailable","backend":this.resolved_backend()}
        }
        http_backend_requested = name
        return {"ok":true,"error":"","error_code":"","backend":this.resolved_backend()}
    }

    fn(capabilities()) {
        if(http_backend_locked && http_backend_selected == "native") {
            return {
                "buffered_text":true,
                "json":true,
                "forms":true,
                "cookies":true,
                "binary_files":false,
                "spooled_bodies":false,
                "uploads":false,
                "multipart":false,
                "streaming":false,
                "websockets":false,
                "tls":false,
                "persistent_workers":false
            }
        }
        capabilities := {
            "buffered_text":true,
            "json":true,
            "forms":true,
            "cookies":true,
            "binary_files":true,
            "spooled_bodies":true,
            "uploads":true,
            "multipart":true,
            "streaming":os() != "windows",
            "websockets":false,
            "tls":false,
            "persistent_workers":true
        }
        return capabilities
    }

    fn(server(config)) {
        backend := this.resolved_backend()
        if(backend != null) { http_backend_selected = backend }
        http_backend_locked = true
        if(backend == null) {
            return {"kind":"http_server_error","ok":false,"error":"http process backend is unavailable","error_code":"backend_unavailable","backend":null}
        }
        http_next_server_id += 1
        id := http_next_server_id
        http_server_backends.set(id, backend)
        http_server_configs.set(id, config)
        http_server_route_counts.set(id, 0)
        return {"kind":"http_server","_server_id":id}
    }

    private fn(is_server(app)) {
        if(type(app) != "object" || !app.has("kind") || !app.has("_server_id")) { return false }
        if(app.kind != "http_server") { return false }
        return http_server_backends.contains(app._server_id)
    }

    private fn(add_route(app, method, path, handler)) {
        if(!this.is_server(app)) {
            return {"ok":false,"error":"invalid http server handle","error_code":"invalid_handle","backend":null}
        }
        if(type(path) != "string" || !path.starts_with("/") || path.contains("?") || path.contains("#")) {
            return {"ok":false,"error":"route path must be an origin path","error_code":"invalid_route","backend":http_server_backends.get(app._server_id)}
        }
        if(type(handler) != "function") {
            return {"ok":false,"error":"route handler must be callable","error_code":"invalid_handler","backend":http_server_backends.get(app._server_id)}
        }
        if(type(method) != "string" || method == "") {
            return {"ok":false,"error":"route method must be a non-empty string","error_code":"invalid_method","backend":http_server_backends.get(app._server_id)}
        }
        method = method.to_upper()
        token_chars := "!#$%&'*+-.^_`|~0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        for(method_char : method.split("")) {
            if(!token_chars.contains(method_char)) {
                return {"ok":false,"error":"route method is not an HTTP token","error_code":"invalid_method","backend":http_server_backends.get(app._server_id)}
            }
        }
        index := http_server_route_counts.get(app._server_id)
        key := app._server_id.to_string() + ":" + index.to_string()
        http_server_routes.set(key, {"method":method,"path":path,"handler":handler})
        http_server_route_counts.set(app._server_id, index + 1)
        return {"ok":true,"error":"","error_code":"","backend":http_server_backends.get(app._server_id)}
    }

    fn(route(app, method, path, handler)) { return this.add_route(app, method, path, handler) }
    fn(get(app, path, handler)) { return this.add_route(app, "GET", path, handler) }
    fn(post(app, path, handler)) { return this.add_route(app, "POST", path, handler) }
    fn(put(app, path, handler)) { return this.add_route(app, "PUT", path, handler) }
    fn(patch(app, path, handler)) { return this.add_route(app, "PATCH", path, handler) }
    fn(delete(app, path, handler)) { return this.add_route(app, "DELETE", path, handler) }
    fn(head(app, path, handler)) { return this.add_route(app, "HEAD", path, handler) }

    private fn(route_parts(path)) {
        if(path == "/") { return [] }
        inner := path.substr(1)
        if(inner.ends_with("/")) { inner = inner.substr(0, inner.length() - 1) }
        if(inner == "") { return [] }
        return inner.split("/")
    }

    private fn(match_route(pattern, segments)) {
        parts := this.route_parts(pattern)
        if(parts.size() != segments.size()) { return null }
        params := {}
        i := 0
        while(i < parts.size()) {
            part := parts[i]
            if(part.starts_with(":")) {
                if(part.length() == 1) { return null }
                param_key := part.substr(1)
                params[param_key] = segments[i]
            }
            else if(part != segments[i]) { return null }
            i += 1
        }
        return params
    }

    private fn(options(options)) {
        if(options.size() > 0 && type(options[0]) == "object") { return options[0] }
        return {}
    }

    private fn(response_headers(kind, options)) {
        headers := {}
        if(kind == "json") { headers["content-type"] = ["application/json; charset=utf-8"] }
        else if(kind == "file") { headers["content-type"] = ["application/octet-stream"] }
        else { headers["content-type"] = ["text/plain; charset=utf-8"] }
        if(options.has("content_type")) { headers["content-type"] = [options.content_type] }
        if(options.has("headers") && type(options.headers) == "object") {
            for((header_name, header_value) : options.headers) {
                lower_header := header_name.to_lower()
                headers[lower_header] = header_value
            }
        }
        return headers
    }

    fn(text(body, ...options)) {
        parsed_options := this.options(options)
        status := 200
        if(parsed_options.has("status")) { status = parsed_options.status }
        response := {"status":status,"headers":this.response_headers("text", parsed_options),"body":{"kind":"text","text":body}}
        if(parsed_options.has("cookies")) { response["cookies"] = parsed_options.cookies }
        return response
    }

    fn(json(value, ...options)) {
        parsed_options := this.options(options)
        status := 200
        if(parsed_options.has("status")) { status = parsed_options.status }
        response := {"status":status,"headers":this.response_headers("json", parsed_options),"body":{"kind":"json","value":value}}
        if(parsed_options.has("cookies")) { response["cookies"] = parsed_options.cookies }
        return response
    }

    fn(cookie(cookie_name, cookie_value, ...options)) {
        parsed_options := this.options(options)
        cookie := {"name":cookie_name,"value":cookie_value}
        if(parsed_options.has("path")) { cookie["path"] = parsed_options.path }
        if(parsed_options.has("domain")) { cookie["domain"] = parsed_options.domain }
        if(parsed_options.has("max_age")) { cookie["max_age"] = parsed_options.max_age }
        if(parsed_options.has("expires")) { cookie["expires"] = parsed_options.expires }
        if(parsed_options.has("secure")) { cookie["secure"] = parsed_options.secure }
        if(parsed_options.has("http_only")) { cookie["http_only"] = parsed_options.http_only }
        if(parsed_options.has("same_site")) { cookie["same_site"] = parsed_options.same_site }
        return cookie
    }

    fn(file(path, ...options)) {
        parsed_options := this.options(options)
        status := 200
        if(parsed_options.has("status")) { status = parsed_options.status }
        body := {"kind":"file","path":path}
        if(parsed_options.has("download_name")) { body["download_name"] = parsed_options.download_name }
        return {"status":status,"headers":this.response_headers("file", parsed_options),"body":body}
    }

    fn(file_from(root, path, ...options)) {
        parsed_options := this.options(options)
        status := 200
        if(parsed_options.has("status")) { status = parsed_options.status }
        body := {"kind":"root_file","root":root,"path":path}
        if(parsed_options.has("download_name")) { body["download_name"] = parsed_options.download_name }
        return {"status":status,"headers":this.response_headers("file", parsed_options),"body":body}
    }

    fn(stream(producer, ...options)) {
        parsed_options := this.options(options)
        status := 200
        if(parsed_options.has("status")) { status = parsed_options.status }
        response := {
            "status":status,
            "headers":this.response_headers("stream", parsed_options),
            "body":{"kind":"stream","_producer":producer}
        }
        if(parsed_options.has("cookies")) { response["cookies"] = parsed_options.cookies }
        if(status == 204 || status == 205 || status == 304) {
            response["body"] = {"kind":"stream"}
            return response
        }
        if(http_worker_response_path != "" && http_worker_stream_path != "" &&
           http_worker_stream_done_path != "" &&
           type(producer) == "function") {
            public_response := response.omit(["body"])
            public_response["body"] = {"kind":"stream"}
            envelope := {
                "protocol":1,
                "request_id":http_worker_request_id,
                "status":public_response.status,
                "headers":public_response.headers,
                "body":public_response.body
            }
            if(public_response.has("cookies")) { envelope["cookies"] = public_response.cookies }
            this.publish_worker_response(http_worker_response_path, envelope)
            http_worker_stream_published = true
            if(http_worker_request_method != "HEAD") {
                stream_output := ofstream(http_worker_stream_path)
                write_chunk := (chunk) => {
                    stream_output.write(chunk)
                    stream_output.flush()
                }
                producer(write_chunk)
                close(stream_output)
            }
            return public_response
        }
        return response
    }

    private fn(dispatch(app, request)) {
        allowed := []
        selected_index := -1
        fallback_index := -1
        selected_params := {}
        fallback_params := {}
        i := 0
        while(i < http_server_route_counts.get(app._server_id)) {
            key := app._server_id.to_string() + ":" + i.to_string()
            route := http_server_routes.get(key)
            params := this.match_route(route.path, request.segments)
            if(params != null) {
                if(!allowed.contains(route.method)) { allowed.push(route.method) }
                if(route.method == "GET" && !allowed.contains("HEAD")) { allowed.push("HEAD") }
                if(route.method == request.method) {
                    selected_index = i
                    selected_params = params
                    break
                }
                if(request.method == "HEAD" && route.method == "GET" && fallback_index == -1) {
                    fallback_index = i
                    fallback_params = params
                }
            }
            i += 1
        }
        if(selected_index == -1 && fallback_index != -1) {
            selected_index = fallback_index
            selected_params = fallback_params
        }
        if(selected_index == -1) {
            if(allowed.size() > 0) {
                result := {"status":405,"headers":{"allow":[allowed.join(", ")],"content-type":["text/plain; charset=utf-8"]},"body":{"kind":"text","text":"method not allowed"}}
                return result
            }
            result := {"status":404,"headers":{"content-type":["text/plain; charset=utf-8"]},"body":{"kind":"text","text":"not found"}}
            return result
        }
        selected_key := app._server_id.to_string() + ":" + selected_index.to_string()
        selected := http_server_routes.get(selected_key)
        request["params"] = selected_params
        handler := selected.handler
        response := handler(request)
        if(type(response) == "string") { return this.text(response) }
        return response
    }

    private fn(prepare_request(request)) {
        if(request.has("_body_path")) {
            body_token := request.request_id + ":body"
            http_spool_paths.set(body_token, request._body_path)
            request["body"] = {"kind":"spooled","size":request.body.size,"_body_id":body_token}
        }
        public_uploads := []
        files := {}
        if(request.has("_uploads")) {
            for(raw_upload : request._uploads) {
                upload_token := request.request_id + ":upload:" + raw_upload.id
                http_spool_paths.set(upload_token, raw_upload.path)
                upload := {
                    "kind":"upload",
                    "_upload_id":upload_token,
                    "name":raw_upload.field,
                    "filename":raw_upload.filename,
                    "content_type":raw_upload.content_type,
                    "size":raw_upload.size
                }
                public_uploads.push(upload)
                field_key := raw_upload.field
                if(files.has(field_key)) {
                    existing := files[field_key]
                    if(type(existing) == "array") {
                        repeated := copy(existing)
                        repeated.push(upload)
                        files[field_key] = repeated
                    }
                    else { files[field_key] = [existing, upload] }
                }
                else { files[field_key] = upload }
            }
        }
        request["uploads"] = public_uploads
        request["files"] = files
        return request.omit(["_body_path", "_uploads", "_stream_path", "_stream_done_path"])
    }

    fn(save_upload(upload, destination)) {
        if(type(upload) != "object" || !upload.has("kind") || upload.kind != "upload" ||
           !upload.has("_upload_id") || !http_spool_paths.contains(upload._upload_id)) {
            return {"ok":false,"error":"invalid or expired upload handle","error_code":"invalid_upload","backend":"process"}
        }
        copy(http_spool_paths.get(upload._upload_id), destination)
        return {"ok":true,"error":"","error_code":"","backend":"process"}
    }

    fn(save_body(body, destination)) {
        if(type(body) != "object" || !body.has("kind") || body.kind != "spooled" ||
           !body.has("_body_id") || !http_spool_paths.contains(body._body_id)) {
            return {"ok":false,"error":"invalid or expired body handle","error_code":"invalid_body","backend":"process"}
        }
        copy(http_spool_paths.get(body._body_id), destination)
        return {"ok":true,"error":"","error_code":"","backend":"process"}
    }

    private fn(expire_request_spools(request)) {
        if(type(request.body) == "object" && request.body.has("_body_id") && http_spool_paths.contains(request.body._body_id)) {
            http_spool_paths.remove(request.body._body_id)
        }
        for(upload : request.uploads) {
            if(upload.has("_upload_id") && http_spool_paths.contains(upload._upload_id)) {
                http_spool_paths.remove(upload._upload_id)
            }
        }
    }

    private fn(response_envelope(request, response)) {
        envelope := {
            "protocol":1,
            "request_id":request.request_id,
            "status":response.status,
            "headers":response.headers,
            "body":response.body
        }
        if(response.has("cookies")) { envelope["cookies"] = response.cookies }
        return envelope
    }

    private fn(publish_worker_response(path, envelope)) {
        temporary_path := path + ".tmp"
        output := ofstream(temporary_path)
        output.write_val(envelope)
        close(output)
        move(temporary_path, path)
    }

    private fn(execute_worker_request(app, request_path, response_path)) {
        private_request := inject(request_path)
        stream_path := ""
        if(private_request.has("_stream_path")) { stream_path = private_request._stream_path }
        stream_done_path := ""
        if(private_request.has("_stream_done_path")) { stream_done_path = private_request._stream_done_path }
        request := this.prepare_request(private_request)
        http_worker_response_path = response_path
        http_worker_stream_path = stream_path
        http_worker_stream_done_path = stream_done_path
        http_worker_request_id = request.request_id
        http_worker_request_method = request.method
        http_worker_stream_published = false
        response := this.dispatch(app, request)
        if(http_worker_stream_published) {
            done_output := ofstream(http_worker_stream_done_path)
            done_output.write("done")
            close(done_output)
            this.expire_request_spools(request)
            http_worker_response_path = ""
            http_worker_stream_path = ""
            http_worker_stream_done_path = ""
            return
        }
        this.expire_request_spools(request)
        envelope := this.response_envelope(request, response)
        this.publish_worker_response(response_path, envelope)
        http_worker_response_path = ""
        http_worker_stream_path = ""
        http_worker_stream_done_path = ""
    }

    private fn(write_worker_response(app)) {
        this.execute_worker_request(app, getenv("NIFT_HTTP_REQUEST"), getenv("NIFT_HTTP_RESPONSE"))
        return {"ok":true,"error":"","error_code":"","backend":"process"}
    }

    private fn(run_persistent_worker(app)) {
        ready_path := getenv("NIFT_HTTP_READY")
        if(ready_path != null && ready_path != "") {
            ready := ofstream(ready_path)
            ready.write("ready")
            close(ready)
        }
        while(true) {
            exchange := read()
            if(exchange == null) { break }
            request_path := exchange + "/request.json"
            response_path := exchange + "/response.json"
            this.execute_worker_request(app, request_path, response_path)
        }
        return {"ok":true,"error":"","error_code":"","backend":"process"}
    }

    private fn(config_value(config, name, fallback)) {
        if(type(config) == "object" && config.has(name)) { return config.get(name) }
        return fallback
    }

    fn(listen(app)) {
        if(type(app) == "object" && app.has("kind") && app.kind == "http_server_error") {
            return {"ok":false,"error":app.error,"error_code":app.error_code,"backend":app.backend,"exit_code":127}
        }
        if(!this.is_server(app)) {
            return {"ok":false,"error":"invalid http server handle","error_code":"invalid_handle","backend":null,"exit_code":null}
        }
        if(getenv("NIFT_HTTP_WORKER") == "1") { return this.write_worker_response(app) }
        if(getenv("NIFT_HTTP_WORKER") == "persistent") { return this.run_persistent_worker(app) }
        backend := http_server_backends.get(app._server_id)
        if(backend == "native") { return this.native_listen(app) }
        if(backend != "process" || !this.available_now()) {
            return {"ok":false,"error":"http process backend is unavailable","error_code":"backend_unavailable","backend":backend,"exit_code":127}
        }
        nift := this.nift_path()
        if(nift == null) {
            return {"ok":false,"error":"cannot locate the Nift executable","error_code":"worker_unavailable","backend":"process","exit_code":127}
        }
        config := http_server_configs.get(app._server_id)
        host := this.config_value(config, "host", "127.0.0.1")
        port := this.config_value(config, "port", 8080)
        max_requests := this.config_value(config, "max_requests", 0)
        worker_timeout := this.config_value(config, "worker_timeout_ms", 30000)
        client_timeout := this.config_value(config, "client_timeout_ms", 10000)
        response_timeout := this.config_value(config, "response_timeout_ms", 30000)
        max_request_line := this.config_value(config, "max_request_line", 8192)
        max_header_bytes := this.config_value(config, "max_header_bytes", 32768)
        max_headers := this.config_value(config, "max_headers", 100)
        max_body_bytes := this.config_value(config, "max_body_bytes", 1048576)
        max_form_fields := this.config_value(config, "max_form_fields", 256)
        max_form_name_bytes := this.config_value(config, "max_form_name_bytes", 256)
        max_form_value_bytes := this.config_value(config, "max_form_value_bytes", 65536)
        max_cookie_pairs := this.config_value(config, "max_cookie_pairs", 128)
        max_multipart_parts := this.config_value(config, "max_multipart_parts", 128)
        max_multipart_files := this.config_value(config, "max_multipart_files", 32)
        max_part_header_bytes := this.config_value(config, "max_part_header_bytes", 8192)
        max_part_headers := this.config_value(config, "max_part_headers", 32)
        max_file_bytes := this.config_value(config, "max_file_bytes", 1048576)
        max_filename_bytes := this.config_value(config, "max_filename_bytes", 255)
        max_temp_bytes := this.config_value(config, "max_temp_bytes", 2097152)
        max_file_response_bytes := this.config_value(config, "max_file_response_bytes", 67108864)
        max_stream_chunk_bytes := this.config_value(config, "max_stream_chunk_bytes", 65536)
        max_stream_response_bytes := this.config_value(config, "max_stream_response_bytes", 67108864)
        max_concurrency := this.config_value(config, "max_concurrency", 1)
        worker_mode := this.config_value(config, "worker_mode", "oneshot")
        worker_pool_size := this.config_value(config, "worker_pool_size", max_concurrency)
        worker_max_requests := this.config_value(config, "worker_max_requests", 1000)
        shutdown_grace_ms := this.config_value(config, "shutdown_grace_ms", 5000)
        admission_timeout_ms := this.config_value(config, "admission_timeout_ms", 20)
        status_path := this.config_value(config, "status_path", "")
        event_log_path := this.config_value(config, "event_log_path", "")
        max_event_log_bytes := this.config_value(config, "max_event_log_bytes", 1048576)
        backlog := this.config_value(config, "backlog", 16)
        result := run(
            this.python_path(), this.helper_path(),
            "--host", host,
            "--port", port.to_string(),
            "--nift", nift,
            "--app", cmd,
            "--cwd", pwd(),
            "--max-requests", max_requests.to_string(),
            "--worker-timeout-ms", worker_timeout.to_string(),
            "--client-timeout-ms", client_timeout.to_string(),
            "--response-timeout-ms", response_timeout.to_string(),
            "--max-request-line", max_request_line.to_string(),
            "--max-header-bytes", max_header_bytes.to_string(),
            "--max-headers", max_headers.to_string(),
            "--max-body-bytes", max_body_bytes.to_string(),
            "--max-form-fields", max_form_fields.to_string(),
            "--max-form-name-bytes", max_form_name_bytes.to_string(),
            "--max-form-value-bytes", max_form_value_bytes.to_string(),
            "--max-cookie-pairs", max_cookie_pairs.to_string(),
            "--max-multipart-parts", max_multipart_parts.to_string(),
            "--max-multipart-files", max_multipart_files.to_string(),
            "--max-part-header-bytes", max_part_header_bytes.to_string(),
            "--max-part-headers", max_part_headers.to_string(),
            "--max-file-bytes", max_file_bytes.to_string(),
            "--max-filename-bytes", max_filename_bytes.to_string(),
            "--max-temp-bytes", max_temp_bytes.to_string(),
            "--max-file-response-bytes", max_file_response_bytes.to_string(),
            "--max-stream-chunk-bytes", max_stream_chunk_bytes.to_string(),
            "--max-stream-response-bytes", max_stream_response_bytes.to_string(),
            "--max-concurrency", max_concurrency.to_string(),
            "--worker-mode", worker_mode,
            "--worker-pool-size", worker_pool_size.to_string(),
            "--worker-max-requests", worker_max_requests.to_string(),
            "--shutdown-grace-ms", shutdown_grace_ms.to_string(),
            "--admission-timeout-ms", admission_timeout_ms.to_string(),
            "--status-path", status_path,
            "--event-log-path", event_log_path,
            "--max-event-log-bytes", max_event_log_bytes.to_string(),
            "--backlog", backlog.to_string()
        )
        if(!result.launched) {
            return {"ok":false,"error":"failed to launch HTTP helper","error_code":"helper_launch","backend":"process","exit_code":result.exit_code}
        }
        if(result.exit_code != 0) {
            return {"ok":false,"error":result.stderr,"error_code":"helper_failed","backend":"process","exit_code":result.exit_code}
        }
        return {"ok":true,"error":"","error_code":"","backend":"process","exit_code":0}
    }

    fn(server_backend(app)) {
        if(!this.is_server(app)) { return null }
        return http_server_backends.get(app._server_id)
    }

    private fn(native_config(config, name, fallback)) {
        if(type(config) == "object" && config.has(name)) { return config.get(name) }
        return fallback
    }

    private fn(native_reason(status)) {
        reasons := {"200":"OK","201":"Created","204":"No Content","206":"Partial Content","301":"Moved Permanently","302":"Found","304":"Not Modified","400":"Bad Request","404":"Not Found","405":"Method Not Allowed","408":"Request Timeout","411":"Length Required","413":"Payload Too Large","414":"URI Too Long","415":"Unsupported Media Type","431":"Request Header Fields Too Large","500":"Internal Server Error","501":"Not Implemented","503":"Service Unavailable"}
        if(reasons.has(status.to_string())) { return reasons[status.to_string()] }
        return "Response"
    }

    private fn(native_bytes_to_array(b)) {
        out := []
        i := 0
        n := b.length()
        while(i < n) { out.push(b[i]); i += 1 }
        return out
    }

    private fn(native_array_to_bytes(arr)) {
        return bytes(arr)
    }

    private fn(native_append_str_bytes(out, text)) {
        for(b : this.native_bytes_to_array(text.encode("utf-8"))) { out.push(b) }
        return out
    }

    private fn(native_find(hay, from, needle)) {
        n := hay.size()
        m := needle.size()
        if(m == 0) { return from }
        i := from
        while(i + m <= n) {
            match := true
            j := 0
            while(j < m) {
                if(hay[i + j] != needle[j]) { match = false; break }
                j += 1
            }
            if(match) { return i }
            i += 1
        }
        return -1
    }

    private fn(native_slice(arr, from, count)) {
        out := []
        i := 0
        while(i < count) { out.push(arr[from + i]); i += 1 }
        return out
    }

    private fn(native_is_token_char(code)) {
        alpha := (code >= 65 && code <= 90) || (code >= 97 && code <= 122)
        digit := code >= 48 && code <= 57
        special := code == 33 || code == 35 || code == 36 || code == 37 || code == 38 || code == 39 || code == 42 || code == 43 || code == 45 || code == 46 || code == 94 || code == 95 || code == 96 || code == 124 || code == 126
        return alpha || digit || special
    }

    private fn(native_ascii_lower(code)) {
        if(code >= 65 && code <= 90) { return code + 32 }
        return code
    }

    private fn(native_is_digits_str(str)) {
        b := str.encode("utf-8")
        if(b.length() == 0) { return false }
        i := 0
        ok := true
        n := b.length()
        while(i < n && ok) {
            c := b[i]
            if(c < 48 || c > 57) { ok = false }
            i += 1
        }
        return ok
    }

    private fn(native_valid_utf8(arr)) {
        i := 0
        n := arr.size()
        while(i < n) {
            c := arr[i]
            if(c < 128) { i += 1 }
            else if(c >= 192 && c <= 223) {
                if(i + 1 >= n || !(arr[i + 1] >= 128 && arr[i + 1] <= 191)) { return false }
                i += 2
            }
            else if(c >= 224 && c <= 239) {
                if(i + 2 >= n || !(arr[i + 1] >= 128 && arr[i + 1] <= 191) || !(arr[i + 2] >= 128 && arr[i + 2] <= 191)) { return false }
                i += 3
            }
            else if(c >= 240 && c <= 247) {
                if(i + 3 >= n || !(arr[i + 1] >= 128 && arr[i + 1] <= 191) || !(arr[i + 2] >= 128 && arr[i + 2] <= 191) || !(arr[i + 3] >= 128 && arr[i + 3] <= 191)) { return false }
                i += 4
            }
            else { return false }
        }
        return true
    }

    private fn(native_hex_val(code)) {
        if(code >= 48 && code <= 57) { return code - 48 }
        if(code >= 97 && code <= 102) { return code - 87 }
        if(code >= 65 && code <= 70) { return code - 55 }
        return 0
    }

    private fn(native_unquote(str, plus_as_space)) {
        b := str.encode("utf-8")
        out := []
        i := 0
        n := b.length()
        while(i < n) {
            code := b[i]
            pushed := false
            if(code == 43 && plus_as_space) { out.push(32); i += 1; pushed = true }
            if(!pushed && code == 37 && i + 2 < n) {
                c1 := b[i + 1]
                c2 := b[i + 2]
                h1ok := (c1 >= 48 && c1 <= 57) || (c1 >= 97 && c1 <= 102) || (c1 >= 65 && c1 <= 70)
                h2ok := (c2 >= 48 && c2 <= 57) || (c2 >= 97 && c2 <= 102) || (c2 >= 65 && c2 <= 70)
                if(h1ok && h2ok) { out.push(this.native_hex_val(c1) * 16 + this.native_hex_val(c2)); i += 3; pushed = true }
            }
            if(!pushed) {
                if(code == 37) { return null }
                out.push(code)
                i += 1
            }
        }
        if(!this.native_valid_utf8(out)) { return null }
        return bytes(out).decode("utf-8")
    }

    private fn(native_split_crlf(arr)) {
        lines := []
        i := 0
        n := arr.size()
        start := 0
        while(i < n) {
            if(arr[i] == 13 && i + 1 < n && arr[i + 1] == 10) {
                lines.push(this.native_slice(arr, start, i - start))
                i += 2
                start = i
            } else { i += 1 }
        }
        if(start < n) { lines.push(this.native_slice(arr, start, n - start)) }
        return lines
    }

    private fn(native_parse_request_line(line, max_request_line)) {
        n := line.size()
        if(n == 0 || n > max_request_line) { return {"ok":false,"status":414} }
        sp1 := -1
        sp2 := -1
        i := 0
        while(i < n) {
            if(line[i] == 32) {
                if(sp1 == -1) { sp1 = i } else { sp2 = i; break }
            }
            i += 1
        }
        if(sp1 <= 0 || sp2 <= sp1 + 1) { return {"ok":false,"status":400} }
        i = sp2 + 1
        while(i < n) { if(line[i] == 32) { return {"ok":false,"status":400} } i += 1 }
        method_arr := this.native_slice(line, 0, sp1)
        target_arr := this.native_slice(line, sp1 + 1, sp2 - sp1 - 1)
        version_arr := this.native_slice(line, sp2 + 1, n - sp2 - 1)
        i = 0
        while(i < method_arr.size()) { if(!this.native_is_token_char(method_arr[i])) { return {"ok":false,"status":400} } i += 1 }
        i = 0
        while(i < version_arr.size()) { if(version_arr[i] > 127) { return {"ok":false,"status":400} } i += 1 }
        method := bytes(method_arr).decode("utf-8")
        version := bytes(version_arr).decode("utf-8")
        if(version != "HTTP/1.1") { return {"ok":false,"status":400} }
        i = 0
        while(i < target_arr.size()) {
            c := target_arr[i]
            if(c < 32 || c == 127) { return {"ok":false,"status":400} }
            i += 1
        }
        target := bytes(target_arr).decode("utf-8")
        if(!target.starts_with("/") || target.contains("#")) { return {"ok":false,"status":400} }
        return {"ok":true,"method":method,"target":target,"version":version}
    }

    private fn(native_parse_headers(lines, max_headers, max_body_bytes)) {
        headers := {}
        content_lengths := []
        host_count := 0
        i := 1
        while(i < lines.size()) {
            if(i > max_headers) { return {"ok":false,"status":431,"headers":null,"length":0} }
            line := lines[i]
            if(line.size() == 0) { break }
            if(line[0] == 32 || line[0] == 9) { return {"ok":false,"status":400,"headers":null,"length":0} }
            colon := -1
            j := 0
            while(j < line.size()) {
                if(line[j] == 58) { colon = j; break }
                j += 1
            }
            if(colon <= 0) { return {"ok":false,"status":400,"headers":null,"length":0} }
            lower_name := []
            j = 0
            while(j < colon) {
                c := line[j]
                if(!this.native_is_token_char(c)) { return {"ok":false,"status":400,"headers":null,"length":0} }
                lower_name.push(this.native_ascii_lower(c))
                j += 1
            }
            header_name := bytes(lower_name).decode("utf-8")
            vs := colon + 1
            ve := line.size()
            while(vs < ve && (line[vs] == 32 || line[vs] == 9)) { vs += 1 }
            while(ve > vs && (line[ve - 1] == 32 || line[ve - 1] == 9)) { ve -= 1 }
            value_slice := this.native_slice(line, vs, ve - vs)
            j = 0
            while(j < value_slice.size()) {
                c := value_slice[j]
                if((c < 32 && c != 9) || c == 127) { return {"ok":false,"status":400,"headers":null,"length":0} }
                j += 1
            }
            if(!this.native_valid_utf8(value_slice)) { return {"ok":false,"status":400,"headers":null,"length":0} }
            val := bytes(value_slice).decode("utf-8")
            if(headers.has(header_name)) {
                existing := headers[header_name]
                existing.push(val)
                headers[header_name] = existing
            } else { headers[header_name] = [val] }
            if(header_name == "content-length") { content_lengths.push(val) }
            if(header_name == "host") { host_count += 1 }
            i += 1
        }
        if(content_lengths.size() > 1) { return {"ok":false,"status":400,"headers":null,"length":0} }
        length := 0
        if(content_lengths.size() == 1) {
            if(!this.native_is_digits_str(content_lengths[0])) { return {"ok":false,"status":400,"headers":null,"length":0} }
            body_digits := max_body_bytes.to_string().length()
            if(content_lengths[0].length() > body_digits) { return {"ok":false,"status":413,"headers":null,"length":0} }
            length = content_lengths[0].to_int()
        }
        if(headers.has("transfer-encoding")) { return {"ok":false,"status":501,"headers":null,"length":0} }
        if(host_count != 1 || !headers.has("host") || headers["host"][0] == "") { return {"ok":false,"status":400,"headers":null,"length":0} }
        return {"ok":true,"status":200,"headers":headers,"length":length}
    }

    private fn(native_parse_query(raw_query)) {
        query := {}
        if(raw_query == "") { return query }
        for(pair : raw_query.split("&")) {
            if(pair == "") { continue }
            eq := pair.index_of("=")
            key := pair
            value := ""
            if(eq != -1) { key = pair.substr(0, eq); value = pair.substr(eq + 1) }
            dk := this.native_unquote(key, true)
            dv := this.native_unquote(value, true)
            if(dk == null || dv == null) { return null }
            if(query.has(dk)) {
                existing := query[dk]
                if(type(existing) == "array") { existing.push(dv); query[dk] = existing }
                else { query[dk] = [existing, dv] }
            } else { query[dk] = dv }
        }
        return query
    }

    private fn(native_cookies_from_request(headers, max_cookie_pairs)) {
        cookies := {}
        count := 0
        if(!headers.has("cookie")) { return cookies }
        for(raw : headers["cookie"]) {
            for(part : raw.split(";")) {
                trimmed := part.trim()
                if(trimmed == "") { continue }
                eq := trimmed.index_of("=")
                if(eq <= 0) { continue }
                count += 1
                if(count > max_cookie_pairs) { return cookies }
                key := trimmed.substr(0, eq)
                value := trimmed.substr(eq + 1)
                if(key == "") { continue }
                if(cookies.has(key)) {
                    existing := cookies[key]
                    if(type(existing) == "array") { existing.push(value); cookies[key] = existing }
                    else { cookies[key] = [existing, value] }
                } else { cookies[key] = value }
            }
        }
        return cookies
    }

    private fn(native_build_request(method, target, headers, body_arr, remote_addr, config)) {
        qidx := target.index_of("?")
        raw_path := target
        raw_query := ""
        if(qidx != -1) {
            raw_path = target.substr(0, qidx)
            raw_query = target.substr(qidx + 1)
        }
        decoded_path := this.native_unquote(raw_path, false)
        if(decoded_path == null) { return null }
        query := this.native_parse_query(raw_query)
        if(query == null) { return null }
        segments := []
        if(raw_path != "/") {
            for(part : raw_path.split("/")) {
                if(part == "") { continue }
                dp := this.native_unquote(part, false)
                if(dp == null) { return null }
                segments.push(dp)
            }
        }
        body := {"kind":"text","text":""}
        if(body_arr.size() > 0) {
            if(this.native_valid_utf8(body_arr)) { body = {"kind":"text","text":bytes(body_arr).decode("utf-8")} }
            else { body = {"kind":"bytes","size":body_arr.size(),"data":bytes(body_arr)} }
        }
        max_cookie_pairs := this.native_config(config, "max_cookie_pairs", 128)
        request := {
            "protocol":1,
            "request_id":"native",
            "method":method,
            "target":target,
            "path":decoded_path,
            "segments":segments,
            "query":query,
            "headers":headers,
            "body":body,
            "json":null,
            "form":{},
            "cookies":this.native_cookies_from_request(headers, max_cookie_pairs),
            "remote_addr":remote_addr
        }
        return request
    }

    private fn(native_json_append(out, v)) {
        t := type(v)
        if(t == "string") {
            out.push(34)
            b := v.encode("utf-8")
            i := 0
            n := b.length()
            while(i < n) {
                c := b[i]
                handled := false
                if(c == 34 || c == 92) { out.push(92); out.push(c); handled = true }
                if(!handled && c == 10) { out.push(92); out.push(110); handled = true }
                if(!handled && c == 13) { out.push(92); out.push(114); handled = true }
                if(!handled && c == 9) { out.push(92); out.push(116); handled = true }
                if(!handled && c == 8) { out.push(92); out.push(98); handled = true }
                if(!handled && c == 12) { out.push(92); out.push(102); handled = true }
                if(!handled && c < 32) {
                    out.push(92); out.push(117); out.push(48); out.push(48)
                    hi := c / 16
                    lo := c % 16
                    if(hi >= 10) { out.push(65 + hi - 10) }
                    if(!(hi >= 10)) { out.push(48 + hi) }
                    if(lo >= 10) { out.push(65 + lo - 10) }
                    if(!(lo >= 10)) { out.push(48 + lo) }
                    handled = true
                }
                if(!handled) { out.push(c) }
                i += 1
            }
            out.push(34)
        }
        else if(t == "bool") {
            if(v) { out.push(116); out.push(114); out.push(117); out.push(101) }
            else { out.push(102); out.push(97); out.push(108); out.push(115); out.push(101) }
        }
        else if(t == "null") {
            out.push(110); out.push(117); out.push(108); out.push(108)
        }
        else if(t == "int" || t == "float") {
            out = this.native_append_str_bytes(out, v.to_string())
        }
        else if(t == "array") {
            out.push(91)
            i := 0
            while(i < v.size()) {
                if(i > 0) { out.push(44) }
                out = this.native_json_append(out, v[i])
                i += 1
            }
            out.push(93)
        }
        else if(t == "object") {
            out.push(123)
            first := true
            for((k, vv) : v) {
                if(!first) { out.push(44) }
                first = false
                out = this.native_json_append(out, k)
                out.push(58)
                out = this.native_json_append(out, vv)
            }
            out.push(125)
        }
        else {
            out.push(110); out.push(117); out.push(108); out.push(108)
        }
        return out
    }

    private fn(native_json_bytes(v)) {
        out := []
        out = this.native_json_append(out, v)
        return out
    }

    private fn(native_emit_header(out, header_name, value)) {
        if(header_name.contains("\r") || header_name.contains("\n") || value.contains("\r") || value.contains("\n")) { return null }
        out = this.native_append_str_bytes(out, header_name + ": " + value + "\r\n")
        return out
    }

    private fn(native_cookie_header(cookie)) {
        if(type(cookie) != "object" || !cookie.has("name") || !cookie.has("value")) { return null }
        line := cookie.name + "=" + cookie.value
        if(cookie.has("path")) { line += "; Path=" + cookie.path }
        if(cookie.has("domain")) { line += "; Domain=" + cookie.domain }
        if(cookie.has("max_age")) { line += "; Max-Age=" + cookie.max_age.to_string() }
        if(cookie.has("expires")) { line += "; Expires=" + cookie.expires }
        if(cookie.has("secure") && cookie.secure) { line += "; Secure" }
        if(cookie.has("http_only") && cookie.http_only) { line += "; HttpOnly" }
        if(cookie.has("same_site")) { line += "; SameSite=" + cookie.same_site }
        if(line.contains("\r") || line.contains("\n")) { return null }
        return line
    }

    private fn(native_serialize_response(request, response)) {
        status := 200
        if(type(response) == "object" && response.has("status")) { status = response.status }
        headers := {}
        if(type(response) == "object" && response.has("headers") && type(response.headers) == "object") {
            for((hn, hv) : response.headers) {
                lower_hn := hn.to_lower()
                headers[lower_hn] = hv
            }
        }
        body_bytes := []
        kind := "text"
        if(type(response) == "object" && response.has("body") && type(response.body) == "object" && response.body.has("kind")) {
            kind = response.body.kind
        }
        if(kind == "text" && type(response) == "object" && response.has("body") && type(response.body) == "object" && response.body.has("text")) {
            body_bytes = this.native_bytes_to_array(response.body.text.encode("utf-8"))
        }
        else if(kind == "json" && type(response) == "object" && response.has("body") && type(response.body) == "object" && response.body.has("value")) {
            body_bytes = this.native_json_bytes(response.body.value)
        }
        else if(kind == "bytes" && type(response) == "object" && response.has("body") && type(response.body) == "object" && response.body.has("data")) {
            body_bytes = this.native_bytes_to_array(response.body.data)
        }
        else {
            status = 501
            headers = {"content-type":["text/plain; charset=utf-8"]}
            body_bytes = this.native_bytes_to_array("native backend does not support this response body kind".encode("utf-8"))
        }
        no_body := status == 204 || status == 205 || status == 304
        if(!no_body) { headers["content-length"] = [body_bytes.size().to_string()] }
        headers["connection"] = ["close"]
        out := []
        out = this.native_append_str_bytes(out, "HTTP/1.1 " + status.to_string() + " " + this.native_reason(status) + "\r\n")
        for((hn, hv) : headers) {
            if(type(hv) == "array") {
                for(v : hv) {
                    emit := this.native_emit_header(out, hn, v)
                    if(emit == null) { return this.native_error_bytes(500, "response header contains invalid characters") }
                    out = emit
                }
            } else {
                emit := this.native_emit_header(out, hn, hv)
                if(emit == null) { return this.native_error_bytes(500, "response header contains invalid characters") }
                out = emit
            }
        }
        if(type(response) == "object" && response.has("cookies") && type(response.cookies) == "array") {
            for(cookie : response.cookies) {
                line := this.native_cookie_header(cookie)
                if(line != null) {
                    out = this.native_append_str_bytes(out, "Set-Cookie: " + line + "\r\n")
                }
            }
        }
        out = this.native_append_str_bytes(out, "\r\n")
        if(!no_body && request.method != "HEAD") { for(b : body_bytes) { out.push(b) } }
        return out
    }

    private fn(native_error_bytes(status, message)) {
        out := []
        out = this.native_append_str_bytes(out, "HTTP/1.1 " + status.to_string() + " " + this.native_reason(status) + "\r\n")
        out = this.native_append_str_bytes(out, "content-type: text/plain; charset=utf-8\r\n")
        body := this.native_bytes_to_array(message.encode("utf-8"))
        out = this.native_append_str_bytes(out, "content-length: " + body.size().to_string() + "\r\n")
        out = this.native_append_str_bytes(out, "connection: close\r\n\r\n")
        for(b : body) { out.push(b) }
        return out
    }

    private fn(native_service(app, conn, fd, config)) {
        state_key := fd.to_string()
        max_request_line := this.native_config(config, "max_request_line", 8192)
        max_header_bytes := this.native_config(config, "max_header_bytes", 32768)
        max_headers := this.native_config(config, "max_headers", 100)
        max_body_bytes := this.native_config(config, "max_body_bytes", 1048576)
        st := {}
        if(http_native_states.contains(state_key)) {
            st = http_native_states.get(state_key)
        } else {
            http_native_states.set(state_key, {"stage":"reading","buf":[],"last":epoch()})
            st = http_native_states.get(state_key)
        }
        if(st.stage == "sending") {
            pending := st["pending"]
            offset := st.get("offset")
            remaining := pending.size() - offset
            if(remaining == 0) {
                socket.close(conn)
                http_native_states.remove(state_key)
                return {"action":"done"}
            }
            chunk_count := 65536
            if(remaining < chunk_count) { chunk_count = remaining }
            chunk := this.native_slice(pending, offset, chunk_count)
            sr := socket.send_all(conn, bytes(chunk))
            if(!sr.ok) {
                st["offset"] = offset + sr.sent
                if(sr.sent >= chunk_count) { st["last"] = epoch() }
                http_native_states.set(state_key, st)
                return {"action":"keep"}
            }
            offset += chunk_count
            if(offset >= pending.size()) {
                socket.close(conn)
                http_native_states.remove(state_key)
                return {"action":"done"}
            }
            st["offset"] = offset
            st["last"] = epoch()
            http_native_states.set(state_key, st)
            return {"action":"keep"}
        }
        buf := st["buf"]
        rr := socket.recv(conn, 16384)
        if(rr.eof) {
            socket.close(conn)
            http_native_states.remove(state_key)
            return {"action":"close"}
        }
        if(!rr.ok && !rr.would_block) {
            socket.close(conn)
            http_native_states.remove(state_key)
            return {"action":"close"}
        }
        if(rr.ok) {
            for(b : this.native_bytes_to_array(rr.data)) { buf.push(b) }
            st["last"] = epoch()
            http_native_states.set(state_key, st)
        }
        term := this.native_find(buf, 0, [13,10,13,10])
        if(term == -1) {
            if(buf.size() > max_header_bytes) {
                socket.send_all(conn, bytes(this.native_error_bytes(431, "request header fields too large")))
                socket.close(conn)
                http_native_states.remove(state_key)
                return {"action":"close"}
            }
            st["buf"] = buf
            http_native_states.set(state_key, st)
            return {"action":"keep"}
        }
        header_arr := this.native_slice(buf, 0, term)
        if(header_arr.size() > max_request_line + max_header_bytes) {
            socket.send_all(conn, bytes(this.native_error_bytes(431, "request header fields too large")))
            socket.close(conn)
            http_native_states.remove(state_key)
            return {"action":"close"}
        }
        rl := {"ok":false}
        hp := {"ok":false}
        if(http_native_parse_lib != "") {
            rp := this.native_ffi_parse_head(http_native_parse_lib, http_native_parse_outbuf, this.native_slice(buf, 0, term + 4), max_headers, max_body_bytes)
            if(rp.ok) {
                rl = {"ok":true,"method":rp.method,"target":rp.target,"version":"HTTP/1.1"}
                hp = {"ok":true,"status":200,"headers":rp.headers,"length":rp.length}
            } else {
                if(rp.status == 431 || rp.status == 413 || rp.status == 501 || rp.status == 414) {
                    socket.send_all(conn, bytes(this.native_error_bytes(rp.status, "bad request")))
                } else {
                    socket.send_all(conn, bytes(this.native_error_bytes(400, "bad request")))
                }
                socket.close(conn)
                http_native_states.remove(state_key)
                return {"action":"close"}
            }
        } else {
            lines := this.native_split_crlf(header_arr)
            if(lines.size() == 0) {
                socket.send_all(conn, bytes(this.native_error_bytes(400, "bad request")))
                socket.close(conn)
                http_native_states.remove(state_key)
                return {"action":"close"}
            }
            rl = this.native_parse_request_line(lines[0], max_request_line)
            if(!rl.ok) {
                socket.send_all(conn, bytes(this.native_error_bytes(rl.status, "bad request")))
                socket.close(conn)
                http_native_states.remove(state_key)
                return {"action":"close"}
            }
            hp = this.native_parse_headers(lines, max_headers, max_body_bytes)
            if(!hp.ok) {
                socket.send_all(conn, bytes(this.native_error_bytes(hp.status, "bad request")))
                socket.close(conn)
                http_native_states.remove(state_key)
                return {"action":"close"}
            }
        }
        if(hp.length > max_body_bytes) {
            socket.send_all(conn, bytes(this.native_error_bytes(413, "request body is too large")))
            socket.close(conn)
            http_native_states.remove(state_key)
            return {"action":"close"}
        }
        body_start := term + 4
        have_body := buf.size() - body_start
        if(have_body < hp.length) {
            st["buf"] = buf
            http_native_states.set(state_key, st)
            return {"action":"keep"}
        }
        body_arr := this.native_slice(buf, body_start, hp.length)
        request := this.native_build_request(rl.method, rl.target, hp.headers, body_arr, "native", config)
        if(request == null) {
            socket.send_all(conn, bytes(this.native_error_bytes(400, "bad request")))
            socket.close(conn)
            http_native_states.remove(state_key)
            return {"action":"close"}
        }
        response := {"status":500,"headers":{},"body":{"kind":"text","text":"handler error"}}
        try {
            response = this.dispatch(app, request)
            if(type(response) == "string") { response = this.text(response) }
        } catch(e) {
            response = {"status":500,"headers":{},"body":{"kind":"text","text":"handler error"}}
        }
        out := []
        try {
            out = this.native_serialize_response(request, response)
        } catch(e) {
            out = this.native_error_bytes(500, "response serialization failed")
        }
        http_native_states.set(state_key, {"stage":"sending","pending":out,"offset":0,"last":epoch(),"start":epoch()})
        return {"action":"keep"}
    }

    private fn(native_bdecode(arr)) {
        return bytes(arr).decode("utf-8")
    }

    private fn(native_ffi_u32(b, p)) {
        a := b[p]
        e := b[p + 1]
        f := b[p + 2]
        g := b[p + 3]
        return a + (e * 256) + (f * 65536) + (g * 16777216)
    }

    private fn(native_ffi_parse_head(lib, outbuf, head, max_headers, max_body_bytes)) {
        hb := bytes(head)
        hbuf := ffi_buffer(hb)
        nn := ffi_call(lib, "http_parse", "i32(buffer,i32,buffer,i32)", hbuf, hb.length(), outbuf, 2048)
        if(nn <= 0) { return {"ok":false,"status":400} }
        out := this.native_bytes_to_array(ffi_bytes(outbuf))
        status := this.native_ffi_u32(out, 0)
        if(status != 200) { return {"ok":false,"status":status} }
        ml := this.native_ffi_u32(out, 4)
        ms := this.native_slice(out, 8, ml)
        method := this.native_bdecode(ms)
        tl := this.native_ffi_u32(out, 8 + ml)
        ts := this.native_slice(out, 12 + ml, tl)
        target := this.native_bdecode(ts)
        vp := 12 + ml + tl
        vl := this.native_ffi_u32(out, vp)
        vp = vp + 4 + vl
        hc := this.native_ffi_u32(out, vp)
        vp = vp + 4
        if(hc > max_headers) { return {"ok":false,"status":431} }
        headers := {}
        i := 0
        while(i < hc) {
            nl := this.native_ffi_u32(out, vp)
            ns := this.native_slice(out, vp + 4, nl)
            name := this.native_bdecode(ns)
            vp = vp + 4 + nl
            nvl := this.native_ffi_u32(out, vp)
            vs := this.native_slice(out, vp + 4, nvl)
            val := this.native_bdecode(vs)
            vp = vp + 4 + nvl
            headers[name] = [val]
            i += 1
        }
        cl := 0
        k := 0
        while(k < 8) {
            bb := out[vp + k]
            mult := 1
            m := 0
            while(m < k) { mult = mult * 256; m += 1 }
            cl = cl + (bb * mult)
            k += 1
        }
        if(cl > max_body_bytes) { return {"ok":false,"status":413} }
        return {"ok":true,"status":200,"method":method,"target":target,"headers":headers,"length":cl}
    }

    private fn(native_load_parser(config)) {
        lib_path := this.native_config(config, "native_parser_lib", "")
        if(lib_path == "" || !exists(lib_path)) { return }
        http_native_parse_lib = ffi_open(lib_path)
        zarr := []
        zi := 0
        while(zi < 2048) { zarr.push(0); zi += 1 }
        http_native_parse_outbuf = ffi_buffer(bytes(zarr))
    }

    private fn(native_listen(app)) {
        config := http_server_configs.get(app._server_id)
        host := this.native_config(config, "host", "127.0.0.1")
        port := this.native_config(config, "port", 0)
        backlog := this.native_config(config, "backlog", 16)
        max_requests := this.native_config(config, "max_requests", 0)
        client_timeout := this.native_config(config, "client_timeout_ms", 10000)
        max_concurrency := this.native_config(config, "max_concurrency", 16)
        poll_timeout := this.native_config(config, "poll_timeout_ms", 50)
        this.native_load_parser(config)
        if(os() == "windows" && max_concurrency > 63) { max_concurrency = 63 }
        listener := socket.listen({"host":host,"port":port,"backlog":backlog})
        if(!listener.ok) {
            return {"ok":false,"error":listener.error,"error_code":listener.error_code,"backend":"native","exit_code":1}
        }
        active := []
        served := 0
        done := false
        while(!done) {
            poll_items := []
            poll_items.push(listener.handle)
            for(a : active) { poll_items.push(a.handle) }
            pr := socket.poll(poll_items, poll_timeout)
            if(!pr.ok) {
                for(a : active) { socket.close(a.handle) }
                socket.close(listener.handle)
                return {"ok":false,"error":pr.error,"error_code":"socket_error","backend":"native","exit_code":1}
            }
            res_by_fd := {}
            i := 1
            while(i < pr.results.size()) {
                res := pr.results[i]
                res_key := res.handle.fd.to_string()
                res_by_fd[res_key] = res
                i += 1
            }
            fresh := {}
            r0 := pr.results[0]
            if(r0.readable || r0.error || r0.hangup) {
                if(max_requests == 0 || served < max_requests) {
                    if(active.size() < max_concurrency) {
                        ac := socket.accept(listener.handle)
                        while(ac.ok && active.size() < max_concurrency) {
                            active.push({"handle":ac.conn,"fd":ac.conn.fd})
                            fresh_key := ac.conn.fd.to_string()
                            fresh[fresh_key] = true
                            ac = socket.accept(listener.handle)
                        }
                    }
                }
            }
            next_active := []
            for(a : active) {
                fd := a.fd
                key := fd.to_string()
                res := {"readable":false,"writable":false,"error":false,"hangup":false}
                if(res_by_fd.has(key)) { res = res_by_fd[key] }
                stg := "reading"
                if(http_native_states.contains(key)) { stg = http_native_states.get(key).stage }
                ready := false
                if(stg == "sending") { ready = res.writable || res.error || res.hangup }
                else { ready = res.readable || res.error || res.hangup || fresh.has(key) }
                if(ready) {
                    result := this.native_service(app, a.handle, fd, config)
                    if(result.action == "done") {
                        served += 1
                        if(max_requests > 0 && served >= max_requests) { done = true }
                    }
                    if(result.action == "keep") {
                        stk := key
                        stall := false
                        if(http_native_states.contains(stk)) {
                            st2 := http_native_states.get(stk)
                            if(st2.stage == "sending" && epoch() - st2.get("start") > client_timeout) { stall = true }
                        }
                        if(stall) {
                            socket.close(a.handle)
                            http_native_states.remove(stk)
                        } else {
                            next_active.push({"handle":a.handle,"fd":fd})
                        }
                    }
                } else {
                    state_key := fd.to_string()
                    if(http_native_states.contains(state_key)) {
                        st := http_native_states.get(state_key)
                        age := epoch() - st.get("last")
                        if(st.stage == "sending" && epoch() - st.get("start") > client_timeout) { age = client_timeout + 1 }
                        if(age > client_timeout) {
                            if(st.stage == "reading") {
                                socket.send_all(a.handle, bytes(this.native_error_bytes(408, "request timeout")))
                            }
                            socket.close(a.handle)
                            http_native_states.remove(state_key)
                        } else {
                            next_active.push({"handle":a.handle,"fd":fd})
                        }
                    } else {
                        next_active.push({"handle":a.handle,"fd":fd})
                    }
                }
            }
            active = next_active
        }
        for(a : active) { socket.close(a.handle) }
        socket.close(listener.handle)
        http_native_states.clear()
        return {"ok":true,"error":"","error_code":"","backend":"native","exit_code":0}
    }
}

http := http()
export(http)
