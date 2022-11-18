#pragma once

#include "./argument.hpp"
#include "./argv.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace debate {

class argument_parser;

using opt_string = std::optional<std::string>;

struct help_request : std::exception {};

namespace params {

struct for_argument_parser {
    opt_string prog        = std::nullopt;
    opt_string description = std::nullopt;
};

struct for_subparser {};

struct for_subparser_group {
    std::string title = "subcommands";

    std::function<void(std::string_view, std::string_view)> action;

    opt_string description{};

    opt_bool required{};

    opt_string help{};
};

}  // namespace params

namespace detail {

struct argument_parser_impl;

}  // namespace detail

class argument_parser;
class subparser_group;

class argument_parser {
    friend subparser_group;
    friend detail::argument_parser_impl;

    std::shared_ptr<detail::argument_parser_impl> _impl;

    void _parse_args(argv_array argv) const;

    argument_parser(params::for_argument_parser,
                    std::shared_ptr<detail::argument_parser_impl> parent);

public:
    argument_parser() noexcept;
    explicit argument_parser(params::for_argument_parser p);

    debate::argument add_argument(params::for_argument p);

    subparser_group add_subparsers(params::for_subparser_group);

    template <std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, std::string_view>
    void parse_args(R&& r) const { _parse_args(argv_array(r)); }

    void parse_main_argv(int argc, const char* const* argv) const;

    std::string usage_string() const noexcept;
    std::string usage_string(std::string_view progname) const noexcept;
    std::string help_string() const noexcept;
    std::string help_string(std::string_view progname) const noexcept;
};

class subparser_group {
    friend argument_parser;

    argument_parser _parser;
    explicit subparser_group(argument_parser) noexcept;

public:
    /**
     * @brief Attach a new subparser to the subparser group.
     *
     * @param name The name of the subparser, used as the word to the parent processor that will
     * select the subparser.
     * @return argument_parser A new argument parser that is a child of the parser that was used to
     * create this group.
     */
    argument_parser add_parser(std::string_view name);
    argument_parser add_parser(std::string_view name, params::for_argument_parser);
};

struct e_argument_parser {
    argument_parser value;
};

}  // namespace debate