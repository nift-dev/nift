/* RFC 3986 URI-reference utilities. Public API: only the `url` facade. */

struct(url) {
    private fn(fail(message, code)) {
        return {"valid":false,"error":message,"error_code":code,"scheme":null,"has_authority":false,"userinfo":null,"host":null,"port":null,"host_is_ip_literal":false,"path":"","has_query":false,"query":null,"has_fragment":false,"fragment":null}
    }

    private fn(success(scheme, has_authority, userinfo, host, port, literal, path, has_query, query, has_fragment, fragment)) {
        return {"valid":true,"error":null,"error_code":null,"scheme":scheme,"has_authority":has_authority,"userinfo":userinfo,"host":host,"port":port,"host_is_ip_literal":literal,"path":path,"has_query":has_query,"query":query,"has_fragment":has_fragment,"fragment":fragment}
    }

    private fn(is_alpha(value)) {
        return (value >= 65 && value <= 90) || (value >= 97 && value <= 122)
    }

    private fn(is_digit(value)) { return value >= 48 && value <= 57 }

    private fn(is_hex(value)) {
        return this.is_digit(value) || (value >= 65 && value <= 70) || (value >= 97 && value <= 102)
    }

    private fn(is_unreserved(value)) {
        return this.is_alpha(value) || this.is_digit(value) || value == 45 || value == 46 || value == 95 || value == 126
    }

    private fn(is_sub_delim(value)) {
        return value == 33 || value == 36 || value == 38 || value == 39 || value == 40 || value == 41 || value == 42 || value == 43 || value == 44 || value == 59 || value == 61
    }

    private fn(slice(text, begin, end)) { return text.substr(begin, end - begin) }

    private fn(octets(value)) {
        result := []
        i := 0
        length := value.length()
        while(i < length) { result.push(value[i].to_int()); i += 1 }
        return result
    }

    private fn(within_limit(value)) { return value.length() <= 8192 }

    private fn(valid_utf8(value)) {
        source := this.octets(value)
        i := 0
        length := source.length()
        while(i < length) {
            first := source[i]
            if(first <= 127) { i += 1 }
            else if(first >= 194 && first <= 223) {
                if(i + 1 >= length || source[i + 1] < 128 || source[i + 1] > 191) { return false }
                i += 2
            } else if(first >= 224 && first <= 239) {
                if(i + 2 >= length) { return false }
                second := source[i + 1]
                third := source[i + 2]
                if(third < 128 || third > 191) { return false }
                if(first == 224 && (second < 160 || second > 191)) { return false }
                if(first == 237 && (second < 128 || second > 159)) { return false }
                if(first != 224 && first != 237 && (second < 128 || second > 191)) { return false }
                i += 3
            } else if(first >= 240 && first <= 244) {
                if(i + 3 >= length) { return false }
                second_four := source[i + 1]
                third_four := source[i + 2]
                fourth := source[i + 3]
                if(third_four < 128 || third_four > 191 || fourth < 128 || fourth > 191) { return false }
                if(first == 240 && (second_four < 144 || second_four > 191)) { return false }
                if(first == 244 && (second_four < 128 || second_four > 143)) { return false }
                if(first != 240 && first != 244 && (second_four < 128 || second_four > 191)) { return false }
                i += 4
            } else { return false }
        }
        return true
    }

    private fn(validate_component(text, kind)) {
        source := this.octets(text.encode("utf-8"))
        i := 0
        length := source.length()
        while(i < length) {
            value := source[i]
            if(value == 37) {
                if(i + 2 >= length || !this.is_hex(source[i + 1]) || !this.is_hex(source[i + 2])) {
                    return "malformed_percent_escape"
                }
                i += 3
            } else {
                allowed := this.is_unreserved(value) || this.is_sub_delim(value)
                if(kind == "userinfo") { allowed = allowed || value == 58 }
                if(kind == "path") { allowed = allowed || value == 58 || value == 64 || value == 47 }
                if(kind == "query") { allowed = allowed || value == 58 || value == 64 || value == 47 || value == 63 }
                if(kind == "host") { allowed = allowed }
                if(!allowed || value > 127) { return "invalid_character" }
                i += 1
            }
        }
        return null
    }

    private fn(valid_ipv4(text)) {
        parts := text.split(".")
        if(parts.length() != 4) { return false }
        for(part : parts) {
            part_length := part.length()
            if(part == "" || part_length > 3 || (part_length > 1 && part.starts_with("0"))) { return false }
            bytes_value := this.octets(part.encode("utf-8"))
            i := 0
            while(i < part_length) { if(!this.is_digit(bytes_value[i])) { return false }; i += 1 }
            if(part.to_int() > 255) { return false }
        }
        return true
    }

    private fn(valid_ipv6_piece(piece)) {
        if(piece == "" || piece.length() > 4) { return false }
        encoded := this.octets(piece.encode("utf-8"))
        i := 0
        length := encoded.length()
        while(i < length) { if(!this.is_hex(encoded[i])) { return false }; i += 1 }
        return true
    }

    private fn(ipv6_units(parts, allow_ipv4)) {
        units := 0
        i := 0
        length := parts.length()
        while(i < length) {
            piece := parts[i]
            if(piece == "") { return -1 }
            if(piece.contains(".")) {
                if(!allow_ipv4 || i != length - 1 || !this.valid_ipv4(piece)) { return -1 }
                units += 2
            } else {
                if(!this.valid_ipv6_piece(piece)) { return -1 }
                units += 1
            }
            i += 1
        }
        return units
    }

    private fn(valid_ip_literal(text)) {
        text_length := text.length()
        if(text_length >= 4 && (text.starts_with("v") || text.starts_with("V"))) {
            dot := text.index_of(".")
            if(dot <= 1 || dot == text_length - 1) { return false }
            i := 1
            literal_bytes := this.octets(text.encode("utf-8"))
            while(i < dot) { if(!this.is_hex(literal_bytes[i])) { return false }; i += 1 }
            tail := this.octets(text.substr(dot + 1).encode("utf-8"))
            i = 0
            tail_length := tail.length()
            while(i < tail_length) {
                byte := tail[i]
                if(!(this.is_unreserved(byte) || this.is_sub_delim(byte) || byte == 58)) { return false }
                i += 1
            }
            return true
        }

        marker := text.index_of("::")
        if(marker < 0) { return this.ipv6_units(text.split(":"), true) == 8 }
        if(text.substr(marker + 2).contains("::")) { return false }
        left_text := text.substr(0, marker)
        right_text := text.substr(marker + 2)
        left := []
        right := []
        if(left_text != "") { left = left_text.split(":") }
        if(right_text != "") { right = right_text.split(":") }
        left_units := this.ipv6_units(left, false)
        right_units := this.ipv6_units(right, true)
        return left_units >= 0 && right_units >= 0 && left_units + right_units < 8
    }

    private fn(parse_authority(text)) {
        at := -1
        at_count := 0
        i := 0
        text_length := text.length()
        while(i < text_length) {
            if(text.substr(i, 1) == "@") { at = i; at_count += 1 }
            i += 1
        }
        if(at_count > 1) { return {"ok":false,"error":"authority contains multiple raw @ delimiters","code":"invalid_authority"} }

        userinfo := [null]
        host_port := text
        if(at >= 0) {
            userinfo[0] = text.substr(0, at)
            issue := this.validate_component(userinfo[0], "userinfo")
            if(issue != null) { return {"ok":false,"error":"invalid userinfo: " + issue,"code":issue} }
            host_port = text.substr(at + 1)
        }

        host := ""
        port := [null]
        literal := false
        if(host_port.starts_with("[")) {
            close := host_port.index_of("]")
            if(close < 0) { return {"ok":false,"error":"unterminated bracketed IP literal","code":"invalid_authority"} }
            host = host_port.substr(1, close - 1)
            literal = true
            if(!this.valid_ip_literal(host)) { return {"ok":false,"error":"invalid bracketed IP literal","code":"invalid_ip_literal"} }
            remainder := host_port.substr(close + 1)
            if(remainder != "") {
                if(!remainder.starts_with(":")) { return {"ok":false,"error":"unexpected content after bracketed IP literal","code":"invalid_authority"} }
                port[0] = remainder.substr(1)
            }
        } else {
            if(host_port.contains("[") || host_port.contains("]")) { return {"ok":false,"error":"misplaced IP-literal bracket","code":"invalid_authority"} }
            pieces := host_port.split(":")
            pieces_length := pieces.length()
            if(pieces_length > 2) { return {"ok":false,"error":"IP literals must be bracketed","code":"invalid_authority"} }
            host = pieces[0]
            if(pieces_length == 2) { port[0] = pieces[1] }
            issue := this.validate_component(host, "host")
            if(issue != null) { return {"ok":false,"error":"invalid host: " + issue,"code":issue} }
        }
        if(port[0] != null) {
            encoded_port := this.octets(port[0].encode("utf-8"))
            i = 0
            port_length := encoded_port.length()
            while(i < port_length) {
                if(!this.is_digit(encoded_port[i])) { return {"ok":false,"error":"port must contain only digits","code":"invalid_port"} }
                i += 1
            }
        }
        return {"ok":true,"userinfo":userinfo[0],"host":host,"port":port[0],"literal":literal}
    }

    private fn(remove_dot_segments(path)) {
        length := path.length()
        rooted := path.starts_with("/")
        segments := []
        segment_start := 0
        if(rooted) { segment_start = 1 }
        i := segment_start
        while(i <= length) {
            if(i == length || path.substr(i, 1) == "/") {
                segment := path.substr(segment_start, i - segment_start)
                terminal := i == length
                if(segment == ".") {
                    if(terminal) { segments.push("") }
                } else if(segment == "..") {
                    segment_count := segments.length()
                    if(segment_count > 0) {
                        segments.pop()
                        if(!rooted && segment_count == 1) { rooted = true }
                    }
                    if(terminal) { segments.push("") }
                } else {
                    segments.push(segment)
                }
                segment_start = i + 1
            }
            i += 1
        }
        result := segments.join("/")
        if(rooted) { return "/" + result }
        return result
    }

    private fn(authority_text(value)) {
        parts := []
        if(value.userinfo != null) { parts.push(value.userinfo + "@") }
        if(value.host_is_ip_literal) { parts.push("[" + value.host + "]") } else { parts.push(value.host) }
        if(value.port != null) { parts.push(":" + value.port) }
        return parts.join("")
    }

    private fn(merge(base, path)) {
        base_path := base.path
        if(base.has_authority && base_path == "") { return "/" + path }
        slash := -1
        i := 0
        length := base_path.length()
        while(i < length) { if(base_path.substr(i, 1) == "/") { slash = i }; i += 1 }
        if(slash < 0) { return path }
        return base_path.substr(0, slash + 1) + path
    }

    private fn(hex_value(value)) {
        if(value >= 48 && value <= 57) { return value - 48 }
        if(value >= 65 && value <= 70) { return value - 55 }
        return value - 87
    }

    private fn(encode_text(text)) { return this.percent_encode(text.encode("utf-8")) }

    private fn(decode_text(text)) {
        decoded := this.percent_decode(text)
        if(decoded == null || !this.valid_utf8(decoded)) { return null }
        return decoded.decode("utf-8")
    }

    private fn(valid_value_shape(value)) {
        if(type(value) != "object") { return false }
        required := ["valid","error","error_code","scheme","has_authority","userinfo","host","port","host_is_ip_literal","path","has_query","query","has_fragment","fragment"]
        for(key : required) { if(!value.has(key)) { return false } }
        if(type(value.valid) != "bool" || !value.valid) { return false }
        if(value.error != null || value.error_code != null) { return false }
        if(value.scheme != null && type(value.scheme) != "string") { return false }
        if(type(value.has_authority) != "bool" || type(value.host_is_ip_literal) != "bool") { return false }
        if(type(value.path) != "string" || type(value.has_query) != "bool" || type(value.has_fragment) != "bool") { return false }
        if(value.has_authority) {
            if(type(value.host) != "string") { return false }
            if(value.userinfo != null && type(value.userinfo) != "string") { return false }
            if(value.port != null && type(value.port) != "string") { return false }
        } else {
            if(value.userinfo != null || value.host != null || value.port != null || value.host_is_ip_literal) { return false }
        }
        if(value.has_query) { if(type(value.query) != "string") { return false } }
        else if(value.query != null) { return false }
        if(value.has_fragment) { if(type(value.fragment) != "string") { return false } }
        else if(value.fragment != null) { return false }

        total := value.path.encode("utf-8").length()
        if(value.scheme != null) { total += value.scheme.encode("utf-8").length() + 1 }
        if(value.has_authority) {
            total += value.host.encode("utf-8").length() + 2
            if(value.userinfo != null) { total += value.userinfo.encode("utf-8").length() + 1 }
            if(value.port != null) { total += value.port.encode("utf-8").length() + 1 }
            if(value.host_is_ip_literal) { total += 2 }
        }
        if(value.has_query) { total += value.query.encode("utf-8").length() + 1 }
        if(value.has_fragment) { total += value.fragment.encode("utf-8").length() + 1 }
        return total <= 8192
    }

    private fn(compose(value)) {
        parts := []
        if(value.scheme != null) { parts.push(value.scheme + ":") }
        if(value.has_authority) { parts.push("//"); parts.push(this.authority_text(value)) }
        parts.push(value.path)
        if(value.has_query) { parts.push("?" + value.query) }
        if(value.has_fragment) { parts.push("#" + value.fragment) }
        return parts.join("")
    }

    private fn(same_value(first, second)) {
        if(first.scheme != second.scheme || first.has_authority != second.has_authority) { return false }
        if(first.userinfo != second.userinfo || first.host != second.host || first.port != second.port) { return false }
        if(first.host_is_ip_literal != second.host_is_ip_literal || first.path != second.path) { return false }
        if(first.has_query != second.has_query || first.query != second.query) { return false }
        if(first.has_fragment != second.has_fragment || first.fragment != second.fragment) { return false }
        return true
    }

    fn(parse(text)) {
        if(type(text) != "string") { return this.fail("URI-reference must be a string", "invalid_type") }
        encoded_text := text.encode("utf-8")
        if(!this.within_limit(encoded_text)) { return this.fail("URI-reference exceeds 8192 bytes", "input_too_large") }
        raw := this.octets(encoded_text)
        raw_index := 0
        raw_length := raw.length()
        while(raw_index < raw_length) {
            if(raw[raw_index] > 127) { return this.fail("raw non-ASCII character is not allowed", "invalid_character") }
            raw_index += 1
        }

        text_length := text.length()
        main_end := text_length
        fragment := [null]
        has_fragment := false
        hash := text.index_of("#")
        if(hash >= 0) { main_end = hash; has_fragment = true; fragment[0] = text.substr(hash + 1) }

        hierarchy_end := main_end
        query := [null]
        has_query := false
        question := text.index_of("?")
        if(question >= main_end) { question = -1 }
        if(question >= 0) { hierarchy_end = question; has_query = true; query[0] = this.slice(text, question + 1, main_end) }

        scheme := [null]
        cursor := 0
        colon := -1
        i := 0
        while(i < hierarchy_end) {
            character := text.substr(i, 1)
            if(character == ":") { colon = i; i = hierarchy_end }
            else if(character == "/") { i = hierarchy_end }
            else { i += 1 }
        }
        if(colon >= 0) {
            alpha := "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
            scheme_characters := alpha + "0123456789+-."
            if(colon == 0 || !alpha.contains(text.substr(0, 1))) { return this.fail("invalid URI scheme", "invalid_scheme") }
            i = 1
            while(i < colon) {
                if(!scheme_characters.contains(text.substr(i, 1))) { return this.fail("invalid URI scheme", "invalid_scheme") }
                i += 1
            }
            scheme[0] = text.substr(0, colon)
            cursor = colon + 1
        }

        has_authority := false
        userinfo := [null]
        host := [null]
        port := [null]
        literal := false
        if(cursor + 1 < hierarchy_end && text.substr(cursor, 2) == "//") {
            has_authority = true
            authority_start := cursor + 2
            authority_end := authority_start
            while(authority_end < hierarchy_end && text.substr(authority_end, 1) != "/") { authority_end += 1 }
            parsed_authority := this.parse_authority(this.slice(text, authority_start, authority_end))
            if(!parsed_authority.ok) { return this.fail(parsed_authority.error, parsed_authority.code) }
            userinfo[0] = parsed_authority.userinfo
            host[0] = parsed_authority.host
            port[0] = parsed_authority.port
            literal = parsed_authority.literal
            cursor = authority_end
        }

        path := this.slice(text, cursor, hierarchy_end)
        path_issue := this.validate_component(path, "path")
        if(path_issue != null) { return this.fail("invalid path: " + path_issue, path_issue) }
        if(has_authority && path != "" && !path.starts_with("/")) { return this.fail("authority path must be empty or absolute", "invalid_path") }
        if(has_query) { query_issue := this.validate_component(query[0], "query"); if(query_issue != null) { return this.fail("invalid query: " + query_issue, query_issue) } }
        if(has_fragment) { fragment_issue := this.validate_component(fragment[0], "query"); if(fragment_issue != null) { return this.fail("invalid fragment: " + fragment_issue, fragment_issue) } }
        return this.success(scheme[0], has_authority, userinfo[0], host[0], port[0], literal, path, has_query, query[0], has_fragment, fragment[0])
    }

    fn(stringify(value)) {
        if(!this.valid_value_shape(value)) { return null }
        result := this.compose(value)
        reparsed := this.parse(result)
        if(!reparsed.valid || !this.same_value(value, reparsed)) { return null }
        return result
    }

    fn(resolve(base, reference)) {
        base_text := this.stringify(base)
        if(base_text == null) { return this.fail("base must be a valid parsed URI", "invalid_base") }
        clean_base := base
        if(clean_base.scheme == null) { return this.fail("base URI must have a scheme", "invalid_base") }
        relative := this.parse(reference)
        if(!relative.valid) { return relative }

        scheme := clean_base.scheme
        has_authority := false
        userinfo := [null]
        host := [null]
        port := [null]
        literal := false
        path := ""
        has_query := false
        query := [null]
        if(relative.scheme != null) {
            scheme = relative.scheme; has_authority = relative.has_authority; userinfo[0] = relative.userinfo; host[0] = relative.host; port[0] = relative.port; literal = relative.host_is_ip_literal
            path = this.remove_dot_segments(relative.path); has_query = relative.has_query; query[0] = relative.query
        } else if(relative.has_authority) {
            has_authority = true; userinfo[0] = relative.userinfo; host[0] = relative.host; port[0] = relative.port; literal = relative.host_is_ip_literal
            path = this.remove_dot_segments(relative.path); has_query = relative.has_query; query[0] = relative.query
        } else {
            has_authority = clean_base.has_authority; userinfo[0] = clean_base.userinfo; host[0] = clean_base.host; port[0] = clean_base.port; literal = clean_base.host_is_ip_literal
            if(relative.path == "") {
                path = clean_base.path
                if(relative.has_query) { has_query = true; query[0] = relative.query } else { has_query = clean_base.has_query; query[0] = clean_base.query }
            } else {
                if(relative.path.starts_with("/")) { path = this.remove_dot_segments(relative.path) }
                else { path = this.remove_dot_segments(this.merge(clean_base, relative.path)) }
                has_query = relative.has_query; query[0] = relative.query
            }
        }
        target := this.success(scheme, has_authority, userinfo[0], host[0], port[0], literal, path, has_query, query[0], relative.has_fragment, relative.fragment)
        target_text := this.compose(target)
        if(!this.within_limit(target_text.encode("utf-8"))) { return this.fail("resolved URI exceeds 8192 bytes", "output_too_large") }
        canonical_target := this.parse(target_text)
        if(!canonical_target.valid) { return this.fail("resolved URI is invalid", "invalid_result") }
        return canonical_target
    }

    fn(resolve_string(base, reference)) {
        parsed_base := this.parse(base)
        if(!parsed_base.valid) { return null }
        resolved := this.resolve(parsed_base, reference)
        return this.stringify(resolved)
    }

    fn(percent_encode(value)) {
        if(type(value) != "bytes" || !this.within_limit(value)) { return null }
        source := this.octets(value)
        hex := "0123456789ABCDEF"
        output := []
        i := 0
        length := source.length()
        while(i < length) {
            byte := source[i]
            if(this.is_unreserved(byte)) { output.push(bytes([byte]).decode("utf-8")) }
            else { output.push("%" + hex.substr((byte / 16).floor().to_int(), 1) + hex.substr(byte % 16, 1)) }
            i += 1
        }
        return output.join("")
    }

    fn(percent_decode(value)) {
        if(type(value) != "string") { return null }
        encoded_value := value.encode("utf-8")
        if(!this.within_limit(encoded_value)) { return null }
        source := this.octets(encoded_value)
        output := []
        i := 0
        length := source.length()
        while(i < length) {
            byte := source[i]
            if(byte == 37) {
                if(i + 2 >= length || !this.is_hex(source[i + 1]) || !this.is_hex(source[i + 2])) { return null }
                output.push(this.hex_value(source[i + 1]) * 16 + this.hex_value(source[i + 2]))
                i += 3
            } else { output.push(byte); i += 1 }
        }
        return bytes(output)
    }

    fn(encode(text)) {
        if(type(text) != "string") { return null }
        encoded_text := text.encode("utf-8")
        if(!this.within_limit(encoded_text)) { return null }
        return this.percent_encode(encoded_text)
    }

    fn(decode(text)) {
        if(type(text) != "string") { return null }
        return this.decode_text(text)
    }

    fn(parse_query(query)) {
        if(type(query) != "string" || !this.within_limit(query.encode("utf-8"))) { return null }
        result := []
        if(query == "") { return result }
        for(field : query.split("&")) {
            equals := field.index_of("=")
            name_text := field
            has_value := false
            value_text := [null]
            if(equals >= 0) { name_text = field.substr(0, equals); has_value = true; value_text[0] = field.substr(equals + 1) }
            decoded_name := this.decode_text(name_text)
            if(decoded_name == null) { return null }
            value := [null]
            if(has_value) { value[0] = this.decode_text(value_text[0]); if(value[0] == null) { return null } }
            result.push({"name":decoded_name,"has_value":has_value,"value":value[0]})
        }
        return result
    }

    fn(build_query(parameters)) {
        if(type(parameters) != "array") { return null }
        fields := []
        source_size := 0
        i := 0
        parameter_count := parameters.length()
        while(i < parameter_count) {
            parameter := parameters[i]
            if(type(parameter) != "object") { return null }
            if(!parameter.has("name") || !parameter.has("has_value") || !parameter.has("value")) { return null }
            if(type(parameter.name) != "string" || type(parameter.has_value) != "bool") { return null }
            name_size := parameter.name.encode("utf-8").length()
            source_size += name_size
            if(i > 0) { source_size += 1 }
            if(parameter.has_value) {
                if(type(parameter.value) != "string") { return null }
                source_size += parameter.value.encode("utf-8").length() + 1
            } else if(parameter.value != null) { return null }
            if(source_size > 8192) { return null }
            field := this.encode_text(parameter.name)
            if(parameter.has_value) { field += "=" + this.encode_text(parameter.value) }
            fields.push(field)
            i += 1
        }
        return fields.join("&")
    }
}

url := url()
export(url)
