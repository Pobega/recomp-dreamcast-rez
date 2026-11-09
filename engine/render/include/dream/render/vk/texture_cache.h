#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "dream/render/texture.h"
#include "dream/render/texture_pack.h"
#include "dream/render/vk/context.h"
#include "dream/render/vk/resources.h"

namespace dream::render::vk {

class TextureCache {
public:
    TextureCache() = default;
    ~TextureCache();
    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    bool create(Context& ctx, VkDescriptorSetLayout layout = 0);
    void destroy();

    void set_memory(const std::uint8_t* vram, std::size_t vram_size,
                    const std::uint32_t* palette_ram, PaletteFormat palette_format);
    void invalidate();
    bool set_palette(const std::uint32_t* palette_ram, PaletteFormat format);
    std::size_t invalidate_range(std::uint32_t begin, std::uint32_t end, bool rendered = true);
    VkDescriptorSet get(VkCommandBuffer cmd, std::uint32_t tcw, std::uint32_t tsp, bool tiled);

    void capture_rtt(VkCommandBuffer cmd, VkImage src, std::uint32_t src_w, std::uint32_t src_h,
                     std::uint32_t address, std::uint32_t width, std::uint32_t height);
    void drop_rtt();
    VkDescriptorSet fallback(VkCommandBuffer cmd);

    std::size_t size() const noexcept { return entries_.size(); }
    void begin_frame();
    const std::string& error() const noexcept { return error_; }

    TextureReplacer& replacer() noexcept { return replacer_; }

private:
    struct Entry {
        Entry() = default;
        Entry(Entry&&) noexcept = default;
        Entry& operator=(Entry&&) noexcept = default;
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;

        std::uint32_t tex = 0;
        std::uint32_t sampler = 0;
        TextureInfo info;
        std::uint64_t last_frame = 0;
        std::uint64_t palette_hash = 0;
    };

    struct Meta {
        bool ok = false, indexed = false;
        std::uint32_t palette_base = 0, palette_count = 0;
    };
    std::unordered_map<std::uint64_t, Meta> meta_;

    struct Rtt {
        std::uint32_t tex = 0;
        std::uint32_t fbo = 0;
        std::uint32_t image_w = 0, image_h = 0;
        std::uint32_t bytes = 0;
        std::unordered_map<std::uint32_t, VkDescriptorSet> sets;
    };
    std::unordered_map<std::uint32_t, Rtt> rtt_;
    std::vector<std::pair<std::uint64_t, Rtt>> rtt_retired_;
    void free_rtt(Rtt& r);
    VkDescriptorSet rtt_set(std::uint32_t tcw, std::uint32_t tsp, bool tiled);

    std::uint64_t bank_hash_[64]{};
    void rehash_banks();
    std::uint64_t frame_ = 1;

    void free_entry(Entry& e);
    std::uint32_t sampler_for(std::uint32_t tsp, bool tiled, bool mipmapped);
    bool upload(VkCommandBuffer cmd, Entry& entry, const std::vector<std::uint32_t>& pixels,
                std::uint32_t width, std::uint32_t height,
                const std::vector<std::vector<std::uint32_t>>* mips = nullptr);
    void update_in_place(Entry& entry, const std::vector<std::uint32_t>& pixels,
                         std::uint32_t width, std::uint32_t height,
                         const std::vector<std::vector<std::uint32_t>>* mips = nullptr);
    bool rendered_into(const TextureInfo& info) const;

    Context* ctx_ = nullptr;
    const std::uint8_t* vram_ = nullptr;
    std::size_t vram_size_ = 0;
    std::uint32_t palette_[1024]{};
    const std::uint32_t* palette_ram_ = nullptr;
    PaletteFormat palette_format_ = PaletteFormat::Argb1555;
    std::unordered_map<std::uint64_t, Entry> entries_;
    std::unordered_map<std::uint32_t, std::uint32_t> samplers_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> rendered_ranges_;
    std::uint32_t fallback_tex_ = 0;
    std::uint32_t fallback_sampler_ = 0;
    VkDescriptorSet fallback_set_ = 0;
    TextureReplacer replacer_;
    std::string error_;
};

}  // namespace dream::render::vk
