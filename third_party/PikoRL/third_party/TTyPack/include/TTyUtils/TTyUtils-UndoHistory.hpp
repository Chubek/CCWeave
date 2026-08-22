#pragma once

#include <TTyUtils/TTyUtils-TextBuffer.hpp>
#include <TTyUtils/TTyUtils-TextCursor.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttyutils::undo {

struct TreeNode {
    std::string before{};
    std::string after{};
    std::size_t parent{};
    std::vector<std::size_t> children{};
    std::string label{};
};

struct Checkpoint {
    std::string name{};
    std::size_t node{};
};

class History;

class Transaction {
public:
    explicit Transaction(History& history);
    Transaction(const Transaction&) = delete;
    auto operator=(const Transaction&) -> Transaction& = delete;
    Transaction(Transaction&& other) noexcept : history_(std::exchange(other.history_, nullptr)) {}
    auto operator=(Transaction&& other) noexcept -> Transaction& {
        if (this != &other) {
            commit();
            history_ = std::exchange(other.history_, nullptr);
        }
        return *this;
    }
    ~Transaction() { commit(); }

    void commit();

private:
    History* history_{};
};

class History {
public:
    explicit History(textbuf::Buffer& buffer) : buffer_(&buffer) {
        nodes_.push_back(TreeNode{.after = std::string(buffer.text())});
    }

    [[nodiscard]] auto transaction() -> Transaction { return Transaction{*this}; }
    [[nodiscard]] auto can_undo() const noexcept -> bool { return current_ != 0; }
    [[nodiscard]] auto can_redo() const noexcept { return !nodes_[current_].children.empty(); }
    [[nodiscard]] auto nodes() const noexcept -> const std::vector<TreeNode>& { return nodes_; }

    void begin() {
        if (depth_++ == 0) before_ = std::string(buffer_->text());
    }

    void commit() {
        if (depth_ == 0 || --depth_ != 0) return;
        const auto after = std::string(buffer_->text());
        if (after == before_) return;
        const auto index = nodes_.size();
        nodes_.push_back(TreeNode{.before = std::move(before_), .after = after, .parent = current_});
        nodes_[current_].children.push_back(index);
        current_ = index;
    }

    auto undo() -> bool {
        if (!can_undo()) return false;
        set_text(nodes_[current_].before);
        current_ = nodes_[current_].parent;
        return true;
    }

    auto redo_branch(const std::size_t branch = 0) -> bool {
        if (branch >= nodes_[current_].children.size()) return false;
        current_ = nodes_[current_].children[branch];
        set_text(nodes_[current_].after);
        return true;
    }

    void checkpoint(std::string name) { checkpoints_.push_back({std::move(name), current_}); }
    [[nodiscard]] auto checkpoints() const noexcept -> const std::vector<Checkpoint>& { return checkpoints_; }

private:
    friend class Transaction;

    void set_text(const std::string_view value) {
        const auto index = buffer_->line_index();
        const auto end_line = index.lines() - 1;
        buffer_->erase({0, 0}, {end_line, ttytk::grapheme::count(buffer_->line(end_line))});
        buffer_->insert({0, 0}, value);
    }

    textbuf::Buffer* buffer_{};
    std::vector<TreeNode> nodes_;
    std::vector<Checkpoint> checkpoints_;
    std::size_t current_{};
    std::size_t depth_{};
    std::string before_{};
};

inline Transaction::Transaction(History& history) : history_(&history) { history_->begin(); }
inline void Transaction::commit() {
    if (history_ != nullptr) {
        history_->commit();
        history_ = nullptr;
    }
}

}  // namespace ttyutils::undo
