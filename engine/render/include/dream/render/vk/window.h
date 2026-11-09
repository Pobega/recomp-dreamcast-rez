#pragma once

#include <chrono>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "dream/render/input.h"
#include "dream/render/vk/context.h"
#include "dream/render/vk/resources.h"

struct SDL_Window;
struct _SDL_GameController;
using SDL_Gamepad = struct _SDL_GameController;

namespace dream::render::vk {

enum class Control : unsigned {
    Up,
    Down,
    Left,
    Right,
    A,
    B,
    X,
    Y,
    Start,
    LeftTrigger,
    RightTrigger,
    Screenshot,
    ToggleFps,
    Menu,
    Back,
    Count
};

class Window {
public:
    Window() = default;
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    enum class PresentMode { Fifo, Mailbox, Immediate };
    void set_present_mode(PresentMode m) noexcept { wanted_present_ = m; }
    enum class PresentPath { Auto, Vulkan, Dxgi };
    void set_present_path(PresentPath p) noexcept { wanted_path_ = p; }
    std::string present_path() const;

    bool flips() const noexcept { return false; }
    int flip_queue_depth() const noexcept { return 0; }

    bool create(const char* title, int width, int height);
    void destroy();

    void acquire_gl_context() noexcept;
    void release_gl_context() noexcept;

    bool begin_frame(std::uint32_t& image_index, VkCommandBuffer& cmd);
    bool end_frame(std::uint32_t image_index);
    bool blits() const noexcept { return false; }
    void blit_frame(VkCommandBuffer, VkImage, VkExtent2D, bool) {}
    bool recreate_swapchain();

    bool poll();
    void set_fullscreen(bool on) noexcept;
    bool fullscreen() const noexcept { return fullscreen_; }
    float refresh_rate() const noexcept;
    bool vblank_grid(std::chrono::steady_clock::time_point& vblank,
                     std::chrono::nanoseconds& period) const noexcept;

    bool held(Control c) const noexcept;
    bool pressed(Control c) const noexcept;

    void set_escape_quits(bool yes) noexcept { escape_quits_ = yes; }
    bool escape_quits() const noexcept { return escape_quits_; }

    std::function<bool(const void* sdl_event)> event_filter;
    void set_pad_menu_button(bool yes) noexcept { pad_menu_button_ = yes; }
    void set_game_input_blocked(bool yes) noexcept { game_input_blocked_ = yes; }
    bool game_input_blocked() const noexcept { return game_input_blocked_; }

    void set_threaded(bool yes) noexcept { threaded_ = yes; }
    void pump_events(int timeout_ms);
    void request_close() noexcept { running_ = false; }
    void pixel_size(int& w, int& h) const noexcept;

    void set_bindings(const Bindings& b);
    const Bindings& bindings() const noexcept { return bindings_; }

    static constexpr unsigned kMaxPlayers = 4;
    unsigned players() const noexcept {
        return pads_.empty() ? 1u : static_cast<unsigned>(std::min<std::size_t>(pads_.size(), kMaxPlayers));
    }
    bool pad_held(unsigned player, PadControl c) const noexcept;
    float pad_value(unsigned player, PadControl c) const noexcept;
    void rumble(unsigned player, float strength, unsigned ms) noexcept;
    float rumble_scale = 1.0f;

    bool pad_held(PadControl c) const noexcept;
    bool pad_pressed(PadControl c) const noexcept;
    float pad_value(PadControl c) const noexcept;

    const std::vector<std::string>& devices() const noexcept { return devices_; }
    bool gamepad_connected() const noexcept { return !pads_.empty(); }

    void begin_capture() noexcept;
    void cancel_capture() noexcept;
    bool capturing() const noexcept { return capturing_; }
    bool take_capture(Binding& out, bool& cancelled) noexcept;

    Context& context() noexcept { return ctx_; }
    VkRenderPass render_pass() const noexcept { return 1; }
    VkFramebuffer framebuffer(std::uint32_t) const noexcept { return 0; }
    VkExtent2D extent() const noexcept { return extent_; }
    const std::string& error() const noexcept { return error_; }

private:
    SDL_Window* window_ = nullptr;
    void* gl_ctx_ = nullptr;
    Context ctx_;
    VkExtent2D extent_{};
    std::chrono::steady_clock::time_point last_flip_{};

    bool held_[static_cast<unsigned>(Control::Count)]{};
    bool pressed_[static_cast<unsigned>(Control::Count)]{};
    bool running_ = true;
    bool threaded_ = false;
    struct EventQueue;
    std::shared_ptr<EventQueue> queue_;
    bool fullscreen_ = false;
    std::string error_;

    struct Resolved {
        BindSource source = BindSource::None;
        int code = 0;
        int sign = 1;
    };
    Bindings bindings_;
    Resolved key_[kBindingSlots][kPadControlCount]{};
    Resolved gpad_[kBindingSlots][kPadControlCount]{};
    bool pad_held_[kMaxPlayers][kPadControlCount]{};
    bool pad_pressed_[kMaxPlayers][kPadControlCount]{};
    float pad_value_[kMaxPlayers][kPadControlCount]{};
    float ramp_[kMaxPlayers][kPadControlCount]{};
    std::uint64_t last_poll_ns_ = 0;

    std::vector<SDL_Gamepad*> pads_;
    std::vector<std::string> devices_;
    void refresh_devices();

    bool capturing_ = false, captured_ = false, capture_cancelled_ = false;
#if defined(DREAMCOMP_PORTMASTER)
    bool escape_quits_ = false;
#else
    bool escape_quits_ = true;
#endif
    bool pad_menu_button_ = true;
    bool game_input_blocked_ = false;
    PresentMode wanted_present_ = PresentMode::Fifo;
    PresentPath wanted_path_ = PresentPath::Auto;

    int input_fd_ = -1;
    std::uint32_t input_buttons_ = 0;
    int input_hat_x_ = 0, input_hat_y_ = 0, input_l2_ = 0, input_r2_ = 0;

    bool rumble_on_ = false;
    int rumble_uv_ = 0;
    int rumble_found_uv_ = -1;
    std::chrono::steady_clock::time_point rumble_until_{};
    void rumble_off() noexcept;
    Binding capture_{};
};

std::string default_bindings_path();

}  // namespace dream::render::vk
