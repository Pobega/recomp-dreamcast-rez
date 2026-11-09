#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "dream/render/vk/context.h"

namespace dream::render::vk {

struct PresentOptions {
    enum class Fit : std::uint8_t {
        Letterbox,
        Crop,
        Stretch,
    };
    Fit fit = Fit::Letterbox;
    float display_aspect = 0.0f;
};

inline PresentOptions& present_options() {
    static PresentOptions o;
    return o;
}

class Presenter {
public:
    Presenter() = default;
    ~Presenter();
    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;

    bool create(Context& ctx, VkRenderPass render_pass);
    void destroy();

    bool upload(VkCommandBuffer cmd, const std::uint32_t* rgba, std::uint32_t width,
                std::uint32_t height,
                const std::function<void(std::uint32_t* pixels, std::uint32_t width,
                                         std::uint32_t height)>& decorate = {});
    bool draw(VkCommandBuffer cmd, VkExtent2D target);
    void forget_views() {}
    bool draw_view(VkCommandBuffer cmd, VkImageView view, std::uint32_t width,
                   std::uint32_t height, VkExtent2D target);

    bool smooth = false;
    bool overlay_layer = false;

    const std::string& error() const noexcept { return error_; }

private:
    Context* ctx_ = nullptr;
    std::uint32_t program_ = 0;
    std::uint32_t vao_ = 0;
    std::uint32_t tex_ = 0;
    std::int32_t u_scale_ = -1;
    std::int32_t u_offset_ = -1;
    std::int32_t u_texel_ = -1;
    std::int32_t u_taps_ = -1;
    std::int32_t u_mode_ = -1;
    std::int32_t u_image_ = -1;
    std::uint32_t uploaded_width_ = 0;
    std::uint32_t uploaded_height_ = 0;
    std::vector<std::uint32_t> staging_;
    std::string error_;
};

}  // namespace dream::render::vk
