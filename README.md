# `Debate` - A Command-Line Argument Parsing Library

*Debate* is (yet another) C++ command-line argument parsing library. Debate is
inspired by Python's `argparse` module, and is very opinionated. Debate makes
heavy use of C++ designated initializers, so a recent C++20 compiler is
required.

*Debate* was written as the command-line parser used by
[bpt](https://bpt.pizza).


## Basic Usage

To begin, construct a `debate::argument_parser` object:

```c++
#include <debate/argument_parser.hpp>

int main(int argc, char** argv) {
  // Create a parser:
  debate::argument_parser parser({
    .prog = "my-program",
    .description = R"(
      This is my very excellent program.
      You can use it to do all kinds of interesting stuff.
    )",
  });

  // Now parse some arguments:
  try {
    parser.parse_main_argv(argc, argv);
  } catch (debate::runtime_error) {
    return 1;
  } catch (debate::help_request) {
    std::cerr << parser.help_string();
    return 0;
  }
  // Parse success!
  return 0;
}
```

The constructor for `argument_parser` accepts paramers about the program as a
whole.

The `parse_main_argv` method accepts the `argc` and `argv` array that were given
to the `main()` function (including the `argv[0]`). `parse_main_argv` will throw
an exception in case of error or in case of an explicit `--help` request in the
argument array.


### Adding Arguments

For a given `argument_parser` object `p`, arguments can be defined using the
`add_argument` member function:

```c++
std::string input_file;
p.add_argument({
  .names  = {"input-file"},
  .action = debate::store_string(input_file),
});

bool dry_run = false;
p.add_argument({
  .names       = {"--dry-run", "-n"},
  .action      = debate::store_true(dry_run),
  .wants_value = false,
});
```

The `names` and `action` parameters are always required for each added argument.
See the parameter reference for more information.

In the above, the argument `input-file` is defined as a positional argument.
During parsing, the value of the argument given in the command line array will
be stored in the `input_file` variable.

The above `--dry-run` named argument has a shortened form `-n`, and it does not
expect to consume a value. When Debate sees this argument in a command line
array, it will assign `true` into the reference given to `store_true`.


## Parameter Reference

Most functions in Debate accept a struct object with public members, where those
members can be used as "named arguments" for the relevant function. The order of
the members is required to be correct for the compiler. The parameter names are
documented below in the order that they must be provided. All parameters are
optional unless specified otherwise.

- `debate::params::for_argument_parser` - The constructor parameters for
  `debate::argument_parser`. Accepts the following:

  - `prog`: `optional<string>`: The name of the program (used in some help
    messages).
  - `description`: `optional<string>`: A longer description of the program. This
    string will appear at the top of any help messages for the program.

- `debate::params::for_argument` - Parameters to `add_argument()`. Accepts the
  following:

  - `names`: `vector<string>`: (REQUIRED) An array of strings. Either one
    positional argument name may be provided, or one-or-more long/short named
    arguments. This array must not be empty.

    A positional argument name is any name that does not begin with a hyphen.
    There may be only one name for a positional argument.

    A long-form named argument begins with two hyphens `"--xyz"`. A short-form
    named argument begins with a single hyphen `"-xyz"`. Conventionally, a
    short-form argument name should be a single character name, but this is not
    required by Debate.

  - `action`: `function<void(string_view name, string_view value)>`: (REQUIRED)
    The argument handler. When Debate sees the argument given in a command-line
    array, this function will be invoked with two parameters: The name that was
    used to select the argument, and the value that was given for the argument
    (if applicable).

    For positional parameters, `name` will be the positional name, and `value`
    will be the string given for that position.

    For arguments that have `.want_value=false`, `value` is an empty string.

    It is recommended to only use the `name` parameter for diagnostic purposes
    to match the name that was used by the user on the command line.

  - `can_repeat`: `bool`: (default: `false`) If `true`, Debate will allow this
    argument to appear more than once in a command-line array. For every time
    the argument appears, the `action` will be invoked once. If `false`,
    repeated appearances of an argument value will generate an error.

  - `required`: `bool`: (default: `false` for named arguments, `true` for
    positional arguments). If `true`, generate an error if this argument is not
    provided during command-line parsing.

  - `wants_value`: `bool`: (default: `true`) If `true`, expect the named
    argument to be given a value, otherwise throw an error.

  - `metavar`: `optional<string>`: Specify the string used to represent the
    value in help messages.

  - `help`: `optional<string>`: Specify the help message describing this
    argument.


## Syntax

The resulting application's command-line syntax is opinionated, and based on the
default parsing syntax accepted by Python's `argparse` module.

A command-line argument array is handled as an array of individual strings. The
first element of the array is always assumed to be the name that was used to
invoke the program, and it is not parsed.

### Positional Matching

If an argv-string does not begin with a hyphen, it will be handled as a
positional argument: Debate will seek the first argument in the parser that is
positional and hasn't already been satisfied. If a positional argument is set to
`can_repeat=true`, then it will continuously accept positional arguments, and no
subsequent arguments will be considered. If all positional arguments are
satisfied, Debate will generate an error.


### Long-Form Matching

If an argv-string `S` begins with two hyphens `"--"`, Debate will treat `S` as a
long-form named argument.

- If `S` contains a hyphen, the name string `N` will be the substring of `S` up
  *until* the first hyphen.
- Otherwise, `N` is `S`.
- Debate will search for the first argument `A` in the parser that has a name
  equivalent to `N`. If no `A` is found, Debate will generate an error.
- If `S` contains a hyphen, Debate will consider the substring of `S` *after*
  the first hyphen to be the *value* of `S`. Debate will consume `S` from the
  argv-array. If `A` does not want a value, Debate will generate an error.
- Otherwise, if `A` wants a value, the *value* will be the next argv-string in
  the array after `S`. If `S` is the last string in the argv-array, Debate will
  generate an error. Debate will consume `S` and the element after `S` in the
  argc-array.
- Otherwise, Debate will consume `S`.
- If `A` has already been matched once before and has `can_repeat=false`, Debate
  will generate an error.


### Short-Form Matching

If an argv-string `S` begins with a single hyphen `"-"` Debate will treat `S` as
a short-form named argument *group*. Let `G` be the substring of `S` after the
leading hyphen.

- While `G` is non-empty: for each argument `A` in the parser:
  - If no short-name in `A` is a prefix of `G`, `continue`.
  - Let `G'` be the substring of `G` with the matching prefix from `A` removed.
  - If `A` has already been provided and has `can_repeat=false`, generate an
    error.
  - If `A` wants a value:
    - If `G'` is non-empty, `G'` is the value given for `A`. Consume `G` and
      `break`.
    - Otherwise, the next argument `V` in the argv-array after `S` is the value
      for `A`. Both `S` and `V` are consumed. `break`.
  - Otherwise:
    - `G` now `G'`.
    - `continue`

