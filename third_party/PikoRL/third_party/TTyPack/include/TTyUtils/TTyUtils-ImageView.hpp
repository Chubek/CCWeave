#pragma once

#include <TTyTk/TTyTk-ColorTools.hpp>
#include <TTyTk/TTyTk-ImageProto.hpp>
#include <TTyUtils/TTyUtils-Widget.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace ttyutils::imgview {

enum class FitMode { Contain, Cover, Actual };
enum class Backend { Auto, Kitty, Sixel, Iterm2, Cells };

class ImageView : public ui::Widget {
public:
    [[nodiscard]] auto load(const std::filesystem::path& path) -> bool {
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        data_ = {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        path_ = path.string();
        return true;
    }
    void set_fit(const FitMode mode) noexcept { fit_ = mode; }
    void set_backend(const Backend backend) noexcept { backend_ = backend; }
    [[nodiscard]] auto fit() const noexcept -> FitMode { return fit_; }
    [[nodiscard]] auto backend() const noexcept -> Backend { return backend_; }
    [[nodiscard]] auto path() const noexcept -> const std::string& { return path_; }

private:
    std::string path_;
    std::vector<std::uint8_t> data_;
    FitMode fit_{FitMode::Contain};
    Backend backend_{Backend::Auto};
};

// NOTE(agent): TTyTk::Image is provided by the compatibility pixel container
// in TTyTk-ImageProto.hpp; decoding file formats remains caller-owned.
inline auto show(ImageView& view, const std::filesystem::path& path) -> bool {
    return view.load(path);
}

}  // namespace ttyutils::imgview
