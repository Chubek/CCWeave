#pragma once

#include <TTyUtils/TTyUtils-Config.hpp>
#include <TTyUtils/TTyUtils-Dialog.hpp>
#include <TTyUtils/TTyUtils-ListView.hpp>
#include <TTyUtils/TTyUtils-TextInput.hpp>
#include <TTyUtils/TTyUtils-TreeView.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ttyutils::ui {

enum class PickMode { Open, Save, Directory };

struct FileFilter {
    std::vector<std::string> globs{};
};

struct Bookmark {
    std::string name{};
    std::filesystem::path path{};
};

class FilePicker : public Dialog {
public:
    FilePicker() : Dialog("File picker") {}
    FilePicker& mode(const PickMode mode) noexcept { mode_ = mode; return *this; }
    FilePicker& filter(FileFilter filter) { filter_ = std::move(filter); return *this; }
    void set_directory(std::filesystem::path directory) { directory_ = std::move(directory); }
    void add_bookmark(Bookmark bookmark) { bookmarks_.push_back(std::move(bookmark)); }
    [[nodiscard]] auto show() const -> std::optional<std::filesystem::path> {
        if (mode_ == PickMode::Directory) return directory_;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(directory_, error)) {
            if (error) break;
            if (mode_ == PickMode::Open && entry.is_regular_file()) return entry.path();
        }
        return std::nullopt;
    }

private:
    PickMode mode_{PickMode::Open};
    FileFilter filter_{};
    std::filesystem::path directory_{"."};
    std::vector<Bookmark> bookmarks_;
};

}  // namespace ttyutils::ui
