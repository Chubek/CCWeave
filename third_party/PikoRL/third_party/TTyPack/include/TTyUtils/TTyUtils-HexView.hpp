#pragma once

#include <TTyUtils/TTyUtils-ScrollBar.hpp>
#include <TTyUtils/TTyUtils-Search.hpp>
#include <TTyUtils/TTyUtils-Widget.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace ttyutils::hexview {

struct Region {
    std::size_t begin{};
    std::size_t end{};
};

struct Annotation {
    Region region{};
    std::string name{};
    theme::Style style{};
};

class Inspector {
public:
    [[nodiscard]] static auto u16(const std::vector<std::uint8_t>& bytes, const std::size_t offset) -> std::uint16_t {
        if (offset + 1 >= bytes.size()) return 0;
        return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8U));
    }
    [[nodiscard]] static auto u32(const std::vector<std::uint8_t>& bytes, const std::size_t offset) -> std::uint32_t {
        if (offset + 3 >= bytes.size()) return 0;
        return static_cast<std::uint32_t>(bytes[offset] | (bytes[offset + 1] << 8U) |
                                          (bytes[offset + 2] << 16U) | (bytes[offset + 3] << 24U));
    }
};

class HexView : public ui::Widget {
public:
    [[nodiscard]] auto open(const std::filesystem::path& path) -> bool {
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        data_ = {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        return true;
    }
    void set_data(std::vector<std::uint8_t> data) { data_ = std::move(data); }
    void annotate(Annotation annotation) { annotations_.push_back(std::move(annotation)); }
    [[nodiscard]] auto data() const noexcept -> const std::vector<std::uint8_t>& { return data_; }
    [[nodiscard]] auto annotations() const noexcept -> const std::vector<Annotation>& { return annotations_; }
    void paint(ui::Canvas& canvas) override {
        std::string line;
        for (const auto byte : data_) {
            constexpr char digits[] = "0123456789abcdef";
            line += digits[(byte >> 4U) & 0x0fU];
            line += digits[byte & 0x0fU];
            line.push_back(' ');
        }
        canvas.text(rect().x, rect().y, line, style());
    }

private:
    std::vector<std::uint8_t> data_;
    std::vector<Annotation> annotations_;
};

}  // namespace ttyutils::hexview
