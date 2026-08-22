#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-CharWidth.hpp>
#include <TTyTk/TTyTk-Grampheme.hpp>
#include <TTyTk/TTyTk-ColorTools.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ttytk {

struct Cell {
    std::string grapheme{" "};
    std::uint32_t attributes{};
    color::Rgb foreground{229, 229, 229};
    color::Rgb background{0, 0, 0};
    bool continuation{};
};

class CellBuffer {
public:
    constexpr CellBuffer() = default;
    CellBuffer(const std::size_t rows, const std::size_t columns) { resize(rows, columns); }

    [[nodiscard]] inline auto rows() const noexcept -> std::size_t { return rows_; }
    [[nodiscard]] inline auto columns() const noexcept -> std::size_t { return columns_; }

    [[nodiscard]] inline auto at(const std::size_t row, const std::size_t column) -> Cell& {
        return cells_[row * columns_ + column];
    }

    [[nodiscard]] inline auto at(const std::size_t row, const std::size_t column) const -> const Cell& {
        return cells_[row * columns_ + column];
    }

    inline void put(const std::size_t row, const std::size_t column, Cell cell) {
        if (row >= rows_ || column >= columns_) return;
        const auto width = std::max(1, charwidth::of_string(cell.grapheme));
        cell.continuation = false;
        at(row, column) = std::move(cell);
        if (width == 2 && column + 1 < columns_) {
            at(row, column + 1) = Cell{"", at(row, column).attributes, at(row, column).foreground,
                                        at(row, column).background, true};
        }
    }

    inline void resize(const std::size_t rows, const std::size_t columns) {
        std::vector<Cell> resized(rows * columns);
        const auto copy_rows = std::min(rows_, rows);
        const auto copy_columns = std::min(columns_, columns);
        for (std::size_t row = 0; row < copy_rows; ++row) {
            for (std::size_t column = 0; column < copy_columns; ++column) {
                resized[row * columns + column] = at(row, column);
            }
        }
        rows_ = rows;
        columns_ = columns;
        cells_ = std::move(resized);
    }

    inline void scroll(const std::size_t top, const std::size_t bottom, const int amount) {
        if (top >= bottom || bottom > rows_ || amount == 0) return;
        const auto height = bottom - top;
        const auto shift = static_cast<std::size_t>(std::min<int>(
            static_cast<int>(height), amount < 0 ? -amount : amount));
        if (amount > 0) {
            for (std::size_t row = top; row + shift < bottom; ++row)
                for (std::size_t column = 0; column < columns_; ++column)
                    at(row, column) = at(row + shift, column);
            for (std::size_t row = bottom - shift; row < bottom; ++row)
                for (std::size_t column = 0; column < columns_; ++column) at(row, column) = Cell{};
        } else {
            for (std::size_t row = bottom; row-- > top + shift;)
                for (std::size_t column = 0; column < columns_; ++column)
                    at(row, column) = at(row - shift, column);
            for (std::size_t row = top; row < top + shift; ++row)
                for (std::size_t column = 0; column < columns_; ++column) at(row, column) = Cell{};
        }
    }

    inline void clear() { std::fill(cells_.begin(), cells_.end(), Cell{}); }

private:
    std::size_t rows_{};
    std::size_t columns_{};
    std::vector<Cell> cells_{};
};

// NOTE(agent): The manifests refer to a TTyTk::Screen header that does not
// exist. Screen is the minimal owning façade over the existing cell buffer.
class Screen {
public:
    Screen() = default;
    Screen(const std::size_t rows, const std::size_t columns) : buffer_(rows, columns) {}
    [[nodiscard]] auto rows() const noexcept -> std::size_t { return buffer_.rows(); }
    [[nodiscard]] auto columns() const noexcept -> std::size_t { return buffer_.columns(); }
    void resize(const std::size_t rows, const std::size_t columns) { buffer_.resize(rows, columns); }
    void clear() { buffer_.clear(); }
    [[nodiscard]] auto cells() noexcept -> CellBuffer& { return buffer_; }
    [[nodiscard]] auto cells() const noexcept -> const CellBuffer& { return buffer_; }

private:
    CellBuffer buffer_;
};

}  // namespace ttytk
