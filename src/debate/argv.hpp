#pragma once

#include <iterator>
#include <ranges>
#include <string>
#include <vector>

namespace debate {

class argv_array {
    std::vector<std::string> _args;

public:
    template <std::input_iterator It, std::sentinel_for<It> S>
    requires std::convertible_to<std::iter_reference_t<It>, std::string_view>
    explicit argv_array(It iter, S stop) {
        for (; iter != stop; ++iter) {
            _args.push_back(std::string{std::string_view{*iter}});
        }
    }

    template <std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, std::string_view>
    explicit argv_array(R&& r)
        : argv_array(std::ranges::begin(r), std::ranges::end(r)) {}

    auto begin() const noexcept { return std::cbegin(_args); }
    auto end() const noexcept { return std::cend(_args); }
};

using argv_iterator = std::ranges::iterator_t<argv_array>;
using argv_subrange = std::ranges::subrange<argv_iterator>;

struct e_argv_array {
    argv_array value;
};

}  // namespace debate
