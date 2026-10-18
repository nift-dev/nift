/*
    Deterministic, case-insensitive Levenshtein matching over UTF-8 bytes.
    Public API: the exported `fuzzy` facade. All helpers are private.

    Work is bounded so hostile inputs cannot hang the host:

      - a single distance computation is limited to fuzzy_max_work dynamic
        program cells (about 500 x 500 compared characters);
      - `matches`/`filter` use a bounded banded Levenshtein whose cost scales
        with the query length times the allowed edit-band, not full O(m*n);
      - `filter`/`rank` cap the number of candidates and the cumulative work.

    Inputs that exceed a bound raise the same controlled error the package uses
    for malformed input, deterministically.
*/

fuzzy_max_work := 250000
fuzzy_aggregate_work := 1000000
fuzzy_max_candidates := 1000

struct(fuzzy) {
    private fn(fail()) {
        invalid_value := null
        return invalid_value.length()
    }

    private fn(string(value)) {
        if(type(value) != "string") { return this.fail() }
        return value
    }

    private fn(limit(options)) {
        if(options.size() > 1) { return this.fail() }
        maximum := 2
        if(options.size() == 1) { maximum = options[0] }
        if(type(maximum) != "int" || maximum < 0) { return this.fail() }
        return maximum
    }

    private fn(candidate_list(candidates)) {
        if(type(candidates) != "array") { return this.fail() }
        for(candidate : candidates) { this.string(candidate) }
        return candidates
    }

    private fn(fold(value)) {
        source := value.encode("utf-8")
        folded := []
        i := 0
        while(i < source.length()) {
            byte := source[i].to_int()
            if(byte >= 65 && byte <= 90) { byte += 32 }
            folded.push(byte)
            i += 1
        }
        return folded
    }

    private fn(banded_work(first_length, second_length, maximum)) {
        band := second_length
        two_k := maximum * 2 + 3
        if(two_k < band) { band = two_k }
        if(band < 1) { band = 1 }
        return first_length * band
    }

    private fn(distance_folded(first, second)) {
        if(first.length() * second.length() > fuzzy_max_work) { return this.fail() }
        previous := []
        j := 0
        while(j <= second.length()) { previous.push(j); j += 1 }

        i := 1
        while(i <= first.length()) {
            current := [i]
            j = 1
            while(j <= second.length()) {
                deletion := previous[j] + 1
                insertion := current[j - 1] + 1
                substitution := previous[j - 1]
                if(first[i - 1] != second[j - 1]) { substitution += 1 }
                best := deletion
                if(insertion < best) { best = insertion }
                if(substitution < best) { best = substitution }
                current.push(best)
                j += 1
            }
            previous = current
            i += 1
        }
        return previous[second.length()] + 0
    }

    private fn(bounded(first, second, maximum)) {
        m := first.length()
        n := second.length()
        if(m > n + maximum || n > m + maximum) { return false }
        big := m + n + maximum + 1
        previous := []
        j := 0
        while(j <= n) { previous.push(j); j += 1 }

        i := 1
        while(i <= m) {
            current := []
            j = 0
            while(j <= n) { current.push(big); j += 1 }
            current[0] = i
            low := i - maximum
            if(low < 1) { low = 1 }
            high := i + maximum
            if(high > n) { high = n }
            j = low
            while(j <= high) {
                substitution := 0
                if(first[i - 1] != second[j - 1]) { substitution = 1 }
                best := previous[j] + 1
                if(current[j - 1] + 1 < best) { best = current[j - 1] + 1 }
                if(previous[j - 1] + substitution < best) { best = previous[j - 1] + substitution }
                current[j] = best
                j += 1
            }
            previous = current
            i += 1
        }
        return previous[n] <= maximum
    }

    private fn(insert_ranked(ranked, item)) {
        result := []
        inserted := false
        for(existing : ranked) {
            if(!inserted && item.distance < existing.distance) {
                result.push(item)
                inserted = true
            }
            result.push(existing)
        }
        if(!inserted) { result.push(item) }
        return result
    }

    fn(distance(a, b)) {
        this.string(a)
        this.string(b)
        return this.distance_folded(this.fold(a), this.fold(b))
    }

    fn(matches(query, candidate, ...options)) {
        maximum := this.limit(options)
        this.string(query)
        this.string(candidate)
        qf := this.fold(query)
        cf := this.fold(candidate)
        if(this.banded_work(qf.length(), cf.length(), maximum) > fuzzy_max_work) { return this.fail() }
        return this.bounded(qf, cf, maximum)
    }

    fn(filter(query, candidates, ...options)) {
        this.string(query)
        this.candidate_list(candidates)
        if(candidates.size() > fuzzy_max_candidates) { return this.fail() }
        maximum := this.limit(options)
        qf := this.fold(query)
        work := 0
        result := []
        for(candidate : candidates) {
            cf := this.fold(candidate)
            cells := this.banded_work(qf.length(), cf.length(), maximum)
            if(cells > fuzzy_max_work) { return this.fail() }
            work += cells
            if(work > fuzzy_aggregate_work) { return this.fail() }
            if(this.bounded(qf, cf, maximum)) { result.push(candidate) }
        }
        return result
    }

    fn(rank(query, candidates)) {
        this.string(query)
        this.candidate_list(candidates)
        if(candidates.size() > fuzzy_max_candidates) { return this.fail() }
        qf := this.fold(query)
        work := 0
        ranked := []
        for(candidate : candidates) {
            cf := this.fold(candidate)
            cells := qf.length() * cf.length()
            if(cells > fuzzy_max_work) { return this.fail() }
            work += cells
            if(work > fuzzy_aggregate_work) { return this.fail() }
            item := {"candidate":candidate,"distance":this.distance_folded(qf, cf)}
            ranked = this.insert_ranked(ranked, item)
        }
        return ranked
    }
}

fuzzy := fuzzy()
export(fuzzy)