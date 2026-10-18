/* Strict, bounded command-line parsing. Public API: only the cli facade. */

struct(cli) {
    private fn(diagnostic(code, message, source, option, index)) {
        return {"code":code,"message":message,"source":source,"option":option,"index":index}
    }

    private fn(schema_failure(code, message, option)) {
        return {"ok":false,"diagnostics":[this.diagnostic(code, message, "schema", option, null)],"descriptors":{}}
    }

    private fn(argv_failure(code, message, option, index)) {
        return {"ok":false,"options":{},"present":[],"positionals":[],"diagnostics":[this.diagnostic(code, message, "argv", option, index)]}
    }

    private fn(schema_parse_failure(validation)) {
        return {"ok":false,"options":{},"present":[],"positionals":[],"diagnostics":validation.diagnostics}
    }

    private fn(ascii_letter(character)) {
        return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz".contains(character)
    }

    private fn(utf8_bytes(text)) {
        return text.encode("utf-8").length()
    }

    private fn(valid_long_name(option_name)) {
        if(type(option_name) != "string" || option_name == "" || this.utf8_bytes(option_name) > 64) { return false }
        characters := option_name.split("")
        if(!this.ascii_letter(characters[0]) || option_name.ends_with("-") || option_name.contains("--")) { return false }
        allowed := "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-"
        for(character : characters) { if(!allowed.contains(character)) { return false } }
        return true
    }

    private fn(valid_short_name(option_name)) {
        return type(option_name) == "string" && option_name.split("").size() == 1 && this.ascii_letter(option_name)
    }

    private fn(base_value_valid(value, value_type)) {
        if(value_type == "bool") { return type(value) == "bool" }
        if(value_type == "string") { return type(value) == "string" }
        return type(value) == "int"
    }

    private fn(validate_default(value, value_type, repeat, option)) {
        if(repeat) {
            if(type(value) != "array") { return this.schema_failure("invalid_default", "repeated option default must be an array", option) }
            if(value.size() > 256) { return this.schema_failure("default_too_many_values", "repeated option default exceeds 256 values", option) }
            for(item : value) {
                if(!this.base_value_valid(item, value_type)) { return this.schema_failure("invalid_default", "default value does not match option type", option) }
                if(value_type == "string" && this.utf8_bytes(item) > 4096) {
                    return this.schema_failure("default_string_too_long", "default string exceeds 4096 UTF-8 bytes", option)
                }
            }
        } else {
            if(!this.base_value_valid(value, value_type)) { return this.schema_failure("invalid_default", "default value does not match option type", option) }
            if(value_type == "string" && this.utf8_bytes(value) > 4096) {
                return this.schema_failure("default_string_too_long", "default string exceeds 4096 UTF-8 bytes", option)
            }
        }
        return {"ok":true,"diagnostics":[],"descriptors":{}}
    }

    private fn(normalize_spec(spec)) {
        if(type(spec) != "object") { return this.schema_failure("invalid_spec", "spec must be an object", null) }
        for(field : spec.keys()) {
            if(field != "options") { return this.schema_failure("unknown_spec_field", "unknown top-level spec field", null) }
        }
        if(!spec.has("options")) { return this.schema_failure("missing_options", "spec requires an options object", null) }
        options := spec.get("options", null)
        if(type(options) != "object") { return this.schema_failure("invalid_options", "spec options must be an object", null) }
        if(options.size() > 128) { return this.schema_failure("too_many_options", "spec exceeds 128 options", null) }

        descriptors := {}
        short_aliases := {}
        name_bytes := 0
        default_values := 0
        default_string_bytes := 0
        allowed_fields := ["type", "short", "required", "default", "repeat", "negatable"]
        for(option_name : options.keys()) {
            if(type(option_name) != "string" || option_name == "" || this.utf8_bytes(option_name) > 64) {
                return this.schema_failure("invalid_option_name", "option name must be 1 to 64 UTF-8 bytes", option_name)
            }
            if(!this.valid_long_name(option_name)) { return this.schema_failure("invalid_option_name", "option name has invalid syntax", option_name) }
            descriptor := options.get(option_name, null)
            if(type(descriptor) != "object") { return this.schema_failure("invalid_descriptor", "option descriptor must be an object", option_name) }
            for(field : descriptor.keys()) {
                if(!allowed_fields.contains(field)) { return this.schema_failure("unknown_descriptor_field", "unknown option descriptor field", option_name) }
            }
            if(!descriptor.has("type")) { return this.schema_failure("missing_type", "option descriptor requires type", option_name) }
            value_type := descriptor.get("type", null)
            if(value_type != "bool" && value_type != "string" && value_type != "int") {
                return this.schema_failure("invalid_type", "option type must be bool, string, or int", option_name)
            }
            has_short := descriptor.has("short")
            short := descriptor.get("short", null)
            if(has_short && !this.valid_short_name(short)) { return this.schema_failure("invalid_short", "short name must be one ASCII letter", option_name) }
            if(short != null && short_aliases.has(short)) { return this.schema_failure("duplicate_short", "short name is already in use", option_name) }
            required := descriptor.get("required", false)
            repeat := descriptor.get("repeat", false)
            negatable := descriptor.get("negatable", false)
            if(type(required) != "bool") { return this.schema_failure("invalid_required", "required must be a boolean", option_name) }
            if(type(repeat) != "bool") { return this.schema_failure("invalid_repeat", "repeat must be a boolean", option_name) }
            if(type(negatable) != "bool") { return this.schema_failure("invalid_negatable", "negatable must be a boolean", option_name) }
            has_default := descriptor.has("default")
            if(required && has_default) { return this.schema_failure("required_default", "required option cannot have a default", option_name) }
            if(negatable && value_type != "bool") { return this.schema_failure("invalid_negatable_type", "only bool options can be negatable", option_name) }
            if(has_default) {
                default_value := descriptor.get("default", null)
                checked_default := this.validate_default(default_value, value_type, repeat, option_name)
                if(!checked_default.ok) { return checked_default }
                if(repeat) { default_values += default_value.size() }
                else { default_values += 1 }
                if(default_values > 256) {
                    return this.schema_failure("default_values_budget_exceeded", "schema defaults exceed 256 aggregate values", option_name)
                }
                if(value_type == "string") {
                    if(repeat) {
                        for(item : default_value) { default_string_bytes += this.utf8_bytes(item) }
                    } else { default_string_bytes += this.utf8_bytes(default_value) }
                    if(default_string_bytes > 65536) {
                        return this.schema_failure("default_string_bytes_budget_exceeded", "schema default strings exceed 65536 aggregate UTF-8 bytes", option_name)
                    }
                }
            }
            name_bytes += this.utf8_bytes(option_name)
            if(short != null) { name_bytes += this.utf8_bytes(short); short_aliases[short] = option_name }
            if(name_bytes > 8192) { return this.schema_failure("name_budget_exceeded", "canonical and short names exceed 8192 UTF-8 bytes", option_name) }
            descriptors[option_name] = {"type":value_type,"short":short,"required":required,"repeat":repeat,"negatable":negatable,"has_default":has_default,"default":descriptor.get("default", null)}
        }
        for(option_name : options.keys()) {
            if(descriptors[option_name].negatable && descriptors.has("no-" + option_name)) {
                return this.schema_failure("negated_name_collision", "negated spelling collides with a canonical option", option_name)
            }
        }
        return {"ok":true,"diagnostics":[],"descriptors":descriptors,"short_aliases":short_aliases}
    }

    private fn(integer_shape(text)) {
        if(type(text) != "string" || text == "") { return false }
        characters := text.split("")
        start := 0
        if(characters[0] == "+" || characters[0] == "-") { start = 1 }
        if(start == characters.size()) { return false }
        i := start
        while(i < characters.size()) {
            if(!"0123456789".contains(characters[i])) { return false }
            i += 1
        }
        return true
    }

    private fn(integer_value(text)) {
        if(type(text) != "string" || text == "") { return {"ok":false,"code":"invalid_integer","value":null} }
        characters := text.split("")
        negative := characters[0] == "-"
        start := 0
        if(negative || characters[0] == "+") { start = 1 }
        if(start == characters.size()) { return {"ok":false,"code":"invalid_integer","value":null} }
        first_nonzero := characters.size()
        i := start
        while(i < characters.size()) {
            if(!"0123456789".contains(characters[i])) { return {"ok":false,"code":"invalid_integer","value":null} }
            if(first_nonzero == characters.size() && characters[i] != "0") { first_nonzero = i }
            i += 1
        }
        if(first_nonzero == characters.size()) { return {"ok":true,"code":null,"value":0} }
        normalized := text.substr(first_nonzero)
        limit := "9223372036854775807"
        if(negative) { limit = "9223372036854775808" }
        if(normalized.length() > 19 || (normalized.length() == 19 && normalized > limit)) {
            return {"ok":false,"code":"integer_overflow","value":null}
        }
        canonical := normalized
        if(negative) { canonical = "-" + normalized }
        return {"ok":true,"code":null,"value":canonical.to_int()}
    }

    private fn(long_token(token)) {
        body := token.substr(2)
        pieces := body.split("=")
        option_name := pieces[0]
        has_value := pieces.size() > 1
        if(has_value) {
            rest := []
            i := 1
            while(i < pieces.size()) { rest.push(pieces[i]); i += 1 }
            return {"name":option_name,"has_value":true,"value":rest.join("=")}
        }
        return {"name":option_name,"has_value":false,"value":null}
    }

    private fn(value_for(text, value_type)) {
        if(value_type == "string") { return {"ok":true,"code":null,"value":text} }
        return this.integer_value(text)
    }

    private fn(store(options, present, seen, option_name, descriptor, value)) {
        if(!seen.contains(option_name)) {
            seen.add(option_name)
            present.push(option_name)
            if(descriptor.repeat) { options[option_name] = [] }
        }
        if(descriptor.repeat) {
            repeated := options[option_name]
            repeated.push(value)
            options[option_name] = repeated
        } else { options[option_name] = value }
        return [options, present, seen]
    }

    fn(parse(argv, spec)) {
        validation := this.normalize_spec(spec)
        if(!validation.ok) { return this.schema_parse_failure(validation) }
        if(type(argv) != "array") { return this.argv_failure("invalid_argv", "args must be an array of strings", null, null) }
        if(argv.size() > 256) { return this.argv_failure("too_many_arguments", "argv exceeds 256 arguments", null, null) }
        total_bytes := 0
        argument_index := 0
        while(argument_index < argv.size()) {
            argv_item := argv[argument_index]
            if(type(argv_item) != "string") { return this.argv_failure("invalid_argument", "argv element must be a string", null, argument_index) }
            argument_bytes := this.utf8_bytes(argv_item)
            if(argument_bytes > 4096) { return this.argv_failure("argument_too_long", "argv element exceeds 4096 UTF-8 bytes", null, argument_index) }
            total_bytes += argument_bytes
            if(total_bytes > 65536) { return this.argv_failure("argv_too_large", "argv exceeds 65536 UTF-8 bytes", null, argument_index) }
            argument_index += 1
        }

        descriptors := validation.descriptors
        short_aliases := validation.short_aliases
        options := {}
        for(option_name : descriptors.keys()) {
            descriptor := descriptors[option_name]
            if(descriptor.has_default) { options[option_name] = descriptor.default }
            else if(descriptor.repeat) { options[option_name] = [] }
            else if(descriptor.type == "bool") { options[option_name] = false }
            else { options[option_name] = null }
        }
        present := []
        positionals := []
        seen := set()
        parsing_options := true
        i := 0
        while(i < argv.size()) {
            token := argv[i]
            if(!parsing_options) { positionals.push(token); i += 1; continue }
            if(token == "--") { parsing_options = false; i += 1; continue }
            if(token == "" || token == "-" || !token.starts_with("-")) { positionals.push(token); i += 1; continue }

            if(token.starts_with("--")) {
                parsed := this.long_token(token)
                option_name := parsed["name"]
                has_value := parsed.has_value
                negated := false
                if(!descriptors.has(option_name) && option_name.starts_with("no-")) {
                    candidate := option_name.substr(3)
                    if(descriptors.has(candidate) && descriptors[candidate].negatable) { option_name = candidate; negated = true }
                }
                if(!descriptors.has(option_name)) { return this.argv_failure("unknown_option", "unknown long option", parsed["name"], i) }
                descriptor := descriptors[option_name]
                if(seen.contains(option_name) && !descriptor.repeat) { return this.argv_failure("duplicate_option", "option may not be repeated", option_name, i) }
                if(descriptor.type == "bool") {
                    if(has_value) { return this.argv_failure("unexpected_value", "bool option does not accept a value", option_name, i) }
                    stored := this.store(options, present, seen, option_name, descriptor, !negated)
                    options = stored[0]; present = stored[1]; seen = stored[2]
                } else {
                    if(negated) { return this.argv_failure("unknown_option", "unknown long option", parsed["name"], i) }
                    if(has_value) {
                        converted := this.value_for(parsed.value, descriptor.type)
                        if(!converted.ok) { return this.argv_failure(converted.code, "invalid integer option value", option_name, i) }
                        stored := this.store(options, present, seen, option_name, descriptor, converted.value)
                        options = stored[0]; present = stored[1]; seen = stored[2]
                    } else {
                        if(i + 1 >= argv.size()) { return this.argv_failure("missing_value", "option requires a value", option_name, i) }
                        next_value := argv[i + 1]
                        if(next_value.starts_with("-") && !(descriptor.type == "int" && this.integer_shape(next_value))) {
                            return this.argv_failure("missing_value", "option requires a value", option_name, i)
                        }
                        converted := this.value_for(next_value, descriptor.type)
                        if(!converted.ok) { return this.argv_failure(converted.code, "invalid integer option value", option_name, i + 1) }
                        stored := this.store(options, present, seen, option_name, descriptor, converted.value)
                        options = stored[0]; present = stored[1]; seen = stored[2]
                        i += 1
                    }
                }
            } else {
                characters := token.split("")
                if(characters.size() != 2) { return this.argv_failure("invalid_short_form", "short options cannot be clustered or attached", null, i) }
                short := characters[1]
                if(!short_aliases.has(short)) { return this.argv_failure("unknown_option", "unknown short option", short, i) }
                option_name := short_aliases[short]
                descriptor := descriptors[option_name]
                if(seen.contains(option_name) && !descriptor.repeat) { return this.argv_failure("duplicate_option", "option may not be repeated", option_name, i) }
                if(descriptor.type == "bool") {
                    stored := this.store(options, present, seen, option_name, descriptor, true)
                    options = stored[0]; present = stored[1]; seen = stored[2]
                }
                else {
                    if(i + 1 >= argv.size()) { return this.argv_failure("missing_value", "option requires a value", option_name, i) }
                    next_value := argv[i + 1]
                    if(next_value.starts_with("-") && !(descriptor.type == "int" && this.integer_shape(next_value))) {
                        return this.argv_failure("missing_value", "option requires a value", option_name, i)
                    }
                    converted := this.value_for(next_value, descriptor.type)
                    if(!converted.ok) { return this.argv_failure(converted.code, "invalid integer option value", option_name, i + 1) }
                    stored := this.store(options, present, seen, option_name, descriptor, converted.value)
                    options = stored[0]; present = stored[1]; seen = stored[2]
                    i += 1
                }
            }
            i += 1
        }

        missing := []
        for(option_name : descriptors.keys()) {
            if(descriptors[option_name].required && !seen.contains(option_name)) {
                missing.push(this.diagnostic("missing_required", "required option was not explicitly provided", "argv", option_name, null))
            }
        }
        if(missing.size() > 0) { return {"ok":false,"options":{},"present":[],"positionals":[],"diagnostics":missing} }
        return {"ok":true,"options":options,"present":present,"positionals":positionals,"diagnostics":[]}
    }

    fn(validate_spec(spec)) {
        result := this.normalize_spec(spec)
        return {"ok":result.ok,"diagnostics":result.diagnostics}
    }
}

cli := cli()
export(cli)
