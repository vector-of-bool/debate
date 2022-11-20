#pragma once

#include "./argv.hpp"

#include <neo/declval.hpp>
#include <neo/shared.hpp>

#include <any>
#include <cinttypes>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace debate {

using string_vec = std::vector<std::string>;
using opt_string = std::optional<std::string>;
using opt_bool   = std::optional<bool>;

enum class category {
    general,
    advanced,
    debugging,
    hidden,
};

constexpr auto general   = category::general;
constexpr auto advanced  = category::advanced;
constexpr auto debugging = category::debugging;
constexpr auto hidden    = category::hidden;

namespace params {

struct for_argument {
    string_vec names;

    std::function<void(std::string_view, std::string_view)> action;

    bool     can_repeat  = false;
    opt_bool required    = std::nullopt;
    bool     wants_value = true;

    opt_string metavar = std::nullopt;
    opt_string help    = std::nullopt;

    debate::category category = general;
};

}  // namespace params

namespace detail {

struct argument_data;

}  // namespace detail

struct argument_id {
    std::int64_t v;

    constexpr bool operator==(const argument_id&) const noexcept  = default;
    constexpr auto operator<=>(const argument_id&) const noexcept = default;
};

class argument : neo::shared_state<argument, detail::argument_data> {
    const params::for_argument& _params() const noexcept;

public:
    explicit argument(params::for_argument p);

    bool is_positional() const noexcept;

    argument_id   id() const noexcept;
    bool          can_repeat() const noexcept;
    bool          is_required() const noexcept;
    bool          wants_value() const noexcept;
    std::string   value_name() const noexcept;
    std::string   syntax_string() const noexcept;
    std::string   help_string() const noexcept;
    enum category category() const noexcept;

    std::string_view preferred_name() const noexcept;
    std::string_view match_long(std::string_view) const noexcept;
    std::string_view match_short(std::string_view) const noexcept;

    void handle(std::string_view argv_spelling, std::string_view argv_value) const;
};

struct e_argument_name {
    std::string value;
};

struct e_argument {
    argument value;
};

template <typename D>
requires std::assignable_from<D&, std::string>
auto store_string(D& out) noexcept {
    return [&](std::string_view, std::string_view spell) { out = std::string(spell); };
}

template <typename Dest, typename T>
requires std::assignable_from<Dest&, T&>
auto store_value(Dest& into, T&& value) noexcept {
    return [&into, value = NEO_FWD(value)](std::string_view, std::string_view) { into = value; };
}

template <typename B>
requires std::assignable_from<B&, bool>
auto store_true(B& out) noexcept { return store_value(out, true); }

template <typename B>
requires std::assignable_from<B&, bool>
auto store_false(B& out) noexcept { return store_value(out, false); }

struct null_action_t {
    void operator()(std::string_view, std::string_view) const noexcept {}
};

inline null_action_t null_action;

}  // namespace debate
