/* Deterministic English formatting for integer values. */

struct(humanize) {
    private fn(decimal(value)) {
        if(type(value) != "int") {
            invalid := {}
            return invalid.expected_integer
        }
        return value.to_string()
    }

    private fn(parts(value)) {
        text := this.decimal(value)
        if(text.starts_with("-")) {
            return {"negative":true,"digits":text.substr(1)}
        }
        return {"negative":false,"digits":text}
    }

    private fn(group(digits)) {
        output := ""
        first := digits.length() % 3
        if(first == 0) { first = 3 }
        i := 0
        while(i < digits.length()) {
            if(i > 0 && (i - first) % 3 == 0) { output += "," }
            output += digits.substr(i, 1)
            i += 1
        }
        return output
    }

    private fn(at_least_1024(digits)) {
        if(digits.length() > 4) { return true }
        if(digits.length() < 4) { return false }
        return digits.to_int() >= 1024
    }

    private fn(divide_1024(digits)) {
        quotient := ""
        remainder := 0
        i := 0
        while(i < digits.length()) {
            current := remainder * 10 + digits.substr(i, 1).to_int()
            digit := (current / 1024).floor().to_int()
            remainder = current % 1024
            if(quotient != "" || digit != 0) { quotient += digit.to_string() }
            i += 1
        }
        if(quotient == "") { quotient = "0" }
        return {"quotient":quotient,"remainder":remainder}
    }

    private fn(increment(digits)) {
        output := ""
        carry := 1
        i := digits.length()
        while(i > 0) {
            i -= 1
            digit := digits.substr(i, 1).to_int() + carry
            if(digit == 10) { digit = 0; carry = 1 } else { carry = 0 }
            output = digit.to_string() + output
        }
        if(carry == 1) { output = "1" + output }
        return output
    }

    fn(integer(value)) {
        parsed := this.parts(value)
        sign := ""
        if(parsed.negative) { sign = "-" }
        return sign + this.group(parsed.digits)
    }

    fn(bytes(value)) {
        parsed := this.parts(value)
        digits := parsed.digits
        units := ["B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"]
        unit := 0
        remainders := []
        while(unit < units.length() - 1 && this.at_least_1024(digits)) {
            divided := this.divide_1024(digits)
            digits = divided.quotient
            remainders.push(divided.remainder)
            unit += 1
        }

        amount := digits
        if(unit > 0) {
            carry := 0
            highest_remainder := 0
            for(remainder : remainders) {
                scaled := remainder * 10 + carry
                carry = (scaled / 1024).floor().to_int()
                highest_remainder = scaled % 1024
            }
            tenth := carry
            if(highest_remainder >= 512) { tenth += 1 }
            if(tenth == 10) {
                amount = this.increment(amount)
                tenth = 0
            }
            if(amount == "1024" && tenth == 0 && unit < units.length() - 1) {
                amount = "1"
                unit += 1
            }
            if(tenth != 0) { amount += "." + tenth.to_string() }
        }
        if(parsed.negative && parsed.digits != "0") { amount = "-" + amount }
        return amount + " " + units[unit]
    }

    fn(plural(count, singular, ...plural_values)) {
        this.decimal(count)
        if(type(singular) != "string" || plural_values.length() > 1 ||
           (plural_values.length() == 1 && type(plural_values[0]) != "string")) {
            invalid := {}
            return invalid.expected_plural_arguments
        }
        word := singular + "s"
        if(plural_values.length() == 1) { word = plural_values[0] }
        if(count == 1) { word = singular }
        return this.integer(count) + " " + word
    }

    fn(ordinal(value)) {
        parsed := this.parts(value)
        tail_length := 2
        if(parsed.digits.length() < tail_length) { tail_length = parsed.digits.length() }
        tail := parsed.digits.substr(parsed.digits.length() - tail_length).to_int()
        suffix := "th"
        if(tail < 11 || tail > 13) {
            last := tail % 10
            if(last == 1) { suffix = "st" }
            if(last == 2) { suffix = "nd" }
            if(last == 3) { suffix = "rd" }
        }
        return this.integer(value) + suffix
    }
}

humanize := humanize()
export(humanize)
