#include "./argument_parser.hpp"

#include "./detail/reflow.hpp"
#include "./error.hpp"

#include <boost/leaf/exception.hpp>
#include <boost/leaf/on_error.hpp>
#include <neo/assert.hpp>
#include <neo/memory.hpp>
#include <neo/tl.hpp>
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
namespace stdv = std::views;

using strv = std::string_view;

namespace {

struct nocopy {
    nocopy()              = default;
    nocopy(const nocopy&) = delete;
};

struct subparser {
    category        cat;
    argument_parser parser;
};

using parser_map = std::map<std::string, subparser, std::less<>>;

struct subparser_group_impl {
    parser_map  parsers;
    std::string title;
    opt_string  description;
    bool        required;

    std::weak_ptr<detail::argument_parser_impl> parent;

    std::function<void(std::string_view, std::string_view)> action;
};

}  // namespace

struct detail::argument_parser_impl {
    params::for_argument_parser params;

    std::string                         name;
    std::weak_ptr<argument_parser_impl> parent;

    /// Command-line arguments attached to this parser
    std::vector<debate::argument> arguments{};
    /// Sub-parsers attached to this parser. Only non-null after a call to add_subparsers()
    std::optional<subparser_group_impl> subparsers{};

    // nocopy _disable_copy{};

    static argument_parser_impl&       extract(argument_parser& p) noexcept { return *p._impl; }
    static const argument_parser_impl& extract(const argument_parser& p) noexcept {
        return *p._impl;
    }
};

namespace {

struct parsing_state {
    explicit parsing_state(argument_parser n)
        : parser_chain({n}) {}

    std::vector<argument_parser> parser_chain;

    std::set<argument_id> seen{};

    void check_help(argv_subrange remaining) {
        static std::map<std::string_view, category> help_map = {
            {"--help", general},
            {"-help", general},
            {"-h", general},
            {"-?", general},
            {"--help-adv", advanced},
            {"--help-advanced", advanced},
            {"--help-dbg", debugging},
            {"--help-debug", debugging},
            {"--help-all", debugging},
        };
        auto is_help_request = [](auto s) { return help_map.contains(s); };
        auto help_arg        = stdr::find_if(remaining, is_help_request);
        if (help_arg != remaining.end()) {
            category cat = help_map.find(*help_arg)->second;
            throw help_request{cat};
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
        return parser_chain                                          //
            | std::views::transform(NEO_TL(_impl_of(_1).arguments))  //
            | std::views::join;
    }

    void finalize() const {
        for (const auto& parser : parser_chain) {
            auto _ = boost::leaf::on_error(e_argument_parser{parser});
            for (const argument& arg : _impl_of(parser).arguments) {
                if (arg.is_required() and not seen.count(arg.id())) {
                    BOOST_LEAF_THROW_EXCEPTION(missing_argument{std::string(arg.preferred_name())},
                                               e_argument{arg});
                }
            }
        }

        if (_impl_of(parser_chain.back()).subparsers
            and _impl_of(parser_chain.back()).subparsers->required) {
            auto _ = boost::leaf::on_error(e_argument_parser{parser_chain.back()});
            throw(missing_argument{std::string(_impl_of(parser_chain.back()).subparsers->title)});
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
                parser_chain.push_back(child->second.parser);
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
    _impl->subparsers = subparser_group_impl{
        .parsers     = {},
        .title       = p.title,
        .description = p.description,
        .required    = p.required.value_or(true),
        .parent      = _impl,
        .action      = p.action,
    };
    return subparser_group{*this};
}

subparser_group::subparser_group(argument_parser p) noexcept
    : _parser(p) {}

argument_parser subparser_group::add_parser(params::for_subparser p) {
    auto& impl  = detail::argument_parser_impl::extract(_parser);
    auto  found = impl.subparsers->parsers.find(p.name);
    if (found != impl.subparsers->parsers.end()) {
        throw invalid_argument_params{"Duplicate subparser name"};
    }
    argument_parser parser({
        .prog        = p.name,
        .description = p.description,
        .epilog      = p.epilog,
    });
    subparser       child{.cat = p.category, .parser = parser};
    impl.subparsers->parsers.emplace(p.name, child);
    parser._impl->parent = _parser._impl;
    return parser;
}

void argument_parser::_parse_args(argv_array argv) const {
    auto _ = boost::leaf::on_error(e_argument_parser{*this});
    parsing_state{*this}.parse_args(argv);
}

void argument_parser::parse_main_argv(int argc, const char* const* argv) const {
    neo_assert_always(expects,
                      argc > 1,
                      "At least one argument is required for parse_main_argv()",
                      argc);
    auto       _ = boost::leaf::on_error(e_invoked_as{argv[0]});
    argv_array arr{argv + 1, argv + argc};
    _parse_args(std::move(arr));
}

std::string argument_parser::arg_usage_string(category cat) const noexcept {
    auto arg_syntaxes = _impl->arguments              //
        | stdv::filter(NEO_TL(_1.category() <= cat))  //
        | stdv::transform(&argument::syntax_string)   //
        | neo::join_text(" "sv);

    auto ret = std::string(arg_syntaxes);
    if (_impl->subparsers.has_value()) {
        auto comma_names =                                //
            _impl->subparsers->parsers                    //
            | stdv::filter(NEO_TL(_1.second.cat <= cat))  //
            | stdv::transform(NEO_TL(_1.first))           //
            | neo::join_text(","sv);
        auto subcommands = neo::str_concat("{"sv, comma_names, "}"sv);
        if (not ret.empty()) {
            ret.push_back(' ');
        }
        if (_impl->subparsers->required) {
            ret.append(std::string(neo::str_concat(subcommands)));
        } else {
            ret.append(std::string(neo::str_concat("[", subcommands, "]")));
        }
    }
    return ret;
}

std::string argument_parser::usage_string(category cat) const noexcept {
    return usage_string(cat, _impl->params.prog.value_or("<program>"));
}

std::string argument_parser::usage_string(category cat, std::string_view progname) const noexcept {
    std::string subcommand_suffix;
    auto        tail_parser = _impl;
    while (tail_parser) {
        neo::ranges::input_range_of<argument> auto selected_args
            = stdv::filter(tail_parser->arguments, NEO_TL(_1.category() <= cat));
        for (const argument& arg : selected_args) {
            if (arg.is_required() and tail_parser != _impl) {
                subcommand_suffix = neo::ufmt(" {}{}", arg.syntax_string(), subcommand_suffix);
            }
        }
        if (not tail_parser->name.empty()) {
            subcommand_suffix = " " + tail_parser->name + subcommand_suffix;
        }
        tail_parser = tail_parser->parent.lock();
    }
    auto ret    = neo::ufmt("{}{}", progname, subcommand_suffix);
    auto indent = ret.size() + 1;
    if (indent > 50) {
        ret.push_back('\n');
        indent = 10;
        ret.append(indent, ' ');
    }
    auto args = arg_usage_string(cat);
    if (not args.empty()) {
        ret = std::string(neo::str_concat(ret, " "sv, args));
    }
    return ret;
}

std::string argument_parser::help_string(category cat) const noexcept {
    return help_string(cat, _impl->params.prog.value_or("<program>"));
}

std::string argument_parser::help_string(category cat, std::string_view progname) const noexcept {
    std::string ret = "Usage: " + usage_string(cat, progname);
    ret.append("\n\n");
    if (_impl->params.description) {
        auto help = detail::reflow_text(*_impl->params.description, "  ", 79);
        ret.append(help);
        ret.append("\n\n");
    }
    bool any_required = false;

    neo::ranges::input_range_of<argument> auto selected_args
        = stdv::filter(_impl->arguments, NEO_TL(_1.category() <= cat));

    for (auto& arg : selected_args) {
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
    for (auto& arg : selected_args) {
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
        for (auto& [key, sub] : subs.parsers | stdv::filter(NEO_TL(_1.second.cat <= cat))) {
            argument_parser subp = sub.parser;
            std::string     usg  = subp.arg_usage_string(cat);
            ret.append(std::string(neo::str_concat("• ", key, " ", usg)));
            auto& desc = subp._impl->params.description;
            if (desc) {
                auto d = detail::reflow_text(*desc, "     ", 79);
                ret.append(std::string(neo::str_concat("\n", "   ➥ ", neo::trim(d), "\n")));
            }
        }
        ret.append("\n");
    }

    auto any_of_category = [&](auto C) {
        return stdr::any_of(_impl->arguments, [&](auto arg) { return arg.category() == C; })
            or (_impl->subparsers and stdr::any_of(_impl->subparsers->parsers, [&](auto pair) {
                    return pair.second.cat == C;
                }));
    };
    auto any_dbg = any_of_category(debugging);
    auto any_adv = any_of_category(advanced);

    if (any_dbg or any_adv) {
        ret.append(
            "Help options:\n"
            "  --help / -h\n"
            "    ➥ Get general help\n\n");
        if (any_adv) {
            ret.append(
                "  --help-adv\n"
                "    ➥ Include advanced progam options\n\n");
        }
        if (any_dbg) {
            ret.append(
                "  --help-dbg\n"
                "    ➥ Include debugging program options\n\n");
        }
    }

    if (_impl->params.epilog.has_value()) {
        auto ep = detail::reflow_text(*_impl->params.epilog, "", 79);
        ret.append(std::string(neo::str_concat(ep, "\n\n")));
    }
    return ret;
}
