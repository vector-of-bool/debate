#include <debate/argument_parser.hpp>
#include <debate/error.hpp>

#include <boost/leaf/handle_errors.hpp>
#include <neo/ufmt.hpp>

#include <iostream>

using namespace debate;

int main(int argc, char** argv) {
    argument_parser parser{{.prog = "debate-example"}};

    opt_string first_value;
    parser.add_argument({
        .names  = {"first"},
        .action = store_string(first_value),
        .help   = "Set the first positional argument (required)",
    });
    parser.add_argument({
        .names       = {"--flag", "-f"},
        .action      = null_action,
        .required    = true,
        .wants_value = true,
        .help        = "Specify the flag_value with this option",
    });

    opt_string echo_message;
    auto       subs = parser.add_subparsers({
              .title       = "Subcommands",
              .action      = null_action,
              .description = "Specify the subcommand to execute",
              .required    = true,
    });
    auto       echo = subs.add_parser("echo", {.description = "Print a message"});
    echo.add_argument({
        .names    = {"message"},
        .action   = store_string(echo_message),
        .required = true,
    });

    return boost::leaf::try_catch(
        [&] {
            boost::leaf::try_catch([&] { parser.parse_main_argv(argc, argv); },
                                   [&](debate::runtime_error, e_argument_parser parser) {
                                       std::cerr << parser.value.usage_string(argv[0]) << '\n';
                                       throw;
                                   });
            // nope
            return 0;
        },
        [&](help_request, e_argument_parser parser) {
            std::cerr << parser.value.help_string();
            return 0;
        },
        [](e_argument arg, missing_argument) {
            std::cerr << neo::ufmt("Missing required argument '{}'\n", arg.value.preferred_name());
            return 1;
        });
}
