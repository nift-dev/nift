/* Buffered RFC 4180-oriented CSV. Public API: only the csv facade. */

struct(csv) {
    private fn(fail()) {
        invalid := {}
        return invalid.csv_contract_error
    }

    private fn(default_options()) {
        return {"delimiter":",","quote":"\"","newline":"\r\n","accept_lf":true,"bom":false,"header":false,"strict_widths":false}
    }

    private fn(max_bytes()) { return 65536 }

    private fn(max_records()) { return 4096 }

    private fn(max_fields()) { return 4096 }

    private fn(max_headers()) { return 4096 }

    private fn(max_cells()) { return 16384 }

    private fn(utf8_bytes(value)) {
        if(type(value) != "string") { return this.fail() }
        return value.encode("utf-8").length()
    }

    private fn(one_character(value)) {
        if(type(value) != "string") { return false }
        return value.split("").size() == 1
    }

    private fn(normalize_options(value)) {
        if(type(value) != "object") { return this.fail() }
        allowed := ["delimiter", "quote", "newline", "line_ending", "accept_lf", "bom", "header", "strict_widths"]
        for(key : value.keys()) {
            if(!allowed.contains(key)) { return this.fail() }
        }

        result := this.default_options()
        delimiter := value.get("delimiter", result.delimiter)
        quote := value.get("quote", result.quote)
        newline := value.get("newline", result.newline)
        if(value.keys().contains("line_ending")) {
            line_ending := value.get("line_ending", result.newline)
            if(value.keys().contains("newline") && newline != line_ending) { return this.fail() }
            newline = line_ending
        }
        if(!this.one_character(delimiter) || !this.one_character(quote) || delimiter == quote) { return this.fail() }
        if(delimiter == "\r" || delimiter == "\n" || quote == "\r" || quote == "\n") { return this.fail() }
        if(newline != "\r\n" && newline != "\n") { return this.fail() }

        accept_lf := value.get("accept_lf", result.accept_lf)
        bom := value.get("bom", result.bom)
        header := value.get("header", result.header)
        strict_widths := value.get("strict_widths", result.strict_widths)
        if(type(accept_lf) != "bool" || type(bom) != "bool" || type(strict_widths) != "bool") { return this.fail() }
        if(newline == "\n" && !accept_lf) { return this.fail() }
        if(type(header) != "bool" && type(header) != "array") { return this.fail() }
        if(type(header) == "array" && header.size() <= this.max_headers()) {
            for(header_name : header) { if(type(header_name) != "string") { return this.fail() } }
        }
        return {"delimiter":delimiter,"quote":quote,"newline":newline,"accept_lf":accept_lf,"bom":bom,"header":header,"strict_widths":strict_widths}
    }

    private fn(diagnostic(code, message, record, field, level)) {
        return {"code":code,"message":message,"record":record,"field":field,"level":level}
    }

    private fn(has_error(diagnostics)) {
        for(item : diagnostics) { if(item.level == "error") { return true } }
        return false
    }

    private fn(add_record(rows, row, field_parts, token)) {
        field := field_parts.join("")
        if(row.size() == 0 && field == "" && !token) { rows.push([]) }
        else { row.push(field); rows.push(row) }
        return rows
    }

    private fn(record_cells(row, field_parts, token)) {
        if(row.size() == 0 && field_parts.size() == 0 && !token) { return 0 }
        return row.size() + 1
    }

    private fn(duplicate_header_diagnostics(headers, diagnostics)) {
        seen := set()
        i := 0
        while(i < headers.size()) {
            if(seen.contains(headers[i])) { diagnostics.push(this.diagnostic("duplicate_header", "duplicate header", 0, i, "warning")) }
            else { seen.add(headers[i]) }
            i += 1
        }
        return diagnostics
    }

    private fn(check_widths(rows, width, offset, diagnostics)) {
        i := 0
        while(i < rows.size()) {
            if(rows[i].size() != width) {
                diagnostics.push(this.diagnostic("width_mismatch", "record width does not match expected width", i + offset, rows[i].size(), "error"))
            }
            i += 1
        }
        return diagnostics
    }

    private fn(input_too_large()) {
        diagnostic := this.diagnostic("input_too_large", "CSV input exceeds 65536 UTF-8 bytes", 0, 0, "error")
        return {"ok":false,"rows":[],"headers":[],"diagnostics":[diagnostic],"empty_input":false,"terminal_newline":false}
    }

    private fn(structural_limit(code, message, record, field)) {
        diagnostic := this.diagnostic(code, message, record, field, "error")
        return {"ok":false,"rows":[],"headers":[],"diagnostics":[diagnostic],"empty_input":false,"terminal_newline":false}
    }

    private fn(parse_with_options(data, opts)) {
        if(type(data) != "string") { return this.fail() }
        if(data.encode("utf-8").length() > this.max_bytes()) { return this.input_too_large() }
        if(type(opts.header) == "array" && opts.header.size() > this.max_headers()) {
            return this.structural_limit("too_many_headers", "CSV exceeds 4096 headers", 0, this.max_headers())
        }
        diagnostics := []
        bom_text := bytes([239, 187, 191]).decode("utf-8")
        if(data.starts_with(bom_text)) {
            if(!opts.bom) { diagnostics.push(this.diagnostic("unexpected_bom", "UTF-8 BOM is not enabled", 0, 0, "error")) }
            data = data.substr(3)
        }

        chars := data.split("")
        rows := []
        row := []
        field := []
        state := "start"
        token := false
        terminal_newline := false
        parsed_cells := 0
        i := 0
        while(i < chars.size()) {
            character := chars[i]
            terminal_newline = false
            if(state == "quoted") {
                if(character == opts.quote) { state = "after_quote" }
                else if(character == "\r" && i + 1 < chars.size() && chars[i + 1] == "\n") {
                    field.push("\r")
                    field.push("\n")
                    i += 1
                }
                else { field.push(character) }
            }
            else if(state == "after_quote") {
                if(character == opts.quote) { field.push(opts.quote); state = "quoted" }
                else if(character == opts.delimiter) {
                    if(row.size() >= this.max_fields()) { return this.structural_limit("too_many_fields", "CSV record exceeds 4096 fields", rows.size(), this.max_fields()) }
                    row.push(field.join("")); field = []; token = false; state = "start"
                }
                else if(character == "\r" && i + 1 < chars.size() && chars[i + 1] == "\n") {
                    if(rows.size() >= this.max_records()) { return this.structural_limit("too_many_records", "CSV exceeds 4096 records", this.max_records(), row.size()) }
                    if(row.size() >= this.max_fields()) { return this.structural_limit("too_many_fields", "CSV record exceeds 4096 fields", rows.size(), this.max_fields()) }
                    record_cells := this.record_cells(row, field, true)
                    if(parsed_cells + record_cells > this.max_cells()) { return this.structural_limit("too_many_cells", "CSV exceeds 16384 aggregate cells", rows.size(), row.size()) }
                    parsed_cells += record_cells
                    rows = this.add_record(rows, row, field, true)
                    row = []; field = []; token = false; state = "start"; terminal_newline = true; i += 1
                }
                else if(character == "\n") {
                    if(!opts.accept_lf) { diagnostics.push(this.diagnostic("bare_lf", "bare LF is not accepted", rows.size(), row.size(), "error")) }
                    if(rows.size() >= this.max_records()) { return this.structural_limit("too_many_records", "CSV exceeds 4096 records", this.max_records(), row.size()) }
                    if(row.size() >= this.max_fields()) { return this.structural_limit("too_many_fields", "CSV record exceeds 4096 fields", rows.size(), this.max_fields()) }
                    record_cells := this.record_cells(row, field, true)
                    if(parsed_cells + record_cells > this.max_cells()) { return this.structural_limit("too_many_cells", "CSV exceeds 16384 aggregate cells", rows.size(), row.size()) }
                    parsed_cells += record_cells
                    rows = this.add_record(rows, row, field, true)
                    row = []; field = []; token = false; state = "start"; terminal_newline = true
                }
                else {
                    diagnostics.push(this.diagnostic("after_quote", "unexpected character after closing quote", rows.size(), row.size(), "error"))
                    field.push(character)
                    state = "unquoted"
                }
            }
            else if(character == opts.delimiter) {
                if(row.size() >= this.max_fields()) { return this.structural_limit("too_many_fields", "CSV record exceeds 4096 fields", rows.size(), this.max_fields()) }
                row.push(field.join("")); field = []; token = false; state = "start"
            }
            else if(character == opts.quote) {
                if(state != "start") {
                    diagnostics.push(this.diagnostic("quote_in_unquoted", "quote in unquoted field", rows.size(), row.size(), "error"))
                    field.push(character)
                }
                else { state = "quoted"; token = true }
            }
            else if(character == "\r" && i + 1 < chars.size() && chars[i + 1] == "\n") {
                if(rows.size() >= this.max_records()) { return this.structural_limit("too_many_records", "CSV exceeds 4096 records", this.max_records(), row.size()) }
                if(row.size() >= this.max_fields()) { return this.structural_limit("too_many_fields", "CSV record exceeds 4096 fields", rows.size(), this.max_fields()) }
                record_token := token || state != "start"
                record_cells := this.record_cells(row, field, record_token)
                if(parsed_cells + record_cells > this.max_cells()) { return this.structural_limit("too_many_cells", "CSV exceeds 16384 aggregate cells", rows.size(), row.size()) }
                parsed_cells += record_cells
                rows = this.add_record(rows, row, field, record_token)
                row = []; field = []; token = false; state = "start"; terminal_newline = true; i += 1
            }
            else if(character == "\n") {
                if(!opts.accept_lf) { diagnostics.push(this.diagnostic("bare_lf", "bare LF is not accepted", rows.size(), row.size(), "error")) }
                if(rows.size() >= this.max_records()) { return this.structural_limit("too_many_records", "CSV exceeds 4096 records", this.max_records(), row.size()) }
                if(row.size() >= this.max_fields()) { return this.structural_limit("too_many_fields", "CSV record exceeds 4096 fields", rows.size(), this.max_fields()) }
                record_token := token || state != "start"
                record_cells := this.record_cells(row, field, record_token)
                if(parsed_cells + record_cells > this.max_cells()) { return this.structural_limit("too_many_cells", "CSV exceeds 16384 aggregate cells", rows.size(), row.size()) }
                parsed_cells += record_cells
                rows = this.add_record(rows, row, field, record_token)
                row = []; field = []; token = false; state = "start"; terminal_newline = true
            }
            else if(character == "\r") {
                diagnostics.push(this.diagnostic("bare_cr", "bare CR is not a record ending", rows.size(), row.size(), "error"))
                field.push(character); state = "unquoted"; token = true
            }
            else { field.push(character); state = "unquoted"; token = true }
            i += 1
        }

        if(state == "quoted") { diagnostics.push(this.diagnostic("unclosed_quote", "quoted field is not closed", rows.size(), row.size(), "error")) }
        if(chars.size() > 0 && !terminal_newline) {
            if(rows.size() >= this.max_records()) { return this.structural_limit("too_many_records", "CSV exceeds 4096 records", this.max_records(), row.size()) }
            if(row.size() >= this.max_fields()) { return this.structural_limit("too_many_fields", "CSV record exceeds 4096 fields", rows.size(), this.max_fields()) }
            record_token := token || state != "start"
            record_cells := this.record_cells(row, field, record_token)
            if(parsed_cells + record_cells > this.max_cells()) { return this.structural_limit("too_many_cells", "CSV exceeds 16384 aggregate cells", rows.size(), row.size()) }
            parsed_cells += record_cells
            rows = this.add_record(rows, row, field, record_token)
        }

        headers := []
        body := rows
        width_offset := 0
        if(type(opts.header) == "array") { headers = opts.header; diagnostics = this.duplicate_header_diagnostics(headers, diagnostics) }
        else if(opts.header) {
            if(rows.size() == 0) { diagnostics.push(this.diagnostic("missing_header", "header record is missing", 0, 0, "error")) }
            else {
                headers = rows[0]
                body = []
                index := 1
                while(index < rows.size()) { body.push(rows[index]); index += 1 }
                width_offset = 1
                diagnostics = this.duplicate_header_diagnostics(headers, diagnostics)
            }
        }

        if(opts.strict_widths) {
            expected := 0
            have_expected := false
            if(headers.size() > 0 || type(opts.header) == "array" || opts.header == true) { expected = headers.size(); have_expected = true }
            else if(body.size() > 0) { expected = body[0].size(); have_expected = true }
            if(have_expected) { diagnostics = this.check_widths(body, expected, width_offset, diagnostics) }
        }
        return {"ok":!this.has_error(diagnostics),"rows":body,"headers":headers,"diagnostics":diagnostics,"empty_input":chars.size() == 0,"terminal_newline":terminal_newline}
    }

    private fn(require_ok(document)) {
        if(!document.ok) { return this.fail() }
        return document.rows
    }

    private fn(field_text(value, opts, force_quote)) {
        if(type(value) != "string") { return this.fail() }
        needs_quote := force_quote || value.contains(opts.delimiter) || value.contains(opts.quote) || value.contains("\r") || value.contains("\n")
        if(!needs_quote) { return value }
        return opts.quote + value.replace(opts.quote, opts.quote + opts.quote) + opts.quote
    }

    private fn(row_values(row, headers, header_set, object_allowed)) {
        values := []
        if(type(row) == "array") {
            if(row.size() > this.max_fields()) { return this.fail() }
            values = row
        }
        else if(type(row) == "object" && object_allowed) {
            if(row.size() > this.max_fields()) { return this.fail() }
            for(entry : row.entries()) {
                if(type(entry.value) != "string" || !header_set.contains(entry.key)) { return this.fail() }
            }
            for(header : headers) {
                value := row.get(header, "")
                if(type(value) != "string") { return this.fail() }
                values.push(value)
            }
        }
        else { return this.fail() }
        for(value : values) { if(type(value) != "string") { return this.fail() } }
        return values
    }

    private fn(values_text(values, opts)) {
        output := ""
        i := 0
        while(i < values.size()) {
            if(i > 0) { output += opts.delimiter }
            output += this.field_text(values[i], opts, values.size() == 1 && values[i] == "")
            i += 1
        }
        return output
    }

    private fn(field_output_bytes(value, opts, force_quote)) {
        value_bytes := this.utf8_bytes(value)
        needs_quote := force_quote || value.contains(opts.delimiter) || value.contains(opts.quote) || value.contains("\r") || value.contains("\n")
        if(!needs_quote) { return value_bytes }
        quote_count := value.split(opts.quote).size() - 1
        quote_bytes := this.utf8_bytes(opts.quote)
        return value_bytes + quote_count * quote_bytes + 2 * quote_bytes
    }

    private fn(row_output_bytes(values, opts, budget)) {
        total := 0
        i := 0
        while(i < values.size()) {
            if(i > 0) {
                total += this.utf8_bytes(opts.delimiter)
                if(total > budget) { return this.fail() }
            }
            total += this.field_output_bytes(values[i], opts, values.size() == 1 && values[i] == "")
            if(total > budget) { return this.fail() }
            i += 1
        }
        return total
    }

    private fn(stringify_with_options(document, opts)) {
        if(type(document) != "object") { return this.fail() }
        rows := document.get("rows", null)
        if(type(rows) != "array") { return this.fail() }
        if(rows.size() > this.max_records()) { return this.fail() }
        headers := document.get("headers", [])
        if(type(headers) != "array") { return this.fail() }
        if(headers.size() > this.max_headers()) { return this.fail() }
        effective_headers := headers
        if(type(opts.header) == "array") {
            if(opts.header.size() > this.max_headers()) { return this.fail() }
            effective_headers = opts.header
        }
        object_allowed := document.keys().contains("headers") || type(opts.header) == "array" || opts.header == true
        aggregate_cells := 0
        if(opts.header == true) {
            if(rows.size() >= this.max_records()) { return this.fail() }
            aggregate_cells = effective_headers.size()
        }
        cell_row_index := 0
        while(cell_row_index < rows.size()) {
            cell_row := rows[cell_row_index]
            if(type(cell_row) == "array") {
                if(cell_row.size() > this.max_fields()) { return this.fail() }
                aggregate_cells += cell_row.size()
            }
            else if(type(cell_row) == "object" && object_allowed) {
                if(cell_row.size() > this.max_fields()) { return this.fail() }
                aggregate_cells += effective_headers.size()
            }
            else { return this.fail() }
            if(aggregate_cells > this.max_cells()) { return this.fail() }
            cell_row_index += 1
        }
        source_bytes := 0
        header_index := 0
        while(header_index < headers.size()) {
            header_value := headers[header_index]
            if(type(header_value) != "string") { return this.fail() }
            source_bytes += this.utf8_bytes(header_value)
            if(source_bytes > this.max_bytes()) { return this.fail() }
            header_index += 1
        }
        if(type(opts.header) == "array") { headers = opts.header }
        if(type(opts.header) == "array") {
            if(opts.header.size() > this.max_headers()) { return this.fail() }
            header_index = 0
            while(header_index < opts.header.size()) {
                supplied_header := opts.header[header_index]
                source_bytes += this.utf8_bytes(supplied_header)
                if(source_bytes > this.max_bytes()) { return this.fail() }
                header_index += 1
            }
        }
        header_set := set()
        header_index = 0
        while(header_index < headers.size()) {
            header_set.add(headers[header_index])
            header_index += 1
        }
        terminal := document.get("terminal_newline", false)
        if(type(terminal) != "bool") { return this.fail() }

        expected := 0
        have_expected := false
        estimated := 0
        if(opts.bom) { estimated = 3 }
        record_count := 0
        last_empty := false
        newline_bytes := this.utf8_bytes(opts.newline)
        if(opts.header == true) {
            estimated += this.row_output_bytes(headers, opts, this.max_bytes() - estimated)
            record_count = 1
            last_empty = headers.size() == 0
        }
        if(opts.strict_widths && object_allowed) { expected = headers.size(); have_expected = true }
        normalized_rows := []
        row_index := 0
        while(row_index < rows.size()) {
            values := this.row_values(rows[row_index], headers, header_set, object_allowed)
            normalized_rows.push(values)
            value_index := 0
            while(value_index < values.size()) {
                source_value := values[value_index]
                source_bytes += this.utf8_bytes(source_value)
                if(source_bytes > this.max_bytes()) { return this.fail() }
                value_index += 1
            }
            if(opts.strict_widths) {
                if(!have_expected) { expected = values.size(); have_expected = true }
                else if(values.size() != expected) { return this.fail() }
            }
            if(record_count > 0) {
                estimated += newline_bytes
                if(estimated > this.max_bytes()) { return this.fail() }
            }
            estimated += this.row_output_bytes(values, opts, this.max_bytes() - estimated)
            record_count += 1
            last_empty = values.size() == 0
            if(estimated > this.max_bytes()) { return this.fail() }
            row_index += 1
        }
        if(record_count > 0 && last_empty) { estimated += newline_bytes }
        else if(terminal && record_count > 0) { estimated += newline_bytes }
        if(estimated > this.max_bytes()) { return this.fail() }

        records := []
        if(opts.header == true) { records.push(this.values_text(headers, opts)) }
        row_index = 0
        while(row_index < normalized_rows.size()) {
            records.push(this.values_text(normalized_rows[row_index], opts))
            row_index += 1
        }
        output := records.join(opts.newline)
        if(records.size() > 0 && records[records.size() - 1] == "") { output += opts.newline }
        if(terminal && output != "" && !output.ends_with(opts.newline)) { output += opts.newline }
        if(opts.bom) { output = bytes([239, 187, 191]).decode("utf-8") + output }
        return output
    }

    private fn(write_text(path, text)) {
        if(type(path) != "string") { return this.fail() }
        output := file(path)
        output.open("w")
        output.write(text)
        output.save()
        output.close()
        return true
    }

    fn(options()) { return this.default_options() }

    fn(parse(text)) {
        return this.require_ok(this.parse_with_options(text, this.default_options()))
    }

    fn(parse_bytes(data)) {
        if(type(data) != "bytes") { return this.fail() }
        if(data.length() > this.max_bytes()) { return this.require_ok(this.input_too_large()) }
        return this.parse(data.decode("utf-8"))
    }

    fn(parse_document(data, options)) {
        return this.parse_with_options(data, this.normalize_options(options))
    }

    fn(stringify(rows)) {
        return this.stringify_with_options({"rows":rows}, this.default_options())
    }

    fn(stringify_document(document, options)) {
        return this.stringify_with_options(document, this.normalize_options(options))
    }

    fn(read(path)) {
        if(type(path) != "string") { return this.fail() }
        data := open_bytes(path)
        if(data.length() > this.max_bytes()) { return this.require_ok(this.input_too_large()) }
        return this.parse_bytes(data)
    }

    fn(read_document(path, options)) {
        if(type(path) != "string") { return this.fail() }
        opts := this.normalize_options(options)
        data := open_bytes(path)
        if(data.length() > this.max_bytes()) { return this.input_too_large() }
        return this.parse_with_options(data.decode("utf-8"), opts)
    }

    fn(write(path, rows)) {
        return this.write_text(path, this.stringify(rows))
    }

    fn(write_document(path, document, options)) {
        return this.write_text(path, this.stringify_document(document, options))
    }
}

csv := csv()
export(csv)
