#include "./argument_parser.hpp"

#include "./error.hpp"

#include <boost/leaf/on_error.hpp>
#include <neo/assert.hpp>
#include <neo/memory.hpp>
#include <neo/tokenize.hpp>
#include <neo/ufmt.hpp>
#include <neo/utility.hpp>

#include <algorithm>
#include <map>
#include <ranges>
#include <set>

using namespace std::literals;
using namespace debate;
namespace stdr = std::ranges;

using strv = std::string_view;

using parser_map = std::map<std::string, argument_parser, std::less<>>;

struct nocopy {
    nocopy()              = default;
    nocopy(const nocopy&) = delete;
};

struct subparser_info {
    parser_map  parsers;
    std::string title;
    opt_string  description;
    bool        required;

    std::weak_ptr<detail::argument_parser_impl> parent;

    std::function<void(std::string_view, std::string_view)> action;
};

struct detail::argument_parser_impl {
    params::for_argument_parser params;

    std::string                         name;
    std::weak_ptr<argument_parser_impl> parent;

    /// Command-line arguments attached to this parser
    std::vector<debate::argument> arguments{};
    /// Sub-parsers attached to this parser. Only non-null after a call to add_subparsers()
    std::optional<subparser_info> subparsers{};

    // nocopy _disable_copy{};

    static argument_parser_impl&       extract(argument_parser& p) noexcept { return *p._impl; }
    static const argument_parser_impl& extract(const argument_parser& p) noexcept {
        return *p._impl;
    }
};

namespace {

struct subparser_group_impl {
    std::map<std::string, argument_parser>* subparser_map_ptr;
};

struct parsing_state {
    explicit parsing_state(argument_parser n)
        : parser_chain({n}) {}

    std::vector<argument_parser> parser_chain;

    std::set<argument_id> seen{};

    void check_help(argv_subrange remaining) {
        if (stdr::find_if(remaining, [](auto s) { return s == neo::oper::any_of("--help", "-h"); })
            != remaining.end()) {
            throw help_request{};
        }
    }

    void parse_args(const argv_array& args) {
        auto _ = boost::leaf::on_error([&] { return e_argv_array{args}; });

        argv_subrange argv{args.begin(), args.end()};

        while (not argv.empty()) {
            int n_skip = parse_more(argv);
            argv       = argv.next(n_skip);
        }

        finalize();
    }

    static const auto& _impl_of(const auto& parser) {
        return detail::argument_parser_impl::extract(parser);
    }

    auto chain_arguments() const {
        return parser_chain | std::views::transform([](auto parser) -> const auto& {
                   return _impl_of(parser).arguments;
               })
            | std::views::join;
    }

    void finalize() const {
        for (const auto& parser : parser_chain) {
            auto _ = boost::leaf::on_error(e_argument_parser{parser});
            for (const argument& arg : _impl_of(parser).arguments) {
                if (arg.is_required() and not seen.count(arg.id())) {
                    auto _1 = boost::leaf::on_error(e_argument{arg});
                    throw missing_argument{std::string(arg.preferred_name())};
                }
            }
        }

        if (_impl_of(parser_chain.back()).subparsers
            and _impl_of(parser_chain.back()).subparsers->required) {
            auto _ = boost::leaf::on_error(e_argument_parser{parser_chain.back()});
            throw missing_argument{std::string{_impl_of(parser_chain.back()).subparsers->title}};
        }
    }

    int parse_more(argv_subrange argv) {
        strv current = argv.front();
        auto _       = boost::leaf::on_error([&] { return e_parsing_word{std::string(current)}; },
                                       e_argument_parser{parser_chain.back()});
        if (current.starts_with("--")) {
            // A long option
            return try_parse_long(current, argv);
        } else if (current.starts_with("-")) {
            return try_parse_shorts(current.substr(1), argv);
        } else {
            return try_parse_positional(current, argv);
        }
    }

    int try_parse_long(strv given, argv_subrange argv) {
        for (const argument& arg : std::views::reverse(chain_arguments())) {
            auto match = arg.match_long(given);
            if (match.empty()) {
                continue;
            }
            return handle_long(given, match, arg, argv);
        }
        check_help(argv);
        throw unknown_argument{std::string{given}};
    }

    int handle_long(strv given, strv arg_name, const argument& arg, argv_subrange argv) {
        auto _ = boost::leaf::on_error(e_argument_name{std::string(arg_name)}, e_argument{arg});
        if (seen.count(arg.id())) {
            // We've already seen this argument before
            if (not arg.can_repeat()) {
                check_help(argv);
                throw invalid_argument_repetition{std::string(arg_name)};
            }
        }
        seen.insert(arg.id());
        auto tail = given.substr(arg_name.size());
        if (tail.empty()) {
            // The next in the argv would be the value
            if (not arg.wants_value()) {
                // This is an argument without a value
                arg.handle(arg_name, "");
                return 1;
            }
            // Treat the next argv element as the value
            auto it = argv.begin() + 1;
            if (it == argv.end()) {
                check_help(argv);
                throw missing_argument_value{std::string{arg_name}};
            }
            auto value = *it;
            arg.handle(arg_name, value);
            return 2;
        } else {
            // The given argv element is spelled as "--long-option=something"
            neo_assert(invariant,
                       tail.starts_with("="),
                       "Invalid long-argument matched",
                       given,
                       arg_name);
            if (not arg.wants_value()) {
                // This argument does not expect a value. Wrong!
                check_help(argv);
                throw invalid_argument_value{std::string(tail.substr(1))};
            }
            auto value = tail.substr(1);
            arg.handle(arg_name, value);
            return 1;
        }
    }

    struct short_skip_results {
        int n_letters;
        int n_words;
    };

    int try_parse_shorts(strv letters, argv_subrange argv) {
        while (not letters.empty()) {
            short_skip_results skip = try_parse_shorts_1(letters, argv);
            letters.remove_prefix(skip.n_letters);
            if (skip.n_words != 0) {
                neo_assert(invariant, letters.empty(), "Did not drain short flag list", letters);
                return skip.n_words;
            }
            if (skip.n_letters == 0) {
                // We never matched anything
                check_help(argv);
                throw unknown_argument{"-" + std::string(letters)};
            }
        }
        return 1;
    }

    short_skip_results try_parse_shorts_1(strv letters, argv_subrange argv) {
        for (auto& arg : std::views::reverse(chain_arguments())) {
            auto mat = arg.match_short(letters);
            if (mat.empty()) {
                continue;
            }
            return handle_short(letters, mat, arg, argv);
        }
        return short_skip_results{0, 0};
    }

    short_skip_results
    handle_short(strv letters, strv short_name, const argument& arg, argv_subrange argv) {
        std::string with_hyphen = "-" + std::string(short_name);
        auto        _ = boost::leaf::on_error(e_argument_name{with_hyphen}, e_argument{arg});
        if (seen.count(arg.id())) {
            // We've seen this one before
            if (not arg.can_repeat()) {
                check_help(argv);
                throw invalid_argument_repetition{with_hyphen};
            }
        }
        seen.insert(arg.id());
        auto remain = letters.substr(short_name.size());
        if (arg.wants_value()) {
            if (remain.empty()) {
                // Treat the following word as the value
                auto it = argv.begin() + 1;
                if (it == argv.end()) {
                    check_help(argv);
                    throw missing_argument_value{with_hyphen};
                }
                arg.handle(with_hyphen, *it);
                return short_skip_results{.n_letters = static_cast<int>(short_name.size()),
                                          .n_words   = 2};
            } else {
                // Treat the remainder of the word as the argument
                arg.handle(with_hyphen, remain);
                return short_skip_results{.n_letters = static_cast<int>(letters.size()),
                                          .n_words   = 1};
            }
        } else {
            // No value. Ignore remaining letters
            arg.handle(with_hyphen, "");
            return short_skip_results{.n_letters = static_cast<int>(short_name.size()),
                                      .n_words   = 0};
        }
    }

    int try_parse_positional(strv given, argv_subrange argv) {
        for (auto&& arg : chain_arguments()) {
            if (not arg.is_positional()) {
                // We're not parsing these here
                continue;
            }
            if (seen.count(arg.id())) {
                // We've already seen this one
                continue;
            }
            seen.insert(arg.id());
            auto _ = boost::leaf::on_error(e_argument_name{std::string{arg.preferred_name()}},
                                           e_argument{arg});
            arg.handle(given, given);
            return 1;
        }
        // No positional argument matched. Maybe a subcommand?
        detail::argument_parser_impl const& tail_parser = _impl_of(parser_chain.back());
        if (tail_parser.subparsers.has_value()) {
            auto child = tail_parser.subparsers->parsers.find(given);
            if (child != tail_parser.subparsers->parsers.end()) {
                // We found a subparser!
                if (tail_parser.subparsers->action) {
                    tail_parser.subparsers->action(given, given);
                }
                parser_chain.push_back(child->second);
                return 1;
            } else {
                check_help(argv);
                throw invalid_argument_value{std::string{given}};
            }
        }
        check_help(argv);
        throw unknown_argument{std::string{given}};
    }
};

}  // namespace

argument_parser::argument_parser() noexcept
    : argument_parser(params::for_argument_parser{}) {}

argument_parser::argument_parser(params::for_argument_parser p) {
    _impl = neo::copy_shared(detail::argument_parser_impl{
        .params = std::move(p),
        .name   = "",
        .parent = {},
    });
}

argument argument_parser::add_argument(params::for_argument p) {
    return _impl->arguments.emplace_back(std::move(p));
}

subparser_group argument_parser::add_subparsers(params::for_subparser_group p) {
    if (_impl->subparsers.has_value()) {
        throw invalid_argument_params{
            "Cannot have multiple subparser groups attached to a single parent parser"};
    }
    _impl->subparsers = subparser_info{
        .parsers     = {},
        .title       = p.title,
        .description = p.description,
        .required    = p.required.value_or(false),
        .parent      = _impl,
        .action      = p.action,
    };
    return subparser_group{*this};
}

subparser_group::subparser_group(argument_parser p) noexcept
    : _parser(p) {}

argument_parser subparser_group::add_parser(std::string_view name, params::for_argument_parser p) {
    auto& impl  = detail::argument_parser_impl::extract(_parser);
    auto  found = impl.subparsers->parsers.find(name);
    if (found != impl.subparsers->parsers.end()) {
        throw invalid_argument_params{"Duplicate subparser name"};
    }
    auto child = impl.subparsers->parsers.emplace(std::string{name}, argument_parser{std::move(p)})
                     .first->second;
    child._impl->parent = _parser._impl;
    child._impl->name   = std::string(name);
    return child;
}

argument_parser subparser_group::add_parser(std::string_view name) {
    return add_parser(name, params::for_argument_parser{});
}

void argument_parser::_parse_args(argv_array argv) const {
    auto _ = boost::leaf::on_error(e_argument_parser{*this});
    parsing_state{*this}.parse_args(argv);
}

void argument_parser::parse_main_argv(int argc, const char* const* argv) const {
    argv_array arr{argv + 1, argv + argc};
    _parse_args(std::move(arr));
}

std::string argument_parser::usage_string() const noexcept {
    return usage_string(_impl->params.prog.value_or("<program>"));
}
std::string argument_parser::usage_string(std::string_view progname) const noexcept {
    std::string subcommand_suffix;
    auto        tail_parser = _impl;
    while (tail_parser) {
        for (const argument& arg : tail_parser->arguments) {
            if (arg.is_required() and tail_parser != _impl) {
                subcommand_suffix = neo::ufmt(" {}{}", arg.syntax_string(), subcommand_suffix);
            }
        }
        if (not tail_parser->name.empty()) {
            subcommand_suffix = " " + tail_parser->name + subcommand_suffix;
        }
        tail_parser = tail_parser->parent.lock();
    }
    auto ret    = neo::ufmt("Usage: {}{}", progname, subcommand_suffix);
    auto indent = ret.size() + 1;
    if (indent > 50) {
        ret.push_back('\n');
        indent = 10;
        ret.append(indent, ' ');
    }

    std::size_t col = indent;
    for (auto& arg : _impl->arguments) {
        auto synstr = arg.syntax_string();
        if (col + synstr.size() > 79 && col > indent) {
            ret.append("\n");
            ret.append(indent - 1, ' ');
            col = indent - 1;
        }
        ret.append(" " + synstr);
        col += synstr.size() + 1;
    }

    if (_impl->subparsers.has_value()) {
        auto&       subs           = _impl->subparsers->parsers;
        std::string subcommand_str = " {";
        for (auto it = subs.cbegin(); it != subs.cend();) {
            subcommand_str.append(it->second._impl->name);
            ++it;
            if (it != subs.cend()) {
                subcommand_str.append(",");
            }
        }
        subcommand_str.append("}");
        if (col + subcommand_str.size() > 79 && col > indent) {
            ret.append("\n");
            ret.append(indent - 1, ' ');
        }
        ret.append(subcommand_str);
    }
    return ret;
}

std::string argument_parser::help_string() const noexcept {
    return help_string(_impl->params.prog.value_or("<program>"));
}
std::string argument_parser::help_string(std::string_view progname) const noexcept {
    std::string ret = usage_string(progname);
    ret.append("\n\n");
    if (_impl->params.description) {
        ret.append(*_impl->params.description);
        ret.append("\n\n");
    }
    bool any_required = false;
    for (auto& arg : _impl->arguments) {
        if (not arg.is_required()) {
            continue;
        }
        if (not any_required) {
            ret.append("Required arguments:\n");
        }
        any_required  = true;
        auto arg_help = arg.help_string();
        for (auto line : neo::iter_lines(arg_help)) {
            ret.append(neo::ufmt("  {}\n", neo::view_text(line)));
        }
    }
    bool any_non_required = false;
    for (auto& arg : _impl->arguments) {
        if (arg.is_required()) {
            continue;
        }
        if (!any_non_required) {
            ret.append("Optional arguments:\n");
        }
        auto arg_help = arg.help_string();
        for (auto line : neo::iter_lines(arg_help)) {
            ret.append(neo::ufmt("  {}\n", neo::view_text(line)));
        }
        any_non_required = true;
        ret.append("\n");
    }

    if (_impl->subparsers) {
        auto& subs = *_impl->subparsers;
        ret.append(neo::ufmt("{}:\n", subs.title));
        if (subs.description) {
            auto desc = neo::trim(*subs.description);
            for (auto line : neo::iter_lines(desc)) {
                line = neo::trim(line);
                ret.append(neo::ufmt("  {}\n", neo::view_text(line)));
            }
            ret.append("\n");
        }
        for (auto& [key, sub] : subs.parsers) {
            ret.append(neo::ufmt("  {}", sub._impl->name));
            auto& desc = sub._impl->params.description;
            if (desc) {
                ret.append("\n");
                auto trimmed = neo::trim(*desc);
                for (auto line : neo::iter_lines(trimmed)) {
                    line = neo::trim(line);
                    ret.append(neo::ufmt("    {}\n", neo::view_text(line)));
                }
                ret.append("\n");
            }
        }
    }
    return ret;
}
