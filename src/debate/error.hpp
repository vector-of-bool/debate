#pragma once

#include <stdexcept>

namespace debate {

struct e_parsing_word {
    std::string value;
};

struct e_argument_value {
    std::string value;
};

struct logic_error : std::logic_error {
    using std::logic_error::logic_error;
};

struct runtime_error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct invalid_argument_params : logic_error {
    using logic_error::logic_error;
};

struct invalid_argument_repetition : runtime_error {
    using runtime_error::runtime_error;
};

struct unknown_argument : runtime_error {
    using runtime_error::runtime_error;
};

struct missing_argument : runtime_error {
    using runtime_error::runtime_error;
};

struct missing_argument_value : runtime_error {
    using runtime_error::runtime_error;
};

struct invalid_argument_value : runtime_error {
    using runtime_error::runtime_error;
};

}  // namespace debate
