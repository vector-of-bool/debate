#pragma once

#include <string_view>

namespace debate::detail {

int lev_edit_distance(std::string_view left, std::string_view right) noexcept;

}  // namespace debate::detail