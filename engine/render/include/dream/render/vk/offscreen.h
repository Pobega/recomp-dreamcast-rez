#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "dream/render/vk/context.h"
#include "dream/render/vk/renderer.h"

namespace dream::render::vk {

class Offscreen {
public:
    Offscreen() = default;
    ~Offscreen();
    Offscreen(const Offscreen&) = delete;
    Offscreen& operator=(const Offscreen&) = delete;

    bool create(Context& ctx, std::uint32_t width, std::uint32_t height);
    void destroy();

    VkRenderPass render_pass() const noexcept { return 1; }
    VkExtent2D extent() const noexcept { return extent_; }
    std::uint32_t width() const noexcept { return extent_.width; }
    std::uint32_t height() const noexcept { return extent_.height; }

    bool render(Renderer& renderer, const Frame& frame, const FrameGeometry& geometry);
    const std::uint32_t* pixels() noexcept;

    bool readback = true;
    VkImageView sampled_view() const noexcept;
    VkImage colour_image() const noexcept;

    std::function<void(VkCommandBuffer cmd, VkImage colour, VkExtent2D extent)> after_pass;
    VkExtent2D area{};
    bool has_frame() const noexcept { return frames > 0; }
    const std::string& error() const noexcept { return error_; }
    bool wait();

    std::uint64_t frames = 0;

private:
    bool copy_to_host();

    Context* ctx_ = nullptr;
    VkExtent2D extent_{};
    std::uint32_t fbo_ = 0;
    std::uint32_t color_tex_ = 0;
    std::uint32_t depth_rb_ = 0;
    std::vector<std::uint32_t> pixel_buf_;
    bool have_pixels_ = false;
    std::string error_;
};

}  // namespace dream::render::vk
