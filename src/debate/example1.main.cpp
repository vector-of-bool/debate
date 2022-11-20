#include <debate/argument_parser.hpp>
#include <debate/error.hpp>

#include <boost/leaf/handle_errors.hpp>
#include <neo/tokenize.hpp>
#include <neo/ufmt.hpp>

#include <iostream>

using namespace debate;
using namespace std::literals;

int main(int argc, char** argv) {
    argument_parser parser{{
        .prog        = "debate-example",
        .description = R"(
            This is a simple example program that displays some of the capabilities
            of the Debate library.

            This text is the "description" for the top-level example.
        )",
        .epilog      = R"(
            This is the epilog text. It appears at the bottom of help messages
            of the associated command that saw the help request.
        )",
    }};

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
    parser.add_argument({
        .names       = {"--enable-advanced-features", "-E!"},
        .action      = null_action,
        .wants_value = false,
        .help        = "Enable advanced features (advanced)",
        .category    = debate::advanced,
    });

    opt_string echo_message;
    auto       subs = parser.add_subparsers({
              .title       = "Subcommands",
              .action      = null_action,
              .description = "Specify the subcommand to execute",
              .required    = true,
    });
    // An "echo" subcommand:
    auto echo = subs.add_parser({
        .name        = "echo",
        .description = "Print a message\n\n(This doesn't "
                       "do anything, it's just an example.)",
    });
    echo.add_argument({
        .names    = {"message"},
        .action   = store_string(echo_message),
        .required = true,
        .help     = R"(
            The message to pass to the echo program.
            This message string is required. This a help paragraph. It should
            automatically be reflowed to fit within 79 columns.

            This is another paragraph now.
        )",
    });

    return boost::leaf::try_catch(
        [&] {
            parser.parse_main_argv(argc, argv);
            return 0;
        },
        [](help_request h, e_argument_parser parser, e_invoked_as progname) {
            std::cerr << parser.value.help_string(h.category, progname.value);
            return 0;
        },
        [](missing_argument, e_argument_parser parser, e_invoked_as progname, e_argument arg) {
            std::cerr << parser.value.usage_string(debate::general, progname.value) << '\n';
            auto arg_help   = arg.value.help_string();
            auto help_lines = neo::iter_lines(arg_help)
                | std::views::transform(NEO_TL(std::string(neo::str_concat("  "sv, _1, "\n"sv))))
                | neo::to_vector;
            std::cerr << neo::ufmt("Missing required argument '{}':\n\n{}",
                                   arg.value.preferred_name(),
                                   neo::join_text(help_lines, ""sv));
            return 1;
        },
        [](missing_argument, e_invoked_as progname, e_argument_parser parser) {
            std::cerr << parser.value.usage_string(debate::general, progname.value) << '\n';
            std::cerr << neo::ufmt("Missing required subcommand\n");
            return 1;
        });
}
