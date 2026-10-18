/*
    Deterministic duration utilities for signed 64-bit integer milliseconds.
    Public API: the exported `duration` facade. All helpers are private.
*/

struct(duration) {
    private fn(integer(value)) {
        if(type(value) != "int") {
            return value.to_int(0)
        }
        return value
    }

    private fn(split(milliseconds)) {
        value := this.integer(milliseconds)
        sign := 0
        if(value < 0) { sign = -1 }
        else if(value > 0) { sign = 1 }

        days_signed := ((value - (value % 86400000)) / 86400000).to_int()
        remainder := value % 86400000
        hours_signed := ((remainder - (remainder % 3600000)) / 3600000).to_int()
        remainder = remainder % 3600000
        minutes_signed := ((remainder - (remainder % 60000)) / 60000).to_int()
        remainder = remainder % 60000
        seconds_signed := ((remainder - (remainder % 1000)) / 1000).to_int()
        milliseconds_signed := remainder % 1000

        if(sign < 0) {
            days_signed = 0 - days_signed
            hours_signed = 0 - hours_signed
            minutes_signed = 0 - minutes_signed
            seconds_signed = 0 - seconds_signed
            milliseconds_signed = 0 - milliseconds_signed
        }
        return {"sign":sign,"days":days_signed,"hours":hours_signed,"minutes":minutes_signed,"seconds":seconds_signed,"milliseconds":milliseconds_signed}
    }

    private fn(pad2(value)) {
        text := value.to_string()
        if(value < 10) { return "0" + text }
        return text
    }

    private fn(pad3(value)) {
        text := value.to_string()
        if(value < 10) { return "00" + text }
        if(value < 100) { return "0" + text }
        return text
    }

    private fn(add_part(text, value, suffix)) {
        if(value == 0) { return text }
        if(text != "") { text += " " }
        return text + value.to_string() + suffix
    }

    fn(parts(milliseconds)) {
        return this.split(milliseconds)
    }

    fn(format(milliseconds)) {
        result := this.split(milliseconds)
        text := ""
        text = this.add_part(text, result.days, "d")
        text = this.add_part(text, result.hours, "h")
        text = this.add_part(text, result.minutes, "m")
        text = this.add_part(text, result.seconds, "s")
        text = this.add_part(text, result.milliseconds, "ms")
        if(text == "") { return "0ms" }
        if(result.sign < 0) { return "-" + text }
        return text
    }

    fn(format_clock(milliseconds)) {
        result := this.split(milliseconds)
        text := ""
        if(result.days > 0) {
            text = result.days.to_string() + ":" + this.pad2(result.hours)
        } else {
            text = this.pad2(result.hours)
        }
        text += ":" + this.pad2(result.minutes)
        text += ":" + this.pad2(result.seconds)
        text += "." + this.pad3(result.milliseconds)
        if(result.sign < 0) { return "-" + text }
        return text
    }

    fn(seconds(value)) { return this.integer(value) * 1000 }
    fn(minutes(value)) { return this.integer(value) * 60000 }
    fn(hours(value)) { return this.integer(value) * 3600000 }
    fn(days(value)) { return this.integer(value) * 86400000 }
}

duration := duration()
export(duration)
