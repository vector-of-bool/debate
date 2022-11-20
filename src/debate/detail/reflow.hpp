#pragma once

#include <cstddef>
#include <string_view>

namespace debate::detail {

std::string reflow_text(std::string_view given,
                        std::string_view subseq_indent,
                        std::size_t      column_limit) noexcept;

}  // namespace debate::detail
