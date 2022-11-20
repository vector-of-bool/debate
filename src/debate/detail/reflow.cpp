#include "./reflow.hpp"

#include <neo/generator.hpp>
#include <neo/ranges.hpp>
#include <neo/tl.hpp>
#include <neo/tokenize.hpp>

#include <ranges>

namespace stdv = std::views;
using namespace std::literals;
using namespace debate;

namespace {

neo::generator<std::string_view>
printable_tokens(auto words, std::string_view indent, std::size_t column_limit) {
    std::size_t col  = indent.size();
    const auto  end  = words.end();
    auto        iter = words.begin();
    if (iter != end) {
        // If the paragraph is empty, there's nothing to indent
        co_yield indent;
    }
    while (iter != end) {
        std::string_view word = neo::view_text(*iter);
        // If we're about to overflow a paragraph
        if (word.size() + col > column_limit
            // (And we aren't at the beginning of a new line)
            and (col != indent.size())) {
            // Yield a newline and another indentation
            co_yield "\n"sv;
            co_yield indent;
            col = indent.size();
        }
        // yield the word
        co_yield word;
        col += word.size();
        bool is_sentence_end = word.ends_with(".");
        ++iter;
        if (iter != end) {
            if (is_sentence_end) {
                // Double-space the ends of sentences
                co_yield "  "sv;
                col += 2;
            } else {
                // Single space after every other word
                co_yield " ";
                col += 1;
            }
        }
    }
}

constexpr auto split_words = [](auto string) {
    return neo::tokenizer{NEO_FWD(string), neo::whitespace_splitter{}}
    // The tokenizer yields tokens, but we only care about the text view
    // of the token:
    | stdv::transform(NEO_TL(_1.view));
};

}  // namespace

std::string debate::detail::reflow_text(std::string_view const given,
                                        std::string_view const indent,
                                        std::size_t            column_limit) noexcept {
    const auto trimmed = neo::trim(given);
    auto       tokens =
        // Iterate each input line
        neo::iter_lines(trimmed)
        // Trim leading/trailing whitespace on each line
        | stdv::transform(neo::trim)
        // If a line is completely blank we consider it to be a paragraph
        // separator. Re-group the lines as paragraphs of lines.
        | stdv::split(""sv)
        // For each paragraph of lines, split each line into a range of words
        | stdv::transform(stdv::transform(split_words))
        // For each paragraph of ranges of words, join the inner ranges
        // into a flat range of words.
        | stdv::transform(stdv::join)
        // Finally, for each paragraph of words, transform the range of words
        // into a range of printable tokens
        | stdv::transform(NEO_TL(printable_tokens(_1, indent, column_limit)));
    std::string acc;
    auto        tokens_iter = tokens.begin();
    auto        tokens_end  = tokens.end();
    while (tokens_iter != tokens_end) {
        auto tokens = *tokens_iter;
        acc.append(neo::join_text(tokens, ""sv));
        ++tokens_iter;
        if (tokens_iter != tokens_end) {
            acc.append("\n\n");
        }
    }
    return acc;
}
