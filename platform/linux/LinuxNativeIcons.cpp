#include "LinuxNativeIcons.h"

#include <png.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace such::platform::linuxui {
namespace {

std::string lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!s.empty() && s.front() == '.') s.erase(s.begin());
    return s;
}

std::string icon_name_for_extension(std::string ext) {
    ext = lower_ascii(std::move(ext));
    if (ext == "xls" || ext == "xlsx" || ext == "ods" || ext == "csv") return "x-office-spreadsheet";
    if (ext == "doc" || ext == "docx" || ext == "odt" || ext == "rtf") return "x-office-document";
    if (ext == "ppt" || ext == "pptx" || ext == "odp") return "x-office-presentation";
    if (ext == "dwg" || ext == "dxf" || ext == "svg" || ext == "ai") return "x-office-drawing";
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "webp" || ext == "tif" || ext == "tiff") return "image-x-generic";
    if (ext == "mp3" || ext == "wav" || ext == "flac" || ext == "m4a") return "audio-x-generic";
    if (ext == "mp4" || ext == "mov" || ext == "mkv" || ext == "avi") return "video-x-generic";
    if (ext == "exe" || ext == "appimage" || ext == "bin") return "application-x-executable";
    return "text-x-generic";
}

std::vector<std::filesystem::path> theme_roots() {
    std::vector<std::filesystem::path> roots;
    if (const char* home = std::getenv("HOME")) {
        roots.emplace_back(std::filesystem::path(home) / ".local/share/icons");
        roots.emplace_back(std::filesystem::path(home) / ".icons");
    }
    if (const char* data = std::getenv("XDG_DATA_DIRS")) {
        std::string s(data);
        std::size_t pos = 0;
        while (pos <= s.size()) {
            const auto next = s.find(':', pos);
            const std::string part = s.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
            if (!part.empty()) roots.emplace_back(std::filesystem::path(part) / "icons");
            if (next == std::string::npos) break;
            pos = next + 1;
        }
    }
    roots.emplace_back("/usr/share/icons");
    return roots;
}

std::filesystem::path find_theme_png(const std::string& icon_name) {
    static constexpr std::array<std::string_view, 4> themes{"Adwaita", "hicolor", "HighContrast", "gnome"};
    static constexpr std::array<std::string_view, 6> sizes{"48x48", "64x64", "32x32", "24x24", "22x22", "16x16"};
    const auto roots = theme_roots();
    for (const auto& root : roots) {
        for (const auto theme : themes) {
            for (const auto size : sizes) {
                const auto p = root / theme / size / "mimetypes" / (icon_name + ".png");
                std::error_code ec;
                if (std::filesystem::is_regular_file(p, ec)) return p;
            }
        }
    }
    return {};
}

RgbaIcon load_png(const std::filesystem::path& path) {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&image, path.string().c_str())) return {};

    constexpr png_uint_32 kMaxIconDimension = 4096;
    if (image.width == 0 || image.height == 0 || image.width > kMaxIconDimension || image.height > kMaxIconDimension) {
        png_image_free(&image);
        return {};
    }

    image.format = PNG_FORMAT_RGBA;
    const std::size_t pixel_count = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    if (pixel_count > (static_cast<std::size_t>(-1) / 4u)) {
        png_image_free(&image);
        return {};
    }

    RgbaIcon out;
    out.width = static_cast<int>(image.width);
    out.height = static_cast<int>(image.height);
    out.rgba.resize(pixel_count * 4u);
    if (!png_image_finish_read(&image, nullptr, out.rgba.data(), 0, nullptr)) {
        png_image_free(&image);
        return {};
    }
    png_image_free(&image);
    return out;
}

RgbaIcon load_system_icon_for_extension(const std::string& ext) {
    const std::string requested = icon_name_for_extension(ext);
    auto p = find_theme_png(requested);
    if (p.empty() && requested != "text-x-generic") p = find_theme_png("text-x-generic");
    if (p.empty()) p = find_theme_png("application-x-generic");
    return p.empty() ? RgbaIcon{} : load_png(p);
}

unsigned long component_to_mask(unsigned value, unsigned long mask) {
    if (mask == 0) return 0;
    unsigned shift = 0;
    unsigned long normalized = mask;
    while ((normalized & 1ul) == 0ul) { normalized >>= 1u; ++shift; }
    const unsigned long scaled = (static_cast<unsigned long>(value) * normalized + 127ul) / 255ul;
    return (scaled << shift) & mask;
}

unsigned long pixel_from_rgb(const Visual* visual, unsigned r, unsigned g, unsigned b) {
    if (visual == nullptr) return 0;
    return component_to_mask(r, visual->red_mask) |
           component_to_mask(g, visual->green_mask) |
           component_to_mask(b, visual->blue_mask);
}

} // namespace

const RgbaIcon& NativeIconCache::icon_for_extension(const std::string& extension) {
    const std::string key = lower_ascii(extension);
    const auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    return cache_.emplace(key, load_system_icon_for_extension(key)).first->second;
}

void NativeIconCache::draw(Display* display, Window window, GC gc, const RgbaIcon& icon,
                           int x, int y, int target_size, unsigned long background_rgb) const {
    constexpr int kMaxRenderedIcon = 2048;
    if (!icon.valid() || target_size <= 0 || target_size > kMaxRenderedIcon) return;
    const int screen = DefaultScreen(display);
    const int depth = DefaultDepth(display, screen);
    Visual* visual = DefaultVisual(display, screen);

    // Let Xlib compute the actual scanline stride for this visual/depth instead
    // of assuming four tightly-packed bytes per pixel. This keeps unusual
    // TrueColor visuals and padded scanlines safe.
    XImage* image = XCreateImage(display, visual, static_cast<unsigned>(depth), ZPixmap, 0, nullptr,
                                 static_cast<unsigned>(target_size), static_cast<unsigned>(target_size), 32, 0);
    if (!image || image->bytes_per_line <= 0) { if (image) XDestroyImage(image); return; }
    const std::size_t stride = static_cast<std::size_t>(image->bytes_per_line);
    const std::size_t height = static_cast<std::size_t>(target_size);
    if (stride > (static_cast<std::size_t>(-1) / height)) { XDestroyImage(image); return; }
    image->data = static_cast<char*>(std::calloc(stride * height, 1));
    if (!image->data) { XDestroyImage(image); return; }

    const int br = static_cast<int>((background_rgb >> 16) & 0xFFu);
    const int bg = static_cast<int>((background_rgb >> 8) & 0xFFu);
    const int bb = static_cast<int>(background_rgb & 0xFFu);

    for (int dy = 0; dy < target_size; ++dy) {
        const int sy = std::clamp((dy * icon.height) / target_size, 0, icon.height - 1);
        for (int dx = 0; dx < target_size; ++dx) {
            const int sx = std::clamp((dx * icon.width) / target_size, 0, icon.width - 1);
            const std::size_t si = static_cast<std::size_t>((sy * icon.width + sx) * 4);
            const int a = icon.rgba[si + 3];
            const int r = (icon.rgba[si] * a + br * (255 - a)) / 255;
            const int g = (icon.rgba[si + 1] * a + bg * (255 - a)) / 255;
            const int b = (icon.rgba[si + 2] * a + bb * (255 - a)) / 255;
            const unsigned long pixel = pixel_from_rgb(visual, static_cast<unsigned>(r),
                                                       static_cast<unsigned>(g), static_cast<unsigned>(b));
            XPutPixel(image, dx, dy, pixel);
        }
    }
    XPutImage(display, window, gc, image, 0, 0, x, y, static_cast<unsigned>(target_size), static_cast<unsigned>(target_size));
    XDestroyImage(image); // also frees data
}

} // namespace such::platform::linuxui
