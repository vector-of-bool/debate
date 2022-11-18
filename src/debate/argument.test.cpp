#include "./argument.hpp"

#include "./error.hpp"

#include <catch2/catch.hpp>

TEST_CASE("Create an argument") {
    debate::argument arg{{.names = {"foo"}, .action = debate::null_action}};
    CHECK(arg.is_positional());

    // Error: More than one positional spelling
    CHECK_THROWS_AS(debate::argument({.names = {"foo", "bar"}, .action = debate::null_action}),
                    debate::invalid_argument_params);
    // Error: Mixing positional and flags
    CHECK_THROWS_AS(debate::argument(
                        {.names = {"positional", "--flags"}, .action = debate::null_action}),
                    debate::invalid_argument_params);
    // Error: Mixing positional and flags
    CHECK_THROWS_AS(debate::argument(
                        {.names = {"positional", "-Short"}, .action = debate::null_action}),
                    debate::invalid_argument_params);
    // Error: Need at least one spelling
    CHECK_THROWS_AS(debate::argument(
                        {.names = debate::string_vec{/* empty */}, .action = debate::null_action}),
                    debate::invalid_argument_params);
}
