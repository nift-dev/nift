/* Strict SemVer 2.0.0 and narrow deterministic range support. */

struct(semver) {
    private fn(failure(raw, message, code)) {
        return {"valid":false,"error":message,"error_code":code,"major":null,"minor":null,"patch":null,"prerelease":[],"build":[],"raw":raw}
    }

    private fn(success(raw, major, minor, patch, prerelease, build)) {
        return {"valid":true,"error":null,"error_code":null,"major":major,"minor":minor,"patch":patch,"prerelease":prerelease,"build":build,"raw":raw}
    }

    private fn(ascii_identifier(text)) {
        allowed := "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-"
        if(text == "" || text.encode("utf-8").length() > 64) { return false }
        for(character : text.split("")) {
            if(!allowed.contains(character)) { return false }
        }
        return true
    }

    private fn(numeric(text)) {
        if(text == "") { return false }
        for(character : text.split("")) {
            if(!"0123456789".contains(character)) { return false }
        }
        return true
    }

    private fn(core_number(text)) {
        if(!this.numeric(text)) { return {"valid":false,"code":"invalid_core"} }
        if(text.length() > 1 && text.starts_with("0")) { return {"valid":false,"code":"leading_zero"} }
        if(text.length() > 10 || (text.length() == 10 && text > "2147483647")) {
            return {"valid":false,"code":"numeric_overflow"}
        }
        return {"valid":true,"value":text.to_int()}
    }

    private fn(parse_identifiers(text, prerelease)) {
        if(text == "" || text.starts_with(".") || text.ends_with(".") || text.contains("..")) {
            return {"valid":false,"code":"empty_identifier","values":[]}
        }
        values := text.split(".")
        for(value : values) {
            if(value.encode("utf-8").length() > 64) {
                return {"valid":false,"code":"identifier_too_long","values":[]}
            }
            if(!this.ascii_identifier(value)) {
                return {"valid":false,"code":"invalid_identifier","values":[]}
            }
            if(prerelease && this.numeric(value) && value.length() > 1 && value.starts_with("0")) {
                return {"valid":false,"code":"prerelease_leading_zero","values":[]}
            }
        }
        return {"valid":true,"code":null,"values":values}
    }

    private fn(compare_parsed(a, b)) {
        if(a.major < b.major) { return -1 }
        if(a.major > b.major) { return 1 }
        if(a.minor < b.minor) { return -1 }
        if(a.minor > b.minor) { return 1 }
        if(a.patch < b.patch) { return -1 }
        if(a.patch > b.patch) { return 1 }
        if(a.prerelease.length() == 0 && b.prerelease.length() == 0) { return 0 }
        if(a.prerelease.length() == 0) { return 1 }
        if(b.prerelease.length() == 0) { return -1 }

        count := a.prerelease.length()
        if(b.prerelease.length() < count) { count = b.prerelease.length() }
        i := 0
        while(i < count) {
            left := a.prerelease[i]
            right := b.prerelease[i]
            left_numeric := this.numeric(left)
            right_numeric := this.numeric(right)
            if(left_numeric && right_numeric) {
                if(left.length() < right.length()) { return -1 }
                if(left.length() > right.length()) { return 1 }
                if(left < right) { return -1 }
                if(left > right) { return 1 }
            } else if(left_numeric) { return -1 }
            else if(right_numeric) { return 1 }
            else {
                if(left < right) { return -1 }
                if(left > right) { return 1 }
            }
            i += 1
        }
        if(a.prerelease.length() < b.prerelease.length()) { return -1 }
        if(a.prerelease.length() > b.prerelease.length()) { return 1 }
        return 0
    }

    private fn(tokens(text)) {
        values := []
        current := ""
        whitespace := " \t\r\n" + bytes([11, 12]).decode("utf-8")
        for(character : text.split("")) {
            if(whitespace.contains(character)) {
                if(current != "") { values.push(current); current = "" }
            } else { current += character }
        }
        if(current != "") { values.push(current) }
        return values
    }

    private fn(wildcard(value)) {
        return value == "*" || value == "x" || value == "X"
    }

    private fn(range_term(token)) {
        if(this.wildcard(token)) { return {"valid":true,"kind":"any","versions":[]} }

        pieces := token.split(".")
        if(pieces.length() == 2 && this.wildcard(pieces[1])) {
            major := this.core_number(pieces[0])
            if(!major.valid) { return {"valid":false} }
            return {"valid":true,"kind":"major","major":major.value,"versions":[]}
        }
        if(pieces.length() == 3 && this.wildcard(pieces[2])) {
            major := this.core_number(pieces[0])
            minor := this.core_number(pieces[1])
            if(!major.valid || !minor.valid) { return {"valid":false} }
            return {"valid":true,"kind":"minor","major":major.value,"minor":minor.value,"versions":[]}
        }

        if(token.starts_with("~")) {
            body := token.substr(1)
            partial := body.split(".")
            if(partial.length() == 2) {
                major := this.core_number(partial[0])
                minor := this.core_number(partial[1])
                if(!major.valid || !minor.valid) { return {"valid":false} }
                lower := this.parse(partial[0] + "." + partial[1] + ".0")
                return {"valid":true,"kind":"tilde","version":lower,"versions":[]}
            }
            version := this.parse(body)
            if(!version.valid) { return {"valid":false} }
            return {"valid":true,"kind":"tilde","version":version,"versions":[version]}
        }

        if(token.starts_with("^")) {
            version := this.parse(token.substr(1))
            if(!version.valid) { return {"valid":false} }
            return {"valid":true,"kind":"caret","version":version,"versions":[version]}
        }

        operation := "="
        body := token
        if(token.starts_with(">=") || token.starts_with("<=")) {
            operation = token.substr(0, 2)
            body = token.substr(2)
        } else if(token.starts_with(">") || token.starts_with("<") || token.starts_with("=")) {
            operation = token.substr(0, 1)
            body = token.substr(1)
        }
        version := this.parse(body)
        if(!version.valid) { return {"valid":false} }
        return {"valid":true,"kind":"compare","operation":operation,"version":version,"versions":[version]}
    }

    private fn(parse_range(text)) {
        if(type(text) != "string") { return {"valid":false,"terms":[]} }
        if(text.encode("utf-8").length() > 1024) { return {"valid":false,"terms":[]} }
        tokens := this.tokens(text)
        if(tokens.length() == 0) { return {"valid":false,"terms":[]} }
        if(tokens.contains("||") || tokens.contains(",")) { return {"valid":false,"terms":[]} }

        if(tokens.length() == 3 && tokens[1] == "-") {
            lower := this.parse(tokens[0])
            upper := this.parse(tokens[2])
            if(!lower.valid || !upper.valid) { return {"valid":false,"terms":[]} }
            terms := []
            terms.push({"kind":"compare","operation":">=","version":lower,"versions":[lower]})
            terms.push({"kind":"compare","operation":"<=","version":upper,"versions":[upper]})
            return {"valid":true,"terms":terms}
        }
        if(tokens.contains("-") || tokens.length() > 32) { return {"valid":false,"terms":[]} }

        terms := []
        for(token : tokens) {
            if(token.contains("||") || token.contains(",") || token.contains("(") || token.contains(")")) {
                return {"valid":false,"terms":[]}
            }
            term := this.range_term(token)
            if(!term.valid) { return {"valid":false,"terms":[]} }
            terms.push(term)
        }
        return {"valid":true,"terms":terms}
    }

    private fn(term_satisfied(candidate, term)) {
        if(term.kind == "any") { return true }
        if(term.kind == "major") { return candidate.major == term.major }
        if(term.kind == "minor") { return candidate.major == term.major && candidate.minor == term.minor }
        if(term.kind == "tilde") {
            return this.compare_parsed(candidate, term.version) >= 0 && candidate.major == term.version.major && candidate.minor == term.version.minor
        }
        if(term.kind == "caret") {
            if(this.compare_parsed(candidate, term.version) < 0) { return false }
            if(term.version.major > 0) { return candidate.major == term.version.major }
            if(term.version.minor > 0) { return candidate.major == 0 && candidate.minor == term.version.minor }
            return candidate.major == 0 && candidate.minor == 0 && candidate.patch == term.version.patch
        }
        comparison := this.compare_parsed(candidate, term.version)
        if(term.operation == "=") { return comparison == 0 }
        if(term.operation == ">") { return comparison > 0 }
        if(term.operation == ">=") { return comparison >= 0 }
        if(term.operation == "<") { return comparison < 0 }
        if(term.operation == "<=") { return comparison <= 0 }
        return false
    }

    private fn(prerelease_allowed(candidate, terms)) {
        if(candidate.prerelease.length() == 0) { return true }
        for(term : terms) {
            for(version : term.versions) {
                if(version.prerelease.length() > 0 && version.major == candidate.major &&
                   version.minor == candidate.minor && version.patch == candidate.patch) { return true }
            }
        }
        return false
    }

    fn(parse(text)) {
        if(type(text) != "string") { return this.failure(null, "version must be a string", "invalid_type") }
        raw := text
        if(text.encode("utf-8").length() > 256) { return this.failure(raw, "version exceeds 256 bytes", "version_too_long") }
        if(text == "") { return this.failure(raw, "version is empty", "invalid_format") }

        plus := text.split("+")
        if(plus.length() > 2) { return this.failure(raw, "version has multiple build separators", "invalid_format") }
        main := plus[0]
        build := []
        if(plus.length() == 2) {
            parsed_build := this.parse_identifiers(plus[1], false)
            if(!parsed_build.valid) { return this.failure(raw, "invalid build metadata", parsed_build.code) }
            build = parsed_build.values
        }

        dash := main.split("-")
        core := dash[0]
        prerelease := []
        if(dash.length() > 1) {
            prerelease_text := ""
            i := 1
            while(i < dash.length()) {
                if(i > 1) { prerelease_text += "-" }
                prerelease_text += dash[i]
                i += 1
            }
            parsed_prerelease := this.parse_identifiers(prerelease_text, true)
            if(!parsed_prerelease.valid) { return this.failure(raw, "invalid prerelease", parsed_prerelease.code) }
            prerelease = parsed_prerelease.values
        }
        if(prerelease.length() + build.length() > 32) {
            return this.failure(raw, "version exceeds 32 identifiers", "too_many_identifiers")
        }

        core_parts := core.split(".")
        if(core_parts.length() != 3) { return this.failure(raw, "version core must contain three components", "invalid_core") }
        major := this.core_number(core_parts[0])
        minor := this.core_number(core_parts[1])
        patch := this.core_number(core_parts[2])
        if(!major.valid) { return this.failure(raw, "invalid major component", major.code) }
        if(!minor.valid) { return this.failure(raw, "invalid minor component", minor.code) }
        if(!patch.valid) { return this.failure(raw, "invalid patch component", patch.code) }
        return this.success(raw, major.value, minor.value, patch.value, prerelease, build)
    }

    fn(valid(text)) { return this.parse(text).valid }

    fn(compare(a, b)) {
        left := this.parse(a)
        right := this.parse(b)
        if(!left.valid || !right.valid) { return null }
        return this.compare_parsed(left, right)
    }

    fn(eq(a, b)) { result := this.compare(a, b); return result != null && result == 0 }
    fn(lt(a, b)) { result := this.compare(a, b); return result != null && result < 0 }
    fn(lte(a, b)) { result := this.compare(a, b); return result != null && result <= 0 }
    fn(gt(a, b)) { result := this.compare(a, b); return result != null && result > 0 }
    fn(gte(a, b)) { result := this.compare(a, b); return result != null && result >= 0 }

    fn(valid_range(range)) { return this.parse_range(range).valid }

    fn(satisfies(version, range)) {
        candidate := this.parse(version)
        parsed_range := this.parse_range(range)
        if(!candidate.valid || !parsed_range.valid) { return false }
        if(!this.prerelease_allowed(candidate, parsed_range.terms)) { return false }
        for(term : parsed_range.terms) {
            if(!this.term_satisfied(candidate, term)) { return false }
        }
        return true
    }
}

semver := semver()
export(semver)
