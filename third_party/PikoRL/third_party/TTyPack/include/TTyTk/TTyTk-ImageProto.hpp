#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-ColorTools.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ttytk::image {

// NOTE(agent): TTyUtils::ImageView names TTyTk::Image, but the manifest-owned
// image header previously exposed only protocol encoders. This pixel container
// is the dependency-neutral image value those encoders consume.
struct Image {
    std::size_t width{};
    std::size_t height{};
    std::vector<color::Rgb> pixels{};
};

namespace detail {

[[nodiscard]] inline auto base64(const Span<const color::Rgb> pixels) -> std::string {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string bytes;
    bytes.reserve(pixels.size() * 3);
    for (const auto pixel : pixels) {
        bytes.push_back(static_cast<char>(pixel.red));
        bytes.push_back(static_cast<char>(pixel.green));
        bytes.push_back(static_cast<char>(pixel.blue));
    }
    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t offset = 0; offset < bytes.size(); offset += 3) {
        const auto a = static_cast<unsigned char>(bytes[offset]);
        const auto b = offset + 1 < bytes.size() ? static_cast<unsigned char>(bytes[offset + 1]) : 0U;
        const auto c = offset + 2 < bytes.size() ? static_cast<unsigned char>(bytes[offset + 2]) : 0U;
        encoded.push_back(alphabet[a >> 2]);
        encoded.push_back(alphabet[((a & 0x03U) << 4) | (b >> 4)]);
        encoded.push_back(offset + 1 < bytes.size() ? alphabet[((b & 0x0fU) << 2) | (c >> 6)] : '=');
        encoded.push_back(offset + 2 < bytes.size() ? alphabet[c & 0x3fU] : '=');
    }
    return encoded;
}

}  // namespace detail

struct Sixel {
    // WezTerm escape sequences: DCS [params] q data ST is Sixel graphic data.
    [[nodiscard]] static inline auto encode(const Span<const color::Rgb> pixels,
                                            const std::size_t width, const std::size_t height)
        -> std::string {
        if (pixels.empty() || width == 0 || height == 0) return {};
        const auto first = pixels.front();
        return "\x1bPq\"1;1;" + std::to_string(width) + ';' + std::to_string(height) +
               "#0;2;" + std::to_string((first.red * 100U) / 255U) + ';' +
               std::to_string((first.green * 100U) / 255U) + ';' +
               std::to_string((first.blue * 100U) / 255U) + "#0~\x1b\\";
    }
};

struct Kitty {
    // kitty terminal graphics protocol, "The graphics escape code":
    // APC_G action=transmit, format=RGB, medium=direct, payload ST.
    [[nodiscard]] static inline auto encode(const Span<const color::Rgb> pixels,
                                            const std::size_t width, const std::size_t height)
        -> std::string {
        return "\x1b_Ga=T,f=24,s=" + std::to_string(width) + ",v=" +
               std::to_string(height) + ';' + detail::base64(pixels) + "\x1b\\";
    }
};

struct ITerm2 {
    // WezTerm escape sequences: OSC 1337 is iTerm2's inline-file protocol.
    [[nodiscard]] static inline auto encode(const Span<const color::Rgb> pixels,
                                            const std::size_t width, const std::size_t height)
        -> std::string {
        return "\x1b]1337;File=inline=1;width=" + std::to_string(width) + ";height=" +
               std::to_string(height) + ':' + detail::base64(pixels) + "\x07";
    }
};

[[nodiscard]] inline auto render_to_seq(const Sixel&, const Span<const color::Rgb> pixels,
                                        const std::size_t width, const std::size_t height) -> std::string {
    return Sixel::encode(pixels, width, height);
}

[[nodiscard]] inline auto render_to_seq(const Kitty&, const Span<const color::Rgb> pixels,
                                        const std::size_t width, const std::size_t height) -> std::string {
    return Kitty::encode(pixels, width, height);
}

[[nodiscard]] inline auto render_to_seq(const ITerm2&, const Span<const color::Rgb> pixels,
                                        const std::size_t width, const std::size_t height) -> std::string {
    return ITerm2::encode(pixels, width, height);
}

}  // namespace ttytk::image

namespace ttytk {
using Image = image::Image;
}  // namespace ttytk
