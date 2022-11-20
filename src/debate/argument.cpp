#include "./argument.hpp"

#include "./detail/reflow.hpp"
#include "./error.hpp"

#include <neo/tokenize.hpp>

#include <algorithm>
#include <cstring>
#include <ranges>
#include <string_view>

using namespace debate;

namespace stdr = std::ranges;
using namespace std::literals;

struct debate::detail::argument_data {
    debate::params::for_argument params;

    bool is_positional;
};

const params::for_argument& argument::_params() const noexcept { return (*this)->params; }

bool argument::is_positional() const noexcept { return (*this)->is_positional; }
bool argument::can_repeat() const noexcept { return _params().can_repeat; }
bool argument::is_required() const noexcept { return _params().required == true; }
bool argument::wants_value() const noexcept { return _params().wants_value; }
// The "preferred name" appears in diagnostics
std::string_view argument::preferred_name() const noexcept { return _params().names.front(); }

std::string argument::value_name() const noexcept {
    if (_params().metavar.has_value()) {
        return *_params().metavar;
    }
    if (is_positional()) {
        return neo::ufmt("<{}>", preferred_name());
    }
    if (preferred_name().starts_with("--")) {
        auto sub = preferred_name().substr(2);
        return neo::ufmt("<{}>", sub);
    } else {
        return "<value>"s;
    }
}

std::string argument::syntax_string() const noexcept {
    std::string ret;
    auto        pref_spell = preferred_name();
    std::string valname    = value_name();
    if (is_positional()) {
        if (is_required()) {
            if (can_repeat()) {
                ret.append(neo::ufmt("{} [{} [...]]", valname, valname));
            } else {
                ret.append(valname);
            }
        } else {
            if (can_repeat()) {
                ret.append(neo::ufmt("[{} [{} [...]]]", valname, valname));
            } else {
                ret.append(neo::ufmt("[{}]", valname));
            }
        }
    } else if (wants_value()) {
        char sep_char = pref_spell.starts_with("--") ? '=' : ' ';
        if (is_required()) {
            ret.append(neo::ufmt("{}{}{}", pref_spell, sep_char, valname));
            if (can_repeat()) {
                ret.append(neo::ufmt(" [{}{}{} [...]]", pref_spell, sep_char, valname));
            }
        } else {
            if (can_repeat()) {
                ret.append(neo::ufmt("[{}{}{} [{}{}{} [...]]]",
                                     pref_spell,
                                     sep_char,
                                     valname,
                                     pref_spell,
                                     sep_char,
                                     valname));
            } else {
                ret.append(neo::ufmt("[{}{}{}]", pref_spell, sep_char, valname));
            }
        }
    } else {
        ret.append(neo::ufmt("[{}]", pref_spell));
    }
    return ret;
}

std::string argument::help_string() const noexcept {
    std::string ret;
    auto        valname = value_name();
    if (is_positional()) {
        ret = valname;
    } else {
        auto names = _params().names  //
            | std::views::transform([&](auto& name) {
                         if (not wants_value()) {
                             return name;
                         } else if (name.starts_with("--")) {
                             return neo::ufmt("{}={}", name, valname);
                         } else {
                             return neo::ufmt("{} {}", name, valname);
                         }
                     })
            | neo::join_text("\n"sv);
        ret.append(std::string(names));
    }
    ret.append("\n");
    if (_params().help) {
        auto help = detail::reflow_text(*_params().help, "   ", 79);
        ret.append(std::string(neo::str_concat(" ➥ ", neo::trim(help), "\n")));
    }

    return ret;
}

namespace {

bool is_positional_word(std::string_view sp) { return not sp.starts_with("-"); }

}  // namespace

argument::argument(params::for_argument p_) {
    detail::argument_data& impl = *this;
    impl.params                 = std::move(p_);

    if (impl.params.names.empty()) {
        throw invalid_argument_params{".names must be non-empty"};
    }

    if (impl.params.names.size() == 1) {
        impl.is_positional = is_positional_word(impl.params.names.front());
        if (impl.is_positional and not impl.params.required.has_value()) {
            impl.params.required = true;
        }
    } else {
        // More than one argument. They must all be non-positional
        if (stdr::any_of(impl.params.names, is_positional_word)) {
            throw invalid_argument_params{
                "All of .names must be flag-like strings or a single positional argument name"};
        }
    }
}

argument_id argument::id() const noexcept {
    auto         addr = &(static_cast<const detail::argument_data&>(*this));
    std::int64_t p;
    std::memcpy(&p, &addr, sizeof addr);
    return argument_id{p};
}

std::string_view argument::match_long(std::string_view word) const noexcept {
    for (std::string_view name : _params().names) {
        if (word.starts_with(name)) {
            if (name == word or word[name.size()] == '=') {
                return name;
            } else {
                // Not a match. The name expects more than this word.
            }
        }
    }
    return std::string_view{};
}

std::string_view argument::match_short(std::string_view letters) const noexcept {
    for (std::string_view name : _params().names) {
        if (name.starts_with('-') and name.size() >= 2 and name[1] != '-') {
            auto shrt = name.substr(1);
            if (letters.starts_with(shrt)) {
                return shrt;
            }
        }
    }
    return std::string_view{};
}

void argument::handle(std::string_view spelling, std::string_view value) const {
    auto&& act = _params().action;
    if (act) {
        act(spelling, value);
    }
}