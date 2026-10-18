/* Resource-bounded deterministic text differences. */

struct(diff) {
    private fn(max_bytes()) { return 65536 }
    private fn(max_units()) { return 8192 }
    private fn(max_cells()) { return 32768 }
    private fn(max_output_bytes()) { return 131584 }

    private fn(sequence_failure(message, code)) {
        return {"ok":false,"changes":[],"error":message,"error_code":code}
    }

    private fn(sequence_success(changes)) {
        return {"ok":true,"changes":changes,"error":null,"error_code":null}
    }

    private fn(unified_failure(message, code)) {
        return {"ok":false,"text":"","error":message,"error_code":code}
    }

    private fn(unified_success(text)) {
        return {"ok":true,"text":text,"error":null,"error_code":null}
    }

    private fn(lines_tokens(text)) {
        characters := text.split("")
        values := []
        current := []
        i := 0
        while(i < characters.size()) {
            character := characters[i]
            current.push(character)
            if(character == "\r") {
                if(i + 1 < characters.size() && characters[i + 1] == "\n") {
                    current.push("\n")
                    i += 1
                }
                values.push(current.join(""))
                current = []
            } else if(character == "\n") {
                values.push(current.join(""))
                current = []
            }
            i += 1
        }
        if(current.size() > 0) { values.push(current.join("")) }
        return values
    }

    private fn(chars_tokens(text)) { return text.split("") }

    private fn(words_tokens(text)) {
        characters := text.split("")
        values := []
        if(characters.size() == 0) { return values }
        whitespace := " \t\n" + bytes([11, 12]).decode("utf-8") + "\r"
        current := []
        current_space := false
        i := 0
        while(i < characters.size()) {
            is_space := whitespace.contains(characters[i])
            if(current.size() > 0 && is_space != current_space) {
                values.push(current.join(""))
                current = []
            }
            current.push(characters[i])
            current_space = is_space
            i += 1
        }
        values.push(current.join(""))
        return values
    }

    private fn(kind_text(kind)) {
        if(kind == 0) { return "equal" }
        if(kind == 1) { return "delete" }
        return "insert"
    }

    private fn(coalesce(kinds, values)) {
        changes := []
        if(kinds.size() == 0) { return changes }
        current_kind := kinds[0]
        current_values := []
        i := 0
        while(i < kinds.size()) {
            if(kinds[i] != current_kind) {
                changes.push({"kind":this.kind_text(current_kind),"values":current_values})
                current_kind = kinds[i]
                current_values = []
            }
            current_values.push(values[i])
            i += 1
        }
        changes.push({"kind":this.kind_text(current_kind),"values":current_values})
        return changes
    }

    private fn(diff_units(before, after, common_prefix_count, common_suffix_count)) {
        operation_kinds := []
        operation_values := []
        i := 0
        while(i < common_prefix_count) {
            operation_kinds.push(0)
            operation_values.push(before[i])
            i += 1
        }

        old_count := before.size() - common_prefix_count - common_suffix_count
        new_count := after.size() - common_prefix_count - common_suffix_count
        if(old_count == 0) {
            j := 0
            while(j < new_count) {
                operation_kinds.push(2)
                operation_values.push(after[common_prefix_count + j])
                j += 1
            }
        } else if(new_count == 0) {
            j := 0
            while(j < old_count) {
                operation_kinds.push(1)
                operation_values.push(before[common_prefix_count + j])
                j += 1
            }
        } else {
            following := []
            j := 0
            while(j <= new_count) { following.push(0); j += 1 }
            directions := []
            i = old_count
            while(i > 0) {
                i -= 1
                current := []
                j = 0
                while(j <= new_count) { current.push(0); j += 1 }
                j = new_count
                while(j > 0) {
                    j -= 1
                    old_value := before[common_prefix_count + i]
                    new_value := after[common_prefix_count + j]
                    if(old_value == new_value) {
                        current[j] = following[j + 1] + 1
                        directions.push(0)
                    } else if(following[j] >= current[j + 1]) {
                        current[j] = following[j]
                        directions.push(1)
                    } else {
                        current[j] = current[j + 1]
                        directions.push(2)
                    }
                }
                following = current
            }

            i = 0
            j = 0
            while(i < old_count || j < new_count) {
                if(i == old_count) {
                    operation_kinds.push(2)
                    operation_values.push(after[common_prefix_count + j])
                    j += 1
                } else if(j == new_count) {
                    operation_kinds.push(1)
                    operation_values.push(before[common_prefix_count + i])
                    i += 1
                } else {
                    direction := directions[(old_count - 1 - i) * new_count + new_count - 1 - j]
                    if(direction == 0) {
                        operation_kinds.push(0)
                        operation_values.push(before[common_prefix_count + i])
                        i += 1
                        j += 1
                    } else if(direction == 1) {
                        operation_kinds.push(1)
                        operation_values.push(before[common_prefix_count + i])
                        i += 1
                    } else {
                        operation_kinds.push(2)
                        operation_values.push(after[common_prefix_count + j])
                        j += 1
                    }
                }
            }
        }

        suffix_index := common_suffix_count
        while(suffix_index > 0) {
            suffix_index -= 1
            operation_kinds.push(0)
            operation_values.push(before[before.size() - 1 - suffix_index])
        }
        return this.coalesce(operation_kinds, operation_values)
    }

    private fn(sequence(before, after, mode)) {
        if(type(before) != "string") { return this.sequence_failure("before must be a string", "invalid_before_type") }
        if(type(after) != "string") { return this.sequence_failure("after must be a string", "invalid_after_type") }
        if(before.encode("utf-8").length() > this.max_bytes()) { return this.sequence_failure("before exceeds 65536 UTF-8 bytes", "before_too_large") }
        if(after.encode("utf-8").length() > this.max_bytes()) { return this.sequence_failure("after exceeds 65536 UTF-8 bytes", "after_too_large") }

        old_units := []
        new_units := []
        if(mode == "lines") {
            old_units = this.lines_tokens(before)
            new_units = this.lines_tokens(after)
        } else if(mode == "chars") {
            old_units = this.chars_tokens(before)
            new_units = this.chars_tokens(after)
        } else {
            old_units = this.words_tokens(before)
            new_units = this.words_tokens(after)
        }
        if(old_units.size() > this.max_units()) { return this.sequence_failure("before exceeds 8192 units", "before_too_many_units") }
        if(new_units.size() > this.max_units()) { return this.sequence_failure("after exceeds 8192 units", "after_too_many_units") }

        common_prefix_count := 0
        shorter := old_units.size()
        if(new_units.size() < shorter) { shorter = new_units.size() }
        while(common_prefix_count < shorter && old_units[common_prefix_count] == new_units[common_prefix_count]) { common_prefix_count += 1 }
        common_suffix_count := 0
        old_left := old_units.size() - common_prefix_count
        new_left := new_units.size() - common_prefix_count
        shorter = old_left
        if(new_left < shorter) { shorter = new_left }
        while(common_suffix_count < shorter && old_units[old_units.size() - 1 - common_suffix_count] == new_units[new_units.size() - 1 - common_suffix_count]) { common_suffix_count += 1 }
        old_middle := old_units.size() - common_prefix_count - common_suffix_count
        new_middle := new_units.size() - common_prefix_count - common_suffix_count
        if(old_middle > 0 && new_middle > 0 && new_middle > (this.max_cells() / old_middle)) {
            return this.sequence_failure("unmatched middle exceeds 32768 DP cells", "work_limit_exceeded")
        }
        return this.sequence_success(this.diff_units(old_units, new_units, common_prefix_count, common_suffix_count))
    }

    private fn(label_valid(value)) {
        if(type(value) != "string" || value.encode("utf-8").length() > 256) { return false }
        controls := bytes([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 127]).decode("utf-8")
        for(character : value.split("")) {
            if(controls.contains(character)) { return false }
        }
        return true
    }

    private fn(unified_options(options)) {
        if(type(options) != "object") { return {"ok":false,"message":"options must be an object","code":"invalid_options_type"} }
        allowed := ["context", "from", "to"]
        for(key : options.keys()) {
            if(!allowed.contains(key)) { return {"ok":false,"message":"options contains an unknown field","code":"unknown_option"} }
        }
        context := options.get("context", 3)
        from := options.get("from", "a")
        to := options.get("to", "b")
        if(type(context) != "int" || context < 0 || context > 100) { return {"ok":false,"message":"context must be an integer from 0 through 100","code":"invalid_context"} }
        if(!this.label_valid(from)) { return {"ok":false,"message":"from must be a control-free string of at most 256 UTF-8 bytes","code":"invalid_from_label"} }
        if(!this.label_valid(to)) { return {"ok":false,"message":"to must be a control-free string of at most 256 UTF-8 bytes","code":"invalid_to_label"} }
        return {"ok":true,"context":context,"from":from,"to":to}
    }

    private fn(line_body(value)) {
        characters := value.split("")
        if(characters.size() > 0 && characters[characters.size() - 1] == "\n") {
            characters.pop()
            if(characters.size() > 0 && characters[characters.size() - 1] == "\r") { characters.pop() }
        } else if(characters.size() > 0 && characters[characters.size() - 1] == "\r") { characters.pop() }
        return characters.join("")
    }

    private fn(has_ending(value)) { return value.ends_with("\n") || value.ends_with("\r") }

    private fn(range_text(start, count)) {
        if(count == 1) { return start.to_string() }
        return start.to_string() + "," + count.to_string()
    }

    private fn(format_unified(changes, options)) {
        records := []
        old_line := 1
        new_line := 1
        for(change : changes) {
            for(value : change.values) {
                records.push({"kind":change.kind,"value":value,"old":old_line,"new":new_line})
                if(change.kind != "insert") { old_line += 1 }
                if(change.kind != "delete") { new_line += 1 }
            }
        }

        windows := []
        i := 0
        while(i < records.size()) {
            if(records[i].kind == "equal") { i += 1 }
            else {
                changed_start := i
                while(i < records.size() && records[i].kind != "equal") { i += 1 }
                changed_end := i
                start := changed_start - options.context
                if(start < 0) { start = 0 }
                end := changed_end + options.context
                if(end > records.size()) { end = records.size() }
                if(windows.size() > 0 && start <= windows[windows.size() - 1].end) {
                    if(end > windows[windows.size() - 1].end) {
                        last := windows.size() - 1
                        windows[last] = {"start":windows[last].start,"end":end}
                    }
                } else { windows.push({"start":start,"end":end}) }
            }
        }

        parts := ["--- ", options.from, "\n+++ ", options.to, "\n"]
        for(window : windows) {
            old_count := 0
            new_count := 0
            old_start := 0
            new_start := 0
            i = window.start
            while(i < window.end) {
                record := records[i]
                if(record.kind != "insert") {
                    if(old_count == 0) { old_start = record.old }
                    old_count += 1
                }
                if(record.kind != "delete") {
                    if(new_count == 0) { new_start = record.new }
                    new_count += 1
                }
                i += 1
            }
            if(old_count == 0) { old_start = records[window.start].old - 1 }
            if(new_count == 0) { new_start = records[window.start].new - 1 }
            parts.push("@@ -")
            parts.push(this.range_text(old_start, old_count))
            parts.push(" +")
            parts.push(this.range_text(new_start, new_count))
            parts.push(" @@\n")
            i = window.start
            while(i < window.end) {
                record := records[i]
                prefix := " "
                if(record.kind == "delete") { prefix = "-" }
                else if(record.kind == "insert") { prefix = "+" }
                parts.push(prefix)
                parts.push(this.line_body(record.value))
                parts.push("\n")
                if(!this.has_ending(record.value)) { parts.push("\\ No newline at end of file\n") }
                i += 1
            }
        }
        return parts.join("")
    }

    fn(lines(before, after)) { return this.sequence(before, after, "lines") }
    fn(chars(before, after)) { return this.sequence(before, after, "chars") }
    fn(words(before, after)) { return this.sequence(before, after, "words") }

    fn(unified(before, after, options)) {
        normalized := this.unified_options(options)
        if(!normalized.ok) { return this.unified_failure(normalized.message, normalized.code) }
        result := this.sequence(before, after, "lines")
        if(!result.ok) { return this.unified_failure(result.error, result.error_code) }
        if(result.changes.size() == 0 || (result.changes.size() == 1 && result.changes[0].kind == "equal")) {
            return this.unified_success("")
        }
        text := this.format_unified(result.changes, normalized)
        if(text.encode("utf-8").length() > this.max_output_bytes()) {
            return this.unified_failure("unified output exceeds 131584 UTF-8 bytes", "output_too_large")
        }
        return this.unified_success(text)
    }
}

diff := diff()
export(diff)
