#include "./edit_distance.hpp"

#include <algorithm>
#include <array>
#include <ranges>
#include <vector>

int debate::detail::lev_edit_distance(std::string_view left, std::string_view right) noexcept {
    const auto n_rows    = left.size() + 1;
    const auto n_columns = right.size() + 1;

    const auto empty_row = std::vector<std::size_t>(n_columns, 0);

    std::vector<std::vector<std::size_t>> matrix(n_rows, empty_row);

    auto row_iter = std::views::iota(1u, n_rows);
    auto col_iter = std::views::iota(1u, n_columns);

    for (auto n : col_iter) {
        matrix[0][n] = n;
    }
    for (auto n : row_iter) {
        matrix[n][0] = n;
    }

    for (auto row : row_iter) {
        for (auto col : col_iter) {
            auto cost = right[col - 1] == left[row - 1] ? 0 : 1;

            auto t1  = matrix[row - 1][col] + 1;
            auto t2  = matrix[row][col - 1] + 1;
            auto t3  = matrix[row - 1][col - 1] + cost;
            auto arr = std::array{t1, t2, t3};

            matrix[row][col] = *std::ranges::min_element(arr);
        }
    }

    return static_cast<int>(matrix.back().back());
}