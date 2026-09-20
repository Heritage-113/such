#pragma once

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace such::platform::linuxui {

struct RgbaIcon {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    [[nodiscard]] bool valid() const noexcept {
        if (width <= 0 || height <= 0) return false;
        const auto w = static_cast<std::size_t>(width);
        const auto h = static_cast<std::size_t>(height);
        return w <= (static_cast<std::size_t>(-1) / h / 4u) && rgba.size() == w * h * 4u;
    }
};

class NativeIconCache {
public:
    [[nodiscard]] const RgbaIcon& icon_for_extension(const std::string& extension);
    void draw(Display* display, Window window, GC gc, const RgbaIcon& icon,
              int x, int y, int target_size, unsigned long background_rgb) const;

private:
    std::unordered_map<std::string, RgbaIcon> cache_;
};

} // namespace such::platform::linuxui
