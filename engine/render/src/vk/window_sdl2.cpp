#include "dream/render/vk/window.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include <SDL.h>

namespace dream::render::vk {

namespace {

constexpr int kRumbleMinUv = 500000;
constexpr int kRumbleMaxUv = 3300000;
constexpr const char* kRumblePowerPath = "/sys/class/gpio/gpio227/value";
constexpr const char* kRumbleVoltagePath = "/sys/class/motor/voltage";
constexpr const char* kControllerDbFile = "gamecontrollerdb.txt";

constexpr SDL_Scancode kScancodes[static_cast<unsigned>(Control::Count)] = {
    SDL_SCANCODE_UP,     SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_Z,      SDL_SCANCODE_X,    SDL_SCANCODE_A,    SDL_SCANCODE_S,
    SDL_SCANCODE_RETURN, SDL_SCANCODE_Q,    SDL_SCANCODE_W,    SDL_SCANCODE_F12,
    SDL_SCANCODE_F10,    SDL_SCANCODE_F1,   SDL_SCANCODE_ESCAPE,
};

constexpr SDL_GameControllerButton kNavButtons[static_cast<unsigned>(Control::Count)] = {
    SDL_CONTROLLER_BUTTON_DPAD_UP,    SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_A,         SDL_CONTROLLER_BUTTON_B,
    SDL_CONTROLLER_BUTTON_INVALID,    SDL_CONTROLLER_BUTTON_INVALID,   SDL_CONTROLLER_BUTTON_START,
    SDL_CONTROLLER_BUTTON_INVALID,    SDL_CONTROLLER_BUTTON_INVALID,   SDL_CONTROLLER_BUTTON_INVALID,
    SDL_CONTROLLER_BUTTON_INVALID,    SDL_CONTROLLER_BUTTON_BACK,      SDL_CONTROLLER_BUTTON_B,
};

void write_sysfs_int(const char* path, int value) noexcept {
#if defined(__linux__)
    const int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return;
    char text[16];
    const int n = std::snprintf(text, sizeof text, "%d", value);
    if (n > 0)
        (void)write(fd, text, static_cast<std::size_t>(n));
    close(fd);
#else
    (void)path;
    (void)value;
#endif
}

int read_sysfs_int(const char* path) noexcept {
#if defined(__linux__)
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    char text[16] = {};
    const ssize_t n = read(fd, text, sizeof text - 1);
    close(fd);
    return n > 0 ? std::atoi(text) : -1;
#else
    (void)path;
    return -1;
#endif
}

#if defined(__linux__)
constexpr unsigned long kBitsPerWord = 8 * sizeof(unsigned long);
constexpr unsigned long kEvWords = (EV_MAX + kBitsPerWord) / kBitsPerWord;
constexpr unsigned long kKeyWords = (KEY_MAX + kBitsPerWord) / kBitsPerWord;

bool test_bit(const unsigned long* bitfield, int bit) {
    return (bitfield[bit / kBitsPerWord] & (1UL << (bit % kBitsPerWord))) != 0;
}

int scan_evdev_gamepad() {
    char path[32];
    for (int i = 0; i < 16; ++i) {
        std::snprintf(path, sizeof path, "/dev/input/event%d", i);
        const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        unsigned long ev_bits[kEvWords] = {};
        if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) >= 0 && test_bit(ev_bits, EV_KEY)) {
            unsigned long key_bits[kKeyWords] = {};
            if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) >= 0) {
                if (test_bit(key_bits, BTN_SOUTH) && test_bit(key_bits, BTN_EAST))
                    return fd;
            }
        }
        close(fd);
    }
    return -1;
}

float normalize_abs_axis(int value, const input_absinfo& info) {
    if (info.maximum <= info.minimum)
        return 0.0f;
    const float range = static_cast<float>(info.maximum - info.minimum);
    const float norm = 2.0f * static_cast<float>(value - info.minimum) / range - 1.0f;
    return std::clamp(norm, -1.0f, 1.0f);
}

input_absinfo g_abs_x{};
input_absinfo g_abs_y{};
float g_stick_x = 0.0f;
float g_stick_y = 0.0f;
bool g_dpad_up = false;
bool g_dpad_down = false;
bool g_dpad_left = false;
bool g_dpad_right = false;
bool g_l2_btn = false;
bool g_r2_btn = false;
#endif

}  // namespace

struct Window::EventQueue {
    std::mutex mutex;
    std::vector<SDL_Event> events;
};

Window::~Window() {
    destroy();
}

std::string Window::present_path() const {
    return "SDL2 GLES3";
}

bool Window::create(const char* title, int width, int height) {
    destroy();

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_LINUX_JOYSTICK_DEADZONES, "0");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) != 0) {
        error_ = SDL_GetError();
        return false;
    }

    if (std::filesystem::exists(kControllerDbFile))
        SDL_GameControllerAddMappingsFromFile(kControllerDbFile);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    constexpr int kMaxHandheldWidth = 1280;
    constexpr int kMaxHandheldHeight = 800;

    SDL_DisplayMode dm{};
    const bool has_dm = (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0);
    const bool use_fullscreen =
        has_dm && (fullscreen_ || width >= dm.w || height >= dm.h ||
                   (dm.w <= kMaxHandheldWidth && dm.h <= kMaxHandheldHeight));

    const int win_w = use_fullscreen ? dm.w : width;
    const int win_h = use_fullscreen ? dm.h : height;
    const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | (use_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h, flags);
    if (use_fullscreen)
        fullscreen_ = true;
    if (!window_) {
        error_ = SDL_GetError();
        return false;
    }

    gl_ctx_ = SDL_GL_CreateContext(window_);
    if (!gl_ctx_) {
        error_ = SDL_GetError();
        return false;
    }

    SDL_GL_SetSwapInterval(wanted_present_ == PresentMode::Immediate ? 0 : 1);

    int dw = 0, dh = 0;
    SDL_GL_GetDrawableSize(window_, &dw, &dh);
    extent_ = {static_cast<std::uint32_t>(dw), static_cast<std::uint32_t>(dh)};

    if (!ctx_.create({})) {
        error_ = ctx_.error();
        return false;
    }

#if defined(__linux__)
    input_fd_ = scan_evdev_gamepad();
    if (input_fd_ >= 0) {
        ioctl(input_fd_, EVIOCGABS(ABS_X), &g_abs_x);
        ioctl(input_fd_, EVIOCGABS(ABS_Y), &g_abs_y);
    }
#endif

    refresh_devices();
    return true;
}

void Window::acquire_gl_context() noexcept {
    if (window_ && gl_ctx_ && SDL_GL_GetCurrentContext() != gl_ctx_)
        SDL_GL_MakeCurrent(window_, gl_ctx_);
}

void Window::release_gl_context() noexcept {
    if (window_ && gl_ctx_ && SDL_GL_GetCurrentContext() == gl_ctx_)
        SDL_GL_MakeCurrent(window_, nullptr);
}

bool Window::begin_frame(std::uint32_t& image_index, VkCommandBuffer& cmd) {
    int dw = 0, dh = 0;
    SDL_GL_GetDrawableSize(window_, &dw, &dh);
    extent_ = {static_cast<std::uint32_t>(dw), static_cast<std::uint32_t>(dh)};
    image_index = 0;
    cmd = nullptr;
    return true;
}

bool Window::end_frame(std::uint32_t) {
    SDL_GL_SwapWindow(window_);
    last_flip_ = std::chrono::steady_clock::now();
    return true;
}

bool Window::recreate_swapchain() {
    int dw = 0, dh = 0;
    if (window_)
        SDL_GL_GetDrawableSize(window_, &dw, &dh);
    extent_ = {static_cast<std::uint32_t>(dw), static_cast<std::uint32_t>(dh)};
    return true;
}

void Window::destroy() {
    rumble_off();
    if (rumble_uv_ != 0 && rumble_found_uv_ > 0) {
        write_sysfs_int(kRumbleVoltagePath, rumble_found_uv_);
        rumble_uv_ = 0;
    }
    for (SDL_Gamepad* g : pads_) {
        if (g)
            SDL_GameControllerClose(g);
    }
    pads_.clear();
    devices_.clear();

#if defined(__linux__)
    if (input_fd_ >= 0) {
        close(input_fd_);
        input_fd_ = -1;
    }
#endif

    ctx_.destroy();

    if (gl_ctx_) {
        SDL_GL_DeleteContext(gl_ctx_);
        gl_ctx_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

void Window::set_fullscreen(bool on) noexcept {
    fullscreen_ = on;
    if (window_) {
        SDL_SetWindowFullscreen(window_, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        int dw = 0, dh = 0;
        SDL_GL_GetDrawableSize(window_, &dw, &dh);
        extent_ = {static_cast<std::uint32_t>(dw), static_cast<std::uint32_t>(dh)};
    }
}

float Window::refresh_rate() const noexcept {
    if (!window_)
        return 0.0f;
    const int idx = SDL_GetWindowDisplayIndex(window_);
    SDL_DisplayMode dm;
    if (idx >= 0 && SDL_GetCurrentDisplayMode(idx, &dm) == 0 && dm.refresh_rate > 0)
        return static_cast<float>(dm.refresh_rate);
    return 60.0f;
}

bool Window::vblank_grid(std::chrono::steady_clock::time_point& vblank,
                         std::chrono::nanoseconds& period) const noexcept {
    if (last_flip_.time_since_epoch().count() == 0)
        return false;
    vblank = last_flip_;
    const float rate = refresh_rate();
    const double hz = (rate > 0.0f) ? static_cast<double>(rate) : 60.0;
    period = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(1.0 / hz));
    return true;
}

void Window::pixel_size(int& w, int& h) const noexcept {
    if (window_)
        SDL_GL_GetDrawableSize(window_, &w, &h);
    else
        w = h = 0;
}

void Window::pump_events(int timeout_ms) {
    if (!queue_)
        queue_ = std::make_shared<EventQueue>();
    SDL_Event ev;
    const bool got = timeout_ms > 0 ? SDL_WaitEventTimeout(&ev, timeout_ms) : SDL_PollEvent(&ev);
    if (!got)
        return;
    std::lock_guard<std::mutex> lock(queue_->mutex);
    queue_->events.push_back(ev);
    while (SDL_PollEvent(&ev))
        queue_->events.push_back(ev);
}

bool Window::poll() {
    std::vector<SDL_Event> events;
    if (threaded_ && queue_) {
        std::lock_guard<std::mutex> lock(queue_->mutex);
        events.swap(queue_->events);
    } else {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            events.push_back(ev);
    }

    bool captured_this_poll = false;
    for (const SDL_Event& ev : events) {
        if (event_filter && event_filter(&ev))
            continue;

        switch (ev.type) {
            case SDL_QUIT:
                running_ = false;
                break;
            case SDL_CONTROLLERDEVICEADDED:
            case SDL_CONTROLLERDEVICEREMOVED:
                refresh_devices();
                break;
            case SDL_KEYDOWN:
                if (capturing_ && !captured_this_poll) {
                    captured_this_poll = true;
                    captured_ = true;
                    capturing_ = false;
                    if (ev.key.keysym.sym == SDLK_ESCAPE) {
                        capture_cancelled_ = true;
                    } else if (const char* n = SDL_GetScancodeName(ev.key.keysym.scancode); n && *n) {
                        capture_ = Binding{BindSource::Key, n, 1};
                    } else {
                        capture_cancelled_ = true;
                    }
                } else if (ev.key.keysym.sym == SDLK_ESCAPE && escape_quits_) {
                    running_ = false;
                } else if (ev.key.keysym.sym == SDLK_RETURN && (ev.key.keysym.mod & KMOD_ALT) && !ev.key.repeat) {
                    set_fullscreen(!fullscreen_);
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                if (capturing_ && !captured_this_poll) {
                    if (const char* n = SDL_GameControllerGetStringForButton(
                            static_cast<SDL_GameControllerButton>(ev.cbutton.button));
                        n && *n) {
                        captured_this_poll = true;
                        captured_ = true;
                        capturing_ = false;
                        capture_ = Binding{BindSource::Button, n, 1};
                    }
                }
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (capturing_ && !captured_this_poll && std::abs(ev.caxis.value) > 20000) {
                    if (const char* n = SDL_GameControllerGetStringForAxis(
                            static_cast<SDL_GameControllerAxis>(ev.caxis.axis));
                        n && *n) {
                        captured_this_poll = true;
                        captured_ = true;
                        capturing_ = false;
                        capture_ = Binding{BindSource::Axis, n, ev.caxis.value < 0 ? -1 : 1};
                    }
                }
                break;
            default:
                break;
        }
    }

#if defined(__linux__)
    if (input_fd_ >= 0) {
        input_event ev;
        while (read(input_fd_, &ev, sizeof ev) == static_cast<ssize_t>(sizeof ev)) {
            if (ev.type == EV_KEY) {
                if (ev.code >= BTN_SOUTH && ev.code < BTN_SOUTH + 32) {
                    const std::uint32_t bit = 1u << (ev.code - BTN_SOUTH);
                    input_buttons_ = ev.value ? (input_buttons_ | bit) : (input_buttons_ & ~bit);
                }
                switch (ev.code) {
                    case BTN_DPAD_UP:    g_dpad_up = (ev.value != 0); break;
                    case BTN_DPAD_DOWN:  g_dpad_down = (ev.value != 0); break;
                    case BTN_DPAD_LEFT:  g_dpad_left = (ev.value != 0); break;
                    case BTN_DPAD_RIGHT: g_dpad_right = (ev.value != 0); break;
                    case BTN_TL2:        g_l2_btn = (ev.value != 0); break;
                    case BTN_TR2:        g_r2_btn = (ev.value != 0); break;
                    default: break;
                }
            } else if (ev.type == EV_ABS) {
                if (ev.code == ABS_HAT0X)
                    input_hat_x_ = ev.value;
                else if (ev.code == ABS_HAT0Y)
                    input_hat_y_ = ev.value;
                else if (ev.code == ABS_Z)
                    input_l2_ = ev.value;
                else if (ev.code == ABS_RZ)
                    input_r2_ = ev.value;
                else if (ev.code == ABS_X)
                    g_stick_x = normalize_abs_axis(ev.value, g_abs_x);
                else if (ev.code == ABS_Y)
                    g_stick_y = normalize_abs_axis(ev.value, g_abs_y);
            }
        }
        const bool ev_mode = (input_buttons_ & (1u << (BTN_MODE - BTN_SOUTH))) != 0;
        const bool ev_back = (input_buttons_ & (1u << (BTN_SELECT - BTN_SOUTH))) != 0;
        const bool ev_start = (input_buttons_ & (1u << (BTN_START - BTN_SOUTH))) != 0;
        if ((ev_mode && ev_start) || (ev_back && ev_start))
            running_ = false;
    }
#endif

    for (SDL_Gamepad* g : pads_) {
        if (!g) continue;
        const bool guide = SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_GUIDE);
        const bool back = SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_BACK);
        const bool start = SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_START);
        if ((guide && start) || (back && start))
            running_ = false;
    }

    if (rumble_on_ && (!running_ || game_input_blocked_ || std::chrono::steady_clock::now() >= rumble_until_))
        rumble_off();

    bool has_open_controller = false;
    for (SDL_Gamepad* g : pads_) {
        if (g) {
            has_open_controller = true;
            break;
        }
    }

    bool pad_toggle_fps = false;
    for (SDL_Gamepad* g : pads_) {
        if (!g) continue;
        const bool guide = SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_GUIDE);
        const bool back = SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_BACK);
        const bool y_btn = SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_Y);
        if ((guide || back) && y_btn) {
            pad_toggle_fps = true;
            break;
        }
    }
#if defined(__linux__)
    if (!has_open_controller && input_fd_ >= 0) {
        const bool ev_mode = (input_buttons_ & (1u << (BTN_MODE - BTN_SOUTH))) != 0;
        const bool ev_back = (input_buttons_ & (1u << (BTN_SELECT - BTN_SOUTH))) != 0;
        const bool ev_north = (input_buttons_ & (1u << (BTN_NORTH - BTN_SOUTH))) != 0;
        if ((ev_mode || ev_back) && ev_north)
            pad_toggle_fps = true;
    }
#endif

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    for (unsigned i = 0; i < static_cast<unsigned>(Control::Count); ++i) {
        const bool was = held_[i];
        bool now = keys && keys[kScancodes[i]];
        if (kNavButtons[i] != SDL_CONTROLLER_BUTTON_INVALID &&
            (pad_menu_button_ || i != static_cast<unsigned>(Control::Menu))) {
            for (SDL_Gamepad* g : pads_) {
                if (g && SDL_GameControllerGetButton(g, kNavButtons[i]))
                    now = true;
            }
        }
        if (i == static_cast<unsigned>(Control::ToggleFps) && pad_toggle_fps)
            now = true;
        if (i == static_cast<unsigned>(Control::Menu) && pad_toggle_fps)
            now = false;
        held_[i] = now;
        pressed_[i] = now && !was;
    }

#if defined(__linux__)
    const bool ev_start_down = (!has_open_controller && input_fd_ >= 0)
                                   ? ((input_buttons_ & (1u << (BTN_START - BTN_SOUTH))) != 0)
                                   : false;
#else
    const bool ev_start_down = false;
#endif
    const bool start_held = (keys && keys[SDL_SCANCODE_RETURN]) || ev_start_down ||
                            held_[static_cast<unsigned>(Control::Start)];
    if (keys && keys[SDL_SCANCODE_ESCAPE]) {
        if (escape_quits_ || start_held)
            running_ = false;
    }

    const auto now_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    const float dt = (last_poll_ns_ != 0 && now_ns > last_poll_ns_)
                         ? static_cast<float>(now_ns - last_poll_ns_) * 1e-9f
                         : 1.0f / 60.0f;
    last_poll_ns_ = now_ns;

    for (unsigned p = 0; p < players(); ++p) {
        SDL_Gamepad* pad = p < pads_.size() ? pads_[p] : nullptr;
        for (unsigned c = 0; c < kPadControlCount; ++c) {
            const bool was = pad_held_[p][c];
            bool now = false;
            float val = 0.0f;

            if (p == 0 && keys) {
                for (unsigned slot = 0; slot < kBindingSlots; ++slot) {
                    const auto& r = key_[slot][c];
                    if (r.source == BindSource::Key && r.code >= 0 && keys[r.code]) {
                        now = true;
                        val = std::max(val, 1.0f);
                    }
                }
            }

            if (pad) {
                for (unsigned slot = 0; slot < kBindingSlots; ++slot) {
                    const auto& r = gpad_[slot][c];
                    if (r.source == BindSource::Button && r.code >= 0) {
                        if (SDL_GameControllerGetButton(pad, static_cast<SDL_GameControllerButton>(r.code))) {
                            now = true;
                            val = std::max(val, 1.0f);
                        }
                    } else if (r.source == BindSource::Axis && r.code >= 0) {
                        const Sint16 ax = SDL_GameControllerGetAxis(pad, static_cast<SDL_GameControllerAxis>(r.code));
                        const float signed_val = static_cast<float>(ax) / 32767.0f;
                        if (r.sign > 0 && signed_val > 0.2f) {
                            now = true;
                            val = std::max(val, (signed_val - 0.2f) / 0.8f);
                        } else if (r.sign < 0 && signed_val < -0.2f) {
                            now = true;
                            val = std::max(val, (-signed_val - 0.2f) / 0.8f);
                        }
                    }
                }
            }

#if defined(__linux__)
            if (p == 0 && !has_open_controller && input_fd_ >= 0) {
                constexpr float kDeadzone = 0.2f;
                switch (static_cast<PadControl>(c)) {
                    case PadControl::Up:
                        if (g_dpad_up || input_hat_y_ < 0) { now = true; val = 1.0f; }
                        break;
                    case PadControl::Down:
                        if (g_dpad_down || input_hat_y_ > 0) { now = true; val = 1.0f; }
                        break;
                    case PadControl::Left:
                        if (g_dpad_left || input_hat_x_ < 0) { now = true; val = 1.0f; }
                        break;
                    case PadControl::Right:
                        if (g_dpad_right || input_hat_x_ > 0) { now = true; val = 1.0f; }
                        break;
                    case PadControl::StickUp:
                        if (g_stick_y < -kDeadzone) {
                            now = true;
                            val = std::max(val, (-g_stick_y - kDeadzone) / (1.0f - kDeadzone));
                        }
                        break;
                    case PadControl::StickDown:
                        if (g_stick_y > kDeadzone) {
                            now = true;
                            val = std::max(val, (g_stick_y - kDeadzone) / (1.0f - kDeadzone));
                        }
                        break;
                    case PadControl::StickLeft:
                        if (g_stick_x < -kDeadzone) {
                            now = true;
                            val = std::max(val, (-g_stick_x - kDeadzone) / (1.0f - kDeadzone));
                        }
                        break;
                    case PadControl::StickRight:
                        if (g_stick_x > kDeadzone) {
                            now = true;
                            val = std::max(val, (g_stick_x - kDeadzone) / (1.0f - kDeadzone));
                        }
                        break;
                    case PadControl::A:
                        if (input_buttons_ & (1u << (BTN_SOUTH - BTN_SOUTH))) { now = true; val = 1.0f; }
                        break;
                    case PadControl::B:
                        if (input_buttons_ & (1u << (BTN_EAST - BTN_SOUTH))) { now = true; val = 1.0f; }
                        break;
                    case PadControl::X:
                        if (input_buttons_ & (1u << (BTN_WEST - BTN_SOUTH))) { now = true; val = 1.0f; }
                        break;
                    case PadControl::Y:
                        if (input_buttons_ & (1u << (BTN_NORTH - BTN_SOUTH))) { now = true; val = 1.0f; }
                        break;
                    case PadControl::Start:
                        if (input_buttons_ & (1u << (BTN_START - BTN_SOUTH))) { now = true; val = 1.0f; }
                        break;
                    case PadControl::LeftTrigger:
                        if ((input_buttons_ & (1u << (BTN_TL - BTN_SOUTH))) || g_l2_btn || input_l2_ > 128) {
                            now = true;
                            val = (input_l2_ > 128) ? static_cast<float>(input_l2_) / 255.0f : 1.0f;
                        }
                        break;
                    case PadControl::RightTrigger:
                        if ((input_buttons_ & (1u << (BTN_TR - BTN_SOUTH))) || g_r2_btn || input_r2_ > 128) {
                            now = true;
                            val = (input_r2_ > 128) ? static_cast<float>(input_r2_) / 255.0f : 1.0f;
                        }
                        break;
                    default:
                        break;
                }
            }
#endif

            if (capturing_ || game_input_blocked_) {
                now = false;
                val = 0.0f;
            }

            pad_held_[p][c] = now;
            pad_pressed_[p][c] = now && !was;

            if (now) {
                ramp_[p][c] = std::min(1.0f, ramp_[p][c] + dt * (1.0f / 0.15f));
            } else {
                ramp_[p][c] = 0.0f;
            }

            if (static_cast<PadControl>(c) == PadControl::LeftTrigger ||
                static_cast<PadControl>(c) == PadControl::RightTrigger) {
                val = std::max(val, ramp_[p][c]);
            }
            pad_value_[p][c] = val;
        }
    }

    return running_;
}

bool Window::held(Control c) const noexcept {
    return held_[static_cast<unsigned>(c)];
}

bool Window::pressed(Control c) const noexcept {
    return pressed_[static_cast<unsigned>(c)];
}

bool Window::pad_held(PadControl c) const noexcept {
    return pad_held(0, c);
}

bool Window::pad_pressed(PadControl c) const noexcept {
    return pad_pressed_[0][static_cast<unsigned>(c)];
}

float Window::pad_value(PadControl c) const noexcept {
    return pad_value(0, c);
}

bool Window::pad_held(unsigned player, PadControl c) const noexcept {
    return player < kMaxPlayers && pad_held_[player][static_cast<unsigned>(c)];
}

float Window::pad_value(unsigned player, PadControl c) const noexcept {
    return player < kMaxPlayers ? pad_value_[player][static_cast<unsigned>(c)] : 0.0f;
}

void Window::refresh_devices() {
    for (SDL_Gamepad* g : pads_) {
        if (g)
            SDL_GameControllerClose(g);
    }
    pads_.clear();
    devices_.clear();
    devices_.push_back("KEYBOARD");

    const int n = SDL_NumJoysticks();
    for (int i = 0; i < n; ++i) {
        if (SDL_IsGameController(i)) {
            SDL_Gamepad* g = SDL_GameControllerOpen(i);
            if (g) {
                pads_.push_back(g);
                const char* name = SDL_GameControllerName(g);
                devices_.push_back(name ? name : "Gamepad");
            }
        }
    }
}

void Window::set_bindings(const Bindings& b) {
    bindings_ = b;
    for (unsigned slot = 0; slot < kBindingSlots; ++slot) {
        for (unsigned c = 0; c < kPadControlCount; ++c) {
            const auto ctrl = static_cast<PadControl>(c);
            key_[slot][c] = {};
            const Binding& kbind = b.keyboard.slot(ctrl, slot);
            if (kbind.source == BindSource::Key) {
                key_[slot][c].source = BindSource::Key;
                key_[slot][c].code = SDL_GetScancodeFromName(kbind.code.c_str());
                key_[slot][c].sign = kbind.sign;
            }
            gpad_[slot][c] = {};
            const Binding& gbind = b.gamepad.slot(ctrl, slot);
            if (gbind.source == BindSource::Button) {
                gpad_[slot][c].source = BindSource::Button;
                gpad_[slot][c].code = SDL_GameControllerGetButtonFromString(gbind.code.c_str());
                gpad_[slot][c].sign = gbind.sign;
            } else if (gbind.source == BindSource::Axis) {
                gpad_[slot][c].source = BindSource::Axis;
                gpad_[slot][c].code = SDL_GameControllerGetAxisFromString(gbind.code.c_str());
                gpad_[slot][c].sign = gbind.sign;
            }
        }
    }
}

void Window::begin_capture() noexcept {
    capturing_ = true;
    captured_ = false;
    capture_cancelled_ = false;
    capture_ = {};
}

void Window::cancel_capture() noexcept {
    capturing_ = false;
    captured_ = false;
    capture_cancelled_ = true;
    capture_ = {};
}

bool Window::take_capture(Binding& out, bool& cancelled) noexcept {
    if (!captured_ && !capture_cancelled_)
        return false;
    cancelled = capture_cancelled_;
    out = capture_;
    captured_ = false;
    capture_cancelled_ = false;
    return true;
}

void Window::rumble_off() noexcept {
    rumble_on_ = false;
    write_sysfs_int(kRumblePowerPath, 0);
}

void Window::rumble(unsigned player, float strength, unsigned ms) noexcept {
    const float scaled = std::clamp(strength * rumble_scale, 0.0f, 1.0f);
    if (scaled <= 0.0f || ms == 0) {
        rumble_off();
        if (player < pads_.size() && pads_[player])
            SDL_GameControllerRumble(pads_[player], 0, 0, 0);
        return;
    }

    if (player < pads_.size() && pads_[player]) {
        const Uint16 mag = static_cast<Uint16>(scaled * 65535.0f);
        SDL_GameControllerRumble(pads_[player], mag, mag, ms);
        return;
    }

    const int uv = kRumbleMinUv + static_cast<int>(scaled * static_cast<float>(kRumbleMaxUv - kRumbleMinUv));
    if (rumble_found_uv_ < 0)
        rumble_found_uv_ = read_sysfs_int(kRumbleVoltagePath);
    if (uv != rumble_uv_) {
        write_sysfs_int(kRumbleVoltagePath, uv);
        rumble_uv_ = uv;
    }
    write_sysfs_int(kRumblePowerPath, 1);
    rumble_on_ = true;
    rumble_until_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
}

std::string default_bindings_path() {
    char* dir = SDL_GetPrefPath("", "rez_recompiled");
    if (!dir)
        return {};
    std::string path = std::string(dir) + "bindings.txt";
    SDL_free(dir);
    return path;
}

}  // namespace dream::render::vk
