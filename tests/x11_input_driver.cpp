#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace {
Window find_named_window(Display* display, Window root, const std::string& name) {
    char* window_name = nullptr;
    if (XFetchName(display, root, &window_name) != 0 && window_name != nullptr) {
        const bool matched = name == window_name;
        XFree(window_name);
        if (matched) return root;
    }

    Window root_return = 0;
    Window parent_return = 0;
    Window* children = nullptr;
    unsigned int count = 0;
    if (XQueryTree(display, root, &root_return, &parent_return, &children, &count) == 0) return 0;

    Window found = 0;
    for (unsigned int i = 0; i < count && found == 0; ++i) {
        found = find_named_window(display, children[i], name);
    }
    if (children != nullptr) XFree(children);
    return found;
}

bool send_keycode(Display* display, Window window, KeyCode keycode) {
    XKeyEvent event{};
    event.display = display;
    event.window = window;
    event.root = DefaultRootWindow(display);
    event.time = CurrentTime;
    event.x = 1;
    event.y = 1;
    event.x_root = 1;
    event.y_root = 1;
    event.same_screen = True;
    event.keycode = keycode;
    event.type = KeyPress;
    if (XSendEvent(display, window, True, KeyPressMask, reinterpret_cast<XEvent*>(&event)) == 0) return false;
    XFlush(display);
    return true;
}

bool send_named_key(Display* display, Window window, const std::string& token) {
    const KeySym keysym = XStringToKeysym(token.c_str());
    if (keysym == NoSymbol) return false;
    const KeyCode keycode = XKeysymToKeycode(display, keysym);
    return keycode != 0 && send_keycode(display, window, keycode);
}

bool send_unicode(Display* display, Window window, std::uint32_t codepoint) {
    if (codepoint > 0x10ffffu) return false;
    int min_keycode = 0;
    int max_keycode = 0;
    XDisplayKeycodes(display, &min_keycode, &max_keycode);
    if (max_keycode <= min_keycode) return false;
    const KeyCode keycode = static_cast<KeyCode>(max_keycode);
    KeySym keysym = static_cast<KeySym>(0x01000000u | codepoint);
    XChangeKeyboardMapping(display, static_cast<int>(keycode), 1, &keysym, 1);
    XSync(display, False);
    return send_keycode(display, window, keycode);
}

bool parse_unicode(std::string token, std::uint32_t& out) {
    if (token.starts_with("U+") || token.starts_with("u+")) token.erase(0, 2);
    else return false;
    if (token.empty()) return false;
    char* end = nullptr;
    const unsigned long value = std::strtoul(token.c_str(), &end, 16);
    if (end == token.c_str() || *end != '\0' || value > 0x10fffful) return false;
    out = static_cast<std::uint32_t>(value);
    return true;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: such_x11_input_driver <window-name> <key|U+codepoint>...\n";
        return 2;
    }

    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) return 3;

    const std::string window_name(argv[1]);
    Window window = 0;
    for (int attempt = 0; attempt < 60 && window == 0; ++attempt) {
        window = find_named_window(display, DefaultRootWindow(display), window_name);
        if (window == 0) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (window == 0) {
        XCloseDisplay(display);
        return 4;
    }

    for (int i = 2; i < argc; ++i) {
        const std::string token(argv[i]);
        std::uint32_t codepoint = 0;
        const bool ok = parse_unicode(token, codepoint)
            ? send_unicode(display, window, codepoint)
            : send_named_key(display, window, token);
        if (!ok) {
            std::cerr << "could not send X11 input token: " << token << '\n';
            XCloseDisplay(display);
            return 5;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    XCloseDisplay(display);
    return 0;
}
