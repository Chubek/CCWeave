#pragma once

#include <TTyUtils/TTyUtils-ListView.hpp>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ttyutils::ui {

enum ColumnAlign { Left = 0, Right = 1, Center = 2 };
enum class SortOrder { Asc, Desc };

struct Column {
    std::string title{};
    int width{10};
    ColumnAlign align{Left};
};

class TableModel {
public:
    virtual ~TableModel() = default;
    [[nodiscard]] virtual auto rows() const -> std::size_t = 0;
    [[nodiscard]] virtual auto value(std::size_t row, std::size_t column) const -> std::string = 0;
};

class TableView : public Widget {
public:
    explicit TableView(std::shared_ptr<TableModel> model = {}) : model_(std::move(model)) {}
    void set_model(std::shared_ptr<TableModel> model) { model_ = std::move(model); }
    void add_column(Column column) { columns_.push_back(std::move(column)); }
    void sort_by(const std::size_t column, const SortOrder order = SortOrder::Asc) {
        sort_column_ = column;
        order_ = order;
    }
    [[nodiscard]] auto columns() const noexcept -> const std::vector<Column>& { return columns_; }

    void paint(Canvas& canvas) override {
        if (!model_) return;
        for (std::size_t row = 0; row < model_->rows(); ++row) {
            std::string line;
            for (std::size_t column = 0; column < columns_.size(); ++column) {
                auto value = model_->value(row, column);
                value.resize(static_cast<std::size_t>(std::max(0, columns_[column].width)), ' ');
                line += value;
            }
            canvas.text(rect().x, rect().y + static_cast<int>(row), line, style());
        }
    }

private:
    std::shared_ptr<TableModel> model_;
    std::vector<Column> columns_;
    std::size_t sort_column_{};
    SortOrder order_{SortOrder::Asc};
};

}  // namespace ttyutils::ui
