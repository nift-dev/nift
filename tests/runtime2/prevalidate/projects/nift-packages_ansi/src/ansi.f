/*
    Deterministic ANSI SGR formatting for Nift.

    Public API: the exported `ansi` facade. Helpers and module state stay private.
*/

ansi_csi := bytes([27]).decode("utf-8") + "["

struct(ansi) {
    private fn(normalize(codes)) {
        normalized := []
        if(type(codes) == "int") { normalized.push(codes) }
        else if(type(codes) == "array") {
            for(code : codes) { normalized.push(code) }
        }
        else { return null }
        if(normalized.size() == 0) { return null }
        for(code : normalized) {
            if(type(code) != "int" || code < 0 || code > 255) { return null }
        }
        return normalized
    }

    private fn(sequence(codes)) {
        result := ansi_csi
        separator := ""
        for(code : codes) {
            result += separator + code.to_string()
            separator = ";"
        }
        return result + "m"
    }

    private fn(styled(text, requested_codes)) {
        if(type(text) != "string") { return null }
        codes := this.normalize(requested_codes)
        if(codes == null) { return null }
        return this.sequence(codes) + text + this.sequence([0])
    }

    fn(sgr(requested_codes)) {
        codes := this.normalize(requested_codes)
        if(codes == null) { return null }
        return this.sequence(codes)
    }

    fn(wrap(text, codes)) { return this.styled(text, codes) }
    fn(reset()) { return this.sequence([0]) }
    fn(bold(text)) { return this.styled(text, [1]) }
    fn(dim(text)) { return this.styled(text, [2]) }
    fn(italic(text)) { return this.styled(text, [3]) }
    fn(underline(text)) { return this.styled(text, [4]) }
    fn(black(text)) { return this.styled(text, [30]) }
    fn(red(text)) { return this.styled(text, [31]) }
    fn(green(text)) { return this.styled(text, [32]) }
    fn(yellow(text)) { return this.styled(text, [33]) }
    fn(blue(text)) { return this.styled(text, [34]) }
    fn(magenta(text)) { return this.styled(text, [35]) }
    fn(cyan(text)) { return this.styled(text, [36]) }
    fn(white(text)) { return this.styled(text, [37]) }
}

ansi := ansi()
export(ansi)
