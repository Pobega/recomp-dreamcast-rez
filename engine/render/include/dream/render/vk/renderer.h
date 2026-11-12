#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "dream/render/display_list.h"
#include "dream/render/fog.h"
#include "dream/render/vk/context.h"
#include "dream/render/vk/resources.h"
#include "dream/render/vk/texture_cache.h"

namespace dream::render::vk {

struct FrameGeometry {
    float left = 0, top = 0;
    float width = 640, height = 480;
    float alpha_ref = 1.0f / 255.0f;
    float shadow_scale = 0.5f;
    bool autosort = true;
    FogSettings fog;
};

struct GeomUniforms {
    std::int32_t u_scale = -1;
    std::int32_t u_offset = -1;
    std::int32_t u_depth = -1;
    std::int32_t u_mode = -1;
    std::int32_t u_alpha_ref = -1;
    std::int32_t u_fog_vert = -1;
    std::int32_t u_fog_ram = -1;
    std::int32_t u_clip = -1;
    std::int32_t u_tex = -1;
};
using ShaderUniforms = GeomUniforms;

class Renderer {
public:
    enum class ModVol : std::uint32_t { Xor, Or, Inclusion, Exclusion, Final };

    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool create(Context& ctx, VkRenderPass render_pass = 0);
    void set_memory(const std::uint8_t* vram, std::size_t vram_size,
                    const std::uint32_t* palette_ram, PaletteFormat palette_format);
    TextureCache& textures() noexcept { return textures_; }
    void set_render_pass(VkRenderPass) noexcept {}
    void use_pipeline_cache(const std::string& path);
    std::uint32_t prewarm(const std::vector<std::uint32_t>& keys);
    std::uint32_t prewarmed = 0;
    void destroy();

    void draw(VkCommandBuffer cmd, const Frame& frame, const FrameGeometry& geometry,
              VkExtent2D target);
    VkExtent2D area{};
    void prepare(VkCommandBuffer upload, const Frame& frame);

    const std::string& error() const noexcept { return error_; }

private:
    struct PipelineKey {
        std::uint32_t depth_mode : 3;
        std::uint32_t depth_write : 1;
        std::uint32_t cull_mode : 2;
        std::uint32_t src_blend : 3;
        std::uint32_t dst_blend : 3;
        std::uint32_t blend : 1;
        std::uint32_t shadow : 1;
        std::uint32_t discard : 1;
        std::uint32_t shader : 10;
        std::uint32_t padding : 7;
        std::uint32_t bits() const noexcept {
            return depth_mode | (depth_write << 3) | (cull_mode << 4) | (src_blend << 6) |
                   (dst_blend << 9) | (blend << 12) | (shadow << 13) | (discard << 14) | (shader << 16);
        }
        bool operator==(const PipelineKey& o) const noexcept { return bits() == o.bits(); }
    };
    static PipelineKey key_from_bits(std::uint32_t bits);

    void draw_modifier_volumes(const Frame& frame, const FrameGeometry& geometry,
                               const float depth_params[3]);

    Context* ctx_ = nullptr;
    std::uint32_t prog_geom_nd_ = 0;
    std::uint32_t prog_geom_d_ = 0;
    std::uint32_t prog_modvol_ = 0;

    GeomUniforms u_geom_nd_;
    GeomUniforms u_geom_d_;
    std::int32_t u_mv_scale_ = -1;
    std::int32_t u_mv_offset_ = -1;
    std::int32_t u_mv_depth_ = -1;
    std::int32_t u_mv_alpha_ = -1;

    std::uint32_t geom_vao_ = 0;
    std::uint32_t modvol_vao_ = 0;

    TextureCache textures_;
    std::unordered_map<std::uint64_t, VkDescriptorSet> frame_sets_;
    VkDescriptorSet fallback_set_ = 0;

    static constexpr unsigned kVertexRing = 4;
    HostBuffer vertex_ring_[kVertexRing];
    HostBuffer index_ring_[kVertexRing];
    unsigned ring_next_ = 0;

    static constexpr unsigned kModvolRing = 4;
    HostBuffer modvol_ring_[kModvolRing];
    unsigned modvol_next_ = 0;
    std::vector<ModifierTriangle> modvol_tris_;

    std::vector<float> staging_;
    std::string error_;
    std::string pipeline_cache_path_;
};

}  // namespace dream::render::vk
