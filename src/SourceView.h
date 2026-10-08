#pragma once

#include "Diagnostic.h"
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace nift::detail {

// Diagnostic coordinates only. Source authority/provenance remains independent.
class SourceDocument {
public:
    const std::filesystem::path path;
    const std::string text;
    const std::vector<std::size_t> line_starts;

    SourceDocument(std::filesystem::path path_, std::string text_)
        : path(std::move(path_)), text(std::move(text_)), line_starts(index(text)) {}

    DiagnosticOrigin locate(std::size_t offset, std::size_t length = 1) const {
        offset = std::min(offset, text.size());
        auto line = std::upper_bound(line_starts.begin(), line_starts.end(), offset);
        const auto number = static_cast<std::size_t>(line - line_starts.begin());
        const auto start = line_starts[number - 1];
        auto end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        if (end > start && text[end - 1] == '\r') --end;
        return {path, number, offset - start + 1,
                std::max<std::size_t>(1, std::min(length, end > offset ? end - offset : 0)),
                text.substr(start, end - start)};
    }

private:
    static std::vector<std::size_t> index(const std::string& value) {
        std::vector<std::size_t> starts{0};
        for (std::size_t i = 0; i < value.size(); ++i)
            if (value[i] == '\n') starts.push_back(i + 1);
        return starts;
    }
};

struct SourceSpanMapping {
    std::size_t begin = 0, length = 0, original = 0, original_length = 0;
    bool anchored = false;
};

// Every view maps directly to original bytes; composition never stacks deltas.
class SourceView {
public:
    SourceView() = default;
    static SourceView identity(std::filesystem::path path, std::string text) {
        auto document = std::make_shared<const SourceDocument>(std::move(path), std::move(text));
        const auto size = document->text.size();
        return SourceView(std::move(document), {}, 0, size, size);
    }
    static SourceView mapped(std::shared_ptr<const SourceDocument> document,
                             std::vector<SourceSpanMapping> spans,
                             std::size_t size, std::size_t eof) {
        return SourceView(std::move(document),
                          std::make_shared<const std::vector<SourceSpanMapping>>(std::move(spans)),
                          0, size, eof);
    }
    explicit operator bool() const { return static_cast<bool>(document_); }
    const std::shared_ptr<const SourceDocument>& document() const { return document_; }
    std::size_t size() const { return size_; }
    std::size_t span_count() const { return spans_ ? spans_->size() : (size_ ? 1 : 0); }
    std::size_t mapping_bytes() const { return spans_ ? spans_->size() * sizeof(SourceSpanMapping) : 0; }

    std::pair<std::size_t, std::size_t> original_range(std::size_t offset,
                                                     std::size_t length = 1) const {
        offset = std::min(offset, size_);
        if (!spans_) return {base_ + offset, std::min(length, size_ - offset)};
        if (offset == size_) return {eof_, 0};
        auto it = std::upper_bound(spans_->begin(), spans_->end(), offset,
            [](std::size_t n, const SourceSpanMapping& span) { return n < span.begin; });
        if (it == spans_->begin()) return {eof_, 0};
        --it;
        if (it->anchored) return {it->original, it->original_length};
        return {it->original + offset - it->begin,
                std::min(length, it->begin + it->length - offset)};
    }
    DiagnosticOrigin locate(std::size_t offset, std::size_t length = 1) const {
        if (!document_) return {};
        const auto range = original_range(offset, length);
        return document_->locate(range.first, range.second);
    }
    SourceView slice(std::size_t start, std::size_t length = std::string::npos) const {
        start = std::min(start, size_);
        length = std::min(length, size_ - start);
        if (!spans_) return SourceView(document_, {}, base_ + start, length, base_ + start + length);
        std::vector<SourceSpanMapping> clipped;
        for (const auto& span : *spans_) {
            const auto begin = std::max(start, span.begin);
            const auto end = std::min(start + length, span.begin + span.length);
            if (begin >= end) continue;
            clipped.push_back({begin - start, end - begin,
                span.original + (span.anchored ? 0 : begin - span.begin),
                span.anchored ? span.original_length : end - begin, span.anchored});
        }
        return mapped(document_, std::move(clipped), length, original_range(start + length, 0).first);
    }
    SourceView trim(std::string_view text) const {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) return slice(text.size(), 0);
        return slice(first, text.find_last_not_of(" \t\r\n") - first + 1);
    }
    std::vector<SourceSpanMapping> spans() const {
        if (spans_) return *spans_;
        if (!size_) return {};
        return {{0, size_, base_, size_, false}};
    }

private:
    std::shared_ptr<const SourceDocument> document_;
    std::shared_ptr<const std::vector<SourceSpanMapping>> spans_;
    std::size_t base_ = 0, size_ = 0, eof_ = 0;
    SourceView(std::shared_ptr<const SourceDocument> document,
               std::shared_ptr<const std::vector<SourceSpanMapping>> spans,
               std::size_t base, std::size_t size, std::size_t eof)
        : document_(std::move(document)), spans_(std::move(spans)), base_(base), size_(size), eof_(eof) {}
};

struct MappedSource { std::string text; SourceView view; };

class SourceBuilder {
public:
    explicit SourceBuilder(SourceView input) : input_(std::move(input)) {}
    void copy(std::string_view text, std::size_t start, std::size_t length) {
        append(text.substr(start, length), input_.slice(start, length));
    }
    void generated(std::string_view text, std::size_t anchor, std::size_t length = 1) {
        if (text.empty()) return;
        const auto range = input_.original_range(anchor, length);
        add({text_.size(), text.size(), range.first, range.second, true});
        text_.append(text);
    }
    void append(std::string_view text, const SourceView& view) {
        if (view.document() != input_.document() || text.size() != view.size())
            throw std::logic_error("source mapping append mismatch");
        const auto base = text_.size();
        for (auto span : view.spans()) { span.begin += base; add(span); }
        text_.append(text);
    }
    MappedSource finish() && {
        const auto size = text_.size();
        auto view = SourceView::mapped(input_.document(), std::move(spans_), size,
                                      input_.original_range(input_.size(), 0).first);
        return {std::move(text_), std::move(view)};
    }
private:
    SourceView input_;
    std::string text_;
    std::vector<SourceSpanMapping> spans_;
    void add(SourceSpanMapping span) {
        if (!span.length) return;
        if (!spans_.empty()) {
            auto& last = spans_.back();
            if (last.begin + last.length == span.begin && last.anchored == span.anchored &&
                ((last.anchored && last.original == span.original && last.original_length == span.original_length) ||
                 (!last.anchored && last.original + last.length == span.original))) {
                last.length += span.length;
                if (!last.anchored) last.original_length = last.length;
                return;
            }
        }
        spans_.push_back(span);
    }
};
} // namespace nift::detail
