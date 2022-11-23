#pragma once

#include "./argv.hpp"

#include <neo/assignable_box.hpp>
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

/// Error data: The argument object that was being handled that generated the error
struct e_argument {
    argument value;
};

/// Error data: The spelling of the argument name as it was given in the command array
struct e_argument_name {
    std::string value;
};

/// Error data: The value that was provided to an argument
struct e_argument_value {
    std::string value;
};

template <typename Target, typename Value>
concept storage_target = requires(Target&& t, Value&& v) {
    t = NEO_FWD(v);
};

template <storage_target<std::string> D>
auto store_string(D&& out) noexcept {
    return [out = neo::assignable_box{NEO_FWD(out)}](std::string_view,
                                                     std::string_view spell) mutable {
        out.get() = std::string(spell);
    };
}

template <typename Dest, typename T>
requires storage_target<Dest, T>
auto store_value(Dest&& into, T&& value) noexcept {
    neo::assignable_box into_box{NEO_FWD(into)};
    return [into_box, value = NEO_FWD(value)](std::string_view, std::string_view) {
        into_box.get() = value;
    };
}

template <storage_target<bool> B>
auto store_true(B&& out) noexcept {
    return store_value(NEO_FWD(out), true);
}

template <storage_target<bool> B>
auto store_false(B&& out) noexcept {
    return store_value(NEO_FWD(out), false);
}

struct null_action_t {
    void operator()(std::string_view, std::string_view) const noexcept {}
};

inline null_action_t null_action;

}  // namespace debate
