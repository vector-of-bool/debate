#include <debate/argument_parser.hpp>

#include <boost/leaf/handle_errors.hpp>
#include <debate/error.hpp>

#include <catch2/catch.hpp>

#include <array>

using debate::argument_parser;
using debate::opt_string;

TEST_CASE("Create a parser") { argument_parser p{{.description = "meow"}}; }

TEST_CASE("Add an argument") {
    argument_parser p{{.description = "test"}};
    p.add_argument({.names = {"hello"}, .action = debate::null_action});
}

TEST_CASE("Parse an argv list") {
    argument_parser p;
    std::string     howdy_got;
    std::string     another_got;
    opt_string      opt_arg;

    p.add_argument({
        .names  = {"Howdy"},
        .action = debate::store_string(howdy_got),
    });
    p.add_argument({
        .names  = {"another"},
        .action = debate::store_string(another_got),
    });
    p.add_argument({
        .names  = {"--flag"},
        .action = debate::store_string(opt_arg),
    });

    std::vector<std::string> strings = {"foo", "bar"};
    p.parse_args(strings);
    CHECK(howdy_got == "foo");
    CHECK(another_got == "bar");

    CHECK_FALSE(opt_arg.has_value());

    strings = {"baz", "--flag", "meow", "quux"};
    p.parse_args(strings);
    CHECK(howdy_got == "baz");
    CHECK(another_got == "quux");
    CHECK(opt_arg == "meow");
}

TEST_CASE("Missing required") {
    argument_parser p;
    p.add_argument({.names = {"--foo"}, .action = debate::null_action, .required = true});

    CHECK_THROWS_AS(p.parse_args(std::array<std::string, 0>{}), debate::missing_argument);
}

TEST_CASE("Cases") {
    argument_parser parser;

    auto parse = [&](std::initializer_list<std::string_view> argv) { parser.parse_args(argv); };

    SECTION("Simple positionals") {
        opt_string first;
        opt_string second;
        opt_string third;

        SECTION("Required positionals") {
            auto first_arg  = parser.add_argument({
                 .names  = {"first"},
                 .action = debate::store_string(first),
            });
            auto second_arg = parser.add_argument({
                .names  = {"second"},
                .action = debate::store_string(second),
            });
            auto third_arg  = parser.add_argument({
                 .names    = {"third"},
                 .action   = debate::store_string(third),
                 .required = false,
            });

            SECTION("Missing first") {
                boost::leaf::try_catch(
                    [&] {
                        parse({});
                        FAIL_CHECK("Did not fail");
                    },
                    [&](debate::missing_argument,
                        debate::e_argument_parser,
                        debate::e_argument        missing_arg,
                        debate::e_argument_value* valptr,
                        debate::e_argument_name*  nameptr) {
                        CHECK(valptr == nullptr);
                        CHECK(nameptr == nullptr);
                        CHECK(missing_arg.value.id() == first_arg.id());
                    });
            }

            SECTION("Missing second") {
                boost::leaf::try_catch(
                    [&] {
                        parse({"foo"});
                        FAIL_CHECK("Did not fail");
                    },
                    [&](debate::missing_argument,
                        debate::e_argument_parser,
                        debate::e_argument        missing_arg,
                        debate::e_argument_value* valptr,
                        debate::e_argument_name*  nameptr) {
                        CHECK(valptr == nullptr);
                        CHECK(nameptr == nullptr);
                        CHECK(missing_arg.value.id() == second_arg.id());
                    });
            }

            SECTION("Missing third non-required") {
                parse({"foo", "bar"});
                CHECK(first == "foo");
                CHECK(second == "bar");
                CHECK_FALSE(third.has_value());
            }

            SECTION("Supply third non-required") {
                parse({"foo", "bar", "baz"});
                CHECK(first == "foo");
                CHECK(second == "bar");
                CHECK(third == "baz");
            }
        }

        SECTION("Repeated positionals") {
            parser.add_argument({
                .names  = {"foo"},
                .action = debate::null_action,
            });

            std::vector<std::string> repeated;
            SECTION("Required repeated") {
                auto repeat_arg = parser.add_argument({
                    .names      = {"repeat-me"},
                    .action     = debate::store_string(std::back_inserter(repeated)),
                    .can_repeat = true,
                });
                SECTION("Missing") {
                    boost::leaf::try_catch(
                        [&] {
                            parse({"first"});
                            FAIL_CHECK("Did not fail");
                        },
                        [&](debate::missing_argument, debate::e_argument arg) {
                            CHECK(arg.value.id() == repeat_arg.id());
                        });
                }
                SECTION("Once") {
                    parse({"first", "second"});
                    CHECKED_IF(repeated.size() == 1) { CHECK(repeated[0] == "second"); };
                }
                SECTION("More than once") {
                    parse({"first", "second", "third", "fourth"});
                    CHECKED_IF(repeated.size() == 3) {
                        CHECK(repeated[0] == "second");
                        CHECK(repeated[1] == "third");
                        CHECK(repeated[2] == "fourth");
                    };
                }
            }
        }
    }

    SECTION("Simple flag with value") {
        opt_string               foo;
        std::vector<std::string> args;

        auto foo_arg_object = parser.add_argument({
            .names  = {"--foo", "-F"},
            .action = debate::store_string(foo),
        });
        parser.add_argument({
            .names  = {"--foo-with-extra"},
            .action = debate::null_action,
        });
        parser.add_argument({
            .names      = {"--arg", "-A"},
            .action     = debate::store_string(std::back_inserter(args)),
            .can_repeat = true,
        });

        SECTION("Omitted") {
            parse({});
            CHECK_FALSE(foo.has_value());
        }

        SECTION("Provided") {
            parse({"--foo", "some-value"});
            CHECK(foo == "some-value");
        }

        SECTION("Provided with equal sign") {
            parse({"--foo=value2"});
            CHECK(foo == "value2");
        }

        SECTION("Does not match longer word") {
            parse({"--foo-with-extra=0"});
            CHECK_FALSE(foo.has_value());
        }

        SECTION("Empty equals") {
            parse({"--foo="});
            CHECKED_IF(foo.has_value()) { CHECK(foo->empty()); }
        }

        SECTION("Missing value") {
            boost::leaf::try_catch(
                [&] {
                    parse({"--foo"});
                    FAIL_CHECK("Did not throw");
                },
                [&](debate::e_argument     failed_arg,
                    debate::e_parsing_word word,
                    debate::e_argument_parser,
                    debate::missing_argument_value) {
                    CHECK(failed_arg.value.id() == foo_arg_object.id());
                    CHECK(word.value == "--foo");
                });
        }

        SECTION("Flag given as value") {
            // The second word will be parsed as the value for --foo
            parse({"--foo", "--foo-with-extra"});
            CHECK(foo == "--foo-with-extra");
        }

        SECTION("Short flag") {
            parse({"-F", "meow"});
            CHECK(foo == "meow");
        }

        SECTION("Attached") {
            parse({"-Fbark"});
            CHECK(foo == "bark");
        }

        SECTION("Missing value") {
            boost::leaf::try_catch(
                [&] {
                    parse({"-F"});
                    FAIL_CHECK("Didn't throw");
                },
                [&](debate::e_argument     failed_arg,
                    debate::e_parsing_word word,
                    debate::e_argument_parser,
                    debate::missing_argument_value) {
                    CHECK(failed_arg.value.id() == foo_arg_object.id());
                    CHECK(word.value == "-F");
                });
        }

        SECTION("Short consumes the next word") {
            parse({"-F", "--foo-with-extra"});
            CHECK(foo == "--foo-with-extra");
        }

        SECTION("Repetition fails") {
            boost::leaf::try_catch(
                [&] {
                    parse({"--foo", "something", "--foo", "again"});
                    FAIL_CHECK("Didn't throw");
                },
                [&](debate::e_argument     failed_arg,
                    debate::e_parsing_word word,
                    debate::e_argument_parser,
                    debate::invalid_argument_repetition) {
                    // We still parsed one value:
                    CHECK(foo == "something");
                    CHECK(failed_arg.value.id() == foo_arg_object.id());
                    CHECK(word.value == "--foo");
                });
        }

        SECTION("Repitition fails with short 1") {
            boost::leaf::try_catch(
                [&] {
                    parse({"--foo", "something", "-F", "again"});
                    FAIL_CHECK("Didn't throw");
                },
                [&](debate::e_argument     failed_arg,
                    debate::e_parsing_word word,
                    debate::e_argument_parser,
                    debate::invalid_argument_repetition) {
                    // We still parsed one value:
                    CHECK(foo == "something");
                    CHECK(failed_arg.value.id() == foo_arg_object.id());
                    CHECK(word.value == "-F");
                });
        }

        SECTION("Repitition fails with short 2") {
            boost::leaf::try_catch(
                [&] {
                    parse({"-F", "something", "--foo", "again"});
                    FAIL_CHECK("Didn't throw");
                },
                [&](debate::e_argument     failed_arg,
                    debate::e_parsing_word word,
                    debate::e_argument_parser,
                    debate::invalid_argument_repetition) {
                    // We still parsed one value:
                    CHECK(foo == "something");
                    CHECK(failed_arg.value.id() == foo_arg_object.id());
                    CHECK(word.value == "--foo");
                });
        }

        SECTION("Repition is okay with can_repeat") {
            parse({"-A", "bar", "--arg", "value"});
            CHECK(args.size() == 2);
        }
    }

    SECTION("Simple toggles") {
        debate::opt_bool toggle;
        opt_string       other_value;
        parser.add_argument({
            .names       = {"--bar", "-B"},
            .action      = debate::store_true(toggle),
            .wants_value = false,
        });
        parser.add_argument({
            .names       = {"--no-bar", "-nb"},
            .action      = debate::store_false(toggle),
            .wants_value = false,
        });
        parser.add_argument({
            .names  = {"--other", "-O"},
            .action = debate::store_string(other_value),
        });

        SECTION("Omitted") {
            parse({});
            CHECK_FALSE(toggle.has_value());
        }

        SECTION("Enable") {
            parse({"--bar"});
            CHECK(toggle.has_value());
            CHECK(toggle == true);
        }

        SECTION("Short enable") {
            parse({"-B"});
            CHECK(toggle.has_value());
            CHECK(toggle == true);
        }

        SECTION("Short value does not toggle") {
            parse({"-OB"});
            CHECK_FALSE(toggle.has_value());
            CHECK(other_value == "B");
        }

        SECTION("Short ordering affects parsing") {
            parse({"-BOmeow"});
            CHECK(toggle.has_value());
            CHECK(toggle == true);
            CHECK(other_value == "meow");
        }
    }
}

TEST_CASE("Subparsers") {
    argument_parser p;
    opt_string      base_value;
    p.add_argument({
        .names  = {"--base-arg"},
        .action = debate::store_string(base_value),
    });

    auto parse = [&](std::initializer_list<std::string_view> argv) { p.parse_args(argv); };

    SECTION("Single subparser") {
        opt_string selected_subparser;

        auto grp = p.add_subparsers({
            .title    = "subcommand",
            .action   = debate::store_string(selected_subparser),
            .required = false,
        });

        grp.add_parser({.name = "foo"});
        grp.add_parser({.name = "bar"});

        SECTION("No subparser") {
            parse({"--base-arg=nope"});
            CHECK(base_value == "nope");
            CHECK_FALSE(selected_subparser.has_value());
        }

        SECTION("Select foo with base arg") {
            parse({"--base-arg", "yep", "foo"});
            CHECK(base_value == "yep");
            CHECK(selected_subparser == "foo");
        }

        SECTION("Select without base arg") {
            parse({"foo"});
            CHECK_FALSE(base_value.has_value());
            CHECK(selected_subparser == "foo");
        }

        SECTION("Base value is not a subparser") {
            parse({"--base-arg", "foo", "bar"});
            CHECK(base_value == "foo");
            CHECK(selected_subparser == "bar");
        }

        SECTION("Pass arg after subparser") {
            parse({"foo", "--base-arg=meow"});
            CHECK(base_value == "meow");
            CHECK(selected_subparser == "foo");
        }

        SECTION("Cannot change subparser") {
            boost::leaf::try_catch(
                [&] {
                    parse({"--base-arg=something", "foo", "bar"});
                    FAIL_CHECK("Did not throw");
                },
                [&](debate::e_argument_parser,
                    debate::e_parsing_word word,
                    debate::unknown_argument) {
                    CHECK(selected_subparser == "foo");
                    CHECK(base_value == "something");
                    CHECK(word.value == "bar");
                });
        }

        SECTION("Duplicate arg after subparser") {
            boost::leaf::try_catch(
                [&] {
                    parse({"--base-arg=boop", "foo", "--base-arg=duplicate"});
                    FAIL_CHECK("Did not throw");
                },
                [&](debate::e_argument_parser,
                    debate::e_parsing_word word,
                    debate::e_argument     arg,
                    debate::invalid_argument_repetition) {
                    CHECK(arg.value.preferred_name() == "--base-arg");
                    CHECK(word.value == "--base-arg=duplicate");
                    CHECK(selected_subparser == "foo");
                    CHECK(base_value == "boop");
                });
        }

        SECTION("Invalid subparser") {
            boost::leaf::try_catch(
                [&] {
                    parse({"invalid"});
                    FAIL_CHECK("Did not throw");
                },
                [&](debate::e_argument_parser,
                    debate::e_parsing_word word,
                    debate::invalid_argument_value) { CHECK(word.value == "invalid"); });
        }
    }

    SECTION("Subparser with arguments") {
        opt_string selected_subparser;
        auto       grp = p.add_subparsers({
                  .action   = debate::store_string(selected_subparser),
                  .required = false,
        });
        auto       foo = grp.add_parser({.name = "foo"});
        opt_string foo_value;
        auto       bar = grp.add_parser({.name = "bar"});
        opt_string bar_value;

        foo.add_argument({
            .names  = {"--foo-arg"},
            .action = debate::store_string(foo_value),
        });
        bar.add_argument({
            .names  = {"--bar-arg"},
            .action = debate::store_string(bar_value),
        });

        SECTION("No subparser") {
            parse({});
            CHECK_FALSE(selected_subparser.has_value());
        }

        SECTION("No subparser, no matching arg") {
            boost::leaf::try_catch(
                [&] {
                    parse({"--foo-arg=nope"});
                    FAIL_CHECK("Did not throw");
                },
                [&](debate::e_argument_parser,
                    debate::e_parsing_word word,
                    debate::unknown_argument) {
                    CHECK(word.value == "--foo-arg=nope");
                    CHECK_FALSE(foo_value.has_value());
                });
        }
    }

    SECTION("Required subparser") {
        auto subs = p.add_subparsers({
            .action   = debate::null_action,
            .required = true,
        });

        subs.add_parser({.name = "foo"});

        boost::leaf::try_catch(
            [&] {
                parse({});
                FAIL_CHECK("Did not throw");
            },
            [&](debate::e_argument_parser, debate::missing_argument) {
                // okay
            });
    }
}
