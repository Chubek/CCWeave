#pragma once

#include <TTyUtils/TTyUtils-ListView.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ttyutils::ui {

struct TreeNodeRef {
    std::string path{};
    std::string label{};
};

class TreeModel {
public:
    virtual ~TreeModel() = default;
    [[nodiscard]] virtual auto roots() const -> std::vector<TreeNodeRef> = 0;
    [[nodiscard]] virtual auto children(const std::string& path) const -> std::vector<TreeNodeRef> = 0;
};

enum class ExpandPolicy { Single, Multiple, All };

class TreeView : public Widget {
public:
    explicit TreeView(std::shared_ptr<TreeModel> model = {}) : model_(std::move(model)) {}
    void set_model(std::shared_ptr<TreeModel> model) { model_ = std::move(model); }
    void set_policy(const ExpandPolicy policy) noexcept { policy_ = policy; }
    void expand(const std::string_view path) {
        if (policy_ == ExpandPolicy::Single) expanded_.clear();
        expanded_.insert(std::string(path));
    }
    void collapse(const std::string_view path) { expanded_.erase(std::string(path)); }
    [[nodiscard]] auto expanded(const std::string_view path) const -> bool { return expanded_.contains(std::string(path)); }
    void on_choose(std::function<void(const TreeNodeRef&)> callback) { choose_ = std::move(callback); }

private:
    std::shared_ptr<TreeModel> model_;
    ExpandPolicy policy_{ExpandPolicy::Multiple};
    std::unordered_set<std::string> expanded_;
    std::function<void(const TreeNodeRef&)> choose_{};
};

}  // namespace ttyutils::ui
