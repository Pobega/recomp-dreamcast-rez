#include "dream/render/vk/texture_cache.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <GLES3/gl3.h>

#include "dream/render/png.h"
#include "dream/render/tsp.h"

namespace dream::render::vk {

namespace {

GLenum filter_for(std::uint32_t tsp, bool min_filter, bool mipmapped) {
    const bool bilinear = tsp_filter_mode(tsp) != 0;
    if (!min_filter || !mipmapped)
        return bilinear ? GL_LINEAR : GL_NEAREST;
    const bool trilinear = tsp_filter_mode(tsp) >= 2;
    if (bilinear)
        return trilinear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST;
    return trilinear ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
}

GLenum wrap_for(bool clamp, bool flip, bool tiled) {
    if (clamp || !tiled)
        return GL_CLAMP_TO_EDGE;
    return flip ? GL_MIRRORED_REPEAT : GL_REPEAT;
}

}  // namespace

TextureCache::~TextureCache() {
    destroy();
}

bool TextureCache::create(Context& ctx, VkDescriptorSetLayout) {
    ctx_ = &ctx;
    return true;
}

void TextureCache::destroy() {
    invalidate();
    for (auto& [key, s] : samplers_) {
        if (s)
            glDeleteSamplers(1, &s);
    }
    samplers_.clear();
    if (fallback_tex_) {
        glDeleteTextures(1, &fallback_tex_);
        fallback_tex_ = 0;
    }
    if (fallback_sampler_) {
        glDeleteSamplers(1, &fallback_sampler_);
        fallback_sampler_ = 0;
    }
    fallback_set_ = 0;
}

void TextureCache::set_memory(const std::uint8_t* vram, std::size_t vram_size,
                              const std::uint32_t* palette_ram, PaletteFormat palette_format) {
    vram_ = vram;
    vram_size_ = vram_size;
    palette_format_ = palette_format;
    palette_ram_ = palette_ram;
    if (palette_ram)
        unpack_palette(palette_ram, palette_format, palette_);
    rehash_banks();
    invalidate();
}

bool TextureCache::set_palette(const std::uint32_t* palette_ram, PaletteFormat format) {
    if (!palette_ram)
        return false;
    std::uint32_t unpacked[1024]{};
    unpack_palette(palette_ram, format, unpacked);
    if (std::memcmp(unpacked, palette_, sizeof palette_) == 0)
        return false;
    std::memcpy(palette_, unpacked, sizeof palette_);
    palette_ram_ = palette_ram;
    palette_format_ = format;
    rehash_banks();
    return true;
}

void TextureCache::rehash_banks() {
    for (unsigned b = 0; b < 64; ++b) {
        std::uint64_t h = 0xCBF29CE484222325ull;
        for (unsigned i = 0; i < 16; ++i)
            h = (h ^ palette_[b * 16 + i]) * 0x100000001B3ull;
        bank_hash_[b] = h;
    }
}

void TextureCache::begin_frame() {
    ++frame_;
    constexpr std::size_t kHigh = 1536, kLow = 1024;
    if (entries_.size() < kHigh)
        return;

    std::vector<std::pair<std::uint64_t, std::uint64_t>> age;
    age.reserve(entries_.size());
    for (const auto& [key, e] : entries_)
        age.emplace_back(e.last_frame, key);
    std::sort(age.begin(), age.end());

    for (std::size_t i = 0; i < age.size() && entries_.size() > kLow; ++i) {
        if (age[i].first + 4 >= frame_)
            break;
        auto it = entries_.find(age[i].second);
        free_entry(it->second);
        entries_.erase(it);
    }
}

void TextureCache::invalidate() {
    for (auto& [key, e] : entries_)
        free_entry(e);
    entries_.clear();
    for (auto& [address, r] : rtt_)
        free_rtt(r);
    rtt_.clear();
}

void TextureCache::free_entry(Entry& e) {
    if (e.tex) {
        glDeleteTextures(1, &e.tex);
        e.tex = 0;
    }
}

void TextureCache::free_rtt(Rtt& r) {
    if (r.fbo) {
        glDeleteFramebuffers(1, &r.fbo);
        r.fbo = 0;
    }
    if (r.tex) {
        glDeleteTextures(1, &r.tex);
        r.tex = 0;
    }
    r.sets.clear();
}


std::size_t TextureCache::invalidate_range(std::uint32_t begin, std::uint32_t end, bool rendered) {
    if (end <= begin)
        return 0;

    if (!rendered) {
        for (auto it = rtt_.begin(); it != rtt_.end();) {
            if (it->first < end && begin < it->first + it->second.bytes) {
                free_rtt(it->second);
                it = rtt_.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (rendered && replacer_.active() &&
        std::find(rendered_ranges_.begin(), rendered_ranges_.end(), std::make_pair(begin, end)) ==
            rendered_ranges_.end() &&
        rendered_ranges_.size() < 64) {
        rendered_ranges_.emplace_back(begin, end);
    }

    std::vector<std::uint64_t> doomed;
    for (const auto& [key, e] : entries_) {
        const std::uint32_t lo = e.info.address;
        const std::uint32_t bytes = e.info.size_bytes();
        if (bytes == 0)
            continue;
        if (lo < end && begin < lo + bytes)
            doomed.push_back(key);
    }

    for (std::uint64_t key : doomed) {
        auto it = entries_.find(key);
        if (it != entries_.end()) {
            free_entry(it->second);
            entries_.erase(it);
        }
    }
    return doomed.size();
}

std::uint32_t TextureCache::sampler_for(std::uint32_t tsp, bool tiled, bool mipmapped) {
    const std::uint32_t key =
        (tsp & 0x7FFFFu) | (tiled ? 0x80000u : 0u) | (mipmapped ? 0x100000u : 0u);
    const auto it = samplers_.find(key);
    if (it != samplers_.end())
        return it->second;

    GLuint sampler = 0;
    glGenSamplers(1, &sampler);
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, filter_for(tsp, false, mipmapped));
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, filter_for(tsp, true, mipmapped));

    const bool clamp_u = tsp_clamp_u(tsp);
    const bool clamp_v = tsp_clamp_v(tsp);
    const bool flip_u = tsp_flip_u(tsp);
    const bool flip_v = tsp_flip_v(tsp);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, wrap_for(clamp_u, flip_u, tiled));
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, wrap_for(clamp_v, flip_v, tiled));

    samplers_[key] = sampler;
    return sampler;
}

bool TextureCache::rendered_into(const TextureInfo& info) const {
    const std::uint32_t lo = info.address, hi = info.address + info.size_bytes();
    for (const auto& [begin, end] : rendered_ranges_) {
        if (lo < end && begin < hi)
            return true;
    }
    return false;
}

bool TextureCache::upload(VkCommandBuffer, Entry& entry, const std::vector<std::uint32_t>& pixels,
                          std::uint32_t width, std::uint32_t height,
                          const std::vector<std::vector<std::uint32_t>>* mips) {
    const std::uint32_t levels = 1u + (mips ? static_cast<std::uint32_t>(mips->size()) : 0u);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, static_cast<GLsizei>(levels), GL_RGBA8, static_cast<GLsizei>(width),
                   static_cast<GLsizei>(height));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(levels - 1));
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    if (mips) {
        for (std::size_t m = 0; m < mips->size(); ++m) {
            const auto& mip = (*mips)[m];
            const std::uint32_t mw = std::max(1u, width >> (m + 1));
            const std::uint32_t mh = std::max(1u, height >> (m + 1));
            glTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(m + 1), 0, 0, static_cast<GLsizei>(mw),
                            static_cast<GLsizei>(mh), GL_RGBA, GL_UNSIGNED_BYTE, mip.data());
        }
    }

    entry.tex = tex;
    return true;
}

void TextureCache::update_in_place(Entry& entry, const std::vector<std::uint32_t>& pixels,
                                   std::uint32_t width, std::uint32_t height,
                                   const std::vector<std::vector<std::uint32_t>>* mips) {
    glBindTexture(GL_TEXTURE_2D, entry.tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    if (mips) {
        for (std::size_t m = 0; m < mips->size(); ++m) {
            const auto& mip = (*mips)[m];
            const std::uint32_t mw = std::max(1u, width >> (m + 1));
            const std::uint32_t mh = std::max(1u, height >> (m + 1));
            glTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(m + 1), 0, 0, static_cast<GLsizei>(mw),
                            static_cast<GLsizei>(mh), GL_RGBA, GL_UNSIGNED_BYTE, mip.data());
        }
    }
}

VkDescriptorSet TextureCache::rtt_set(std::uint32_t tcw, std::uint32_t tsp, bool tiled) {
    TextureInfo info;
    if (!describe_texture(tcw, tsp, 0, info) || info.twiddled || info.vq || info.indexed())
        return 0;
    const auto it = rtt_.find(info.address);
    if (it == rtt_.end())
        return 0;

    const std::uint32_t sampler = sampler_for(tsp, tiled, false);
    return (static_cast<std::uint64_t>(sampler) << 32) | static_cast<std::uint64_t>(it->second.tex);
}

void TextureCache::capture_rtt(VkCommandBuffer, VkImage, std::uint32_t src_w, std::uint32_t src_h,
                               std::uint32_t address, std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 || !ctx_)
        return;

    width = std::min(width, 640u);
    height = std::min(height, 480u);
    const auto next_pot = [](std::uint32_t v) {
        std::uint32_t p = 8;
        while (p < v && p < 1024) p <<= 1;
        return p;
    };
    const std::uint32_t pw = next_pot(width);
    const std::uint32_t ph = next_pot(height);
    const double scale = static_cast<double>(src_h) / 480.0;
    const auto scaled = [&](std::uint32_t v) {
        return std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(v * scale + 0.5));
    };
    const std::uint32_t iw = scaled(pw);
    const std::uint32_t ih = scaled(ph);

    bool fresh = false;
    auto it = rtt_.find(address);
    if (it != rtt_.end() && (it->second.image_w != iw || it->second.image_h != ih)) {
        free_rtt(it->second);
        rtt_.erase(it);
        it = rtt_.end();
    }
    if (it == rtt_.end()) {
        Rtt r{};
        glGenTextures(1, &r.tex);
        glBindTexture(GL_TEXTURE_2D, r.tex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, static_cast<GLsizei>(iw), static_cast<GLsizei>(ih));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &r.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, r.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r.tex, 0);

        r.image_w = iw;
        r.image_h = ih;
        r.bytes = pw * ph * 2;
        it = rtt_.emplace(address, r).first;
        fresh = true;
    }

    Rtt& r = it->second;
    GLint prev_read_fbo = 0, prev_draw_fbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw_fbo);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r.fbo);
    glDisable(GL_SCISSOR_TEST);
    if (fresh) {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    const GLint src_x1 = static_cast<GLint>(std::min(src_w, (width * src_w + 639u) / 640u));
    const GLint src_y1 = static_cast<GLint>(std::min(src_h, (height * src_h + 479u) / 480u));
    const GLint dst_x1 = static_cast<GLint>(std::min(iw, scaled(width)));
    const GLint dst_y1 = static_cast<GLint>(std::min(ih, scaled(height)));

    glBlitFramebuffer(0, 0, src_x1, src_y1, 0, 0, dst_x1, dst_y1, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_read_fbo));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prev_draw_fbo));
}

void TextureCache::drop_rtt() {
    for (auto& [address, r] : rtt_)
        free_rtt(r);
    rtt_.clear();
}

VkDescriptorSet TextureCache::fallback(VkCommandBuffer /*cmd*/) {
    if (fallback_set_)
        return fallback_set_;

    glGenTextures(1, &fallback_tex_);
    glBindTexture(GL_TEXTURE_2D, fallback_tex_);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 1, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const std::uint32_t white = 0xFFFFFFFFu;
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &white);

    glGenSamplers(1, &fallback_sampler_);
    glSamplerParameteri(fallback_sampler_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(fallback_sampler_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(fallback_sampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(fallback_sampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    fallback_set_ = (static_cast<std::uint64_t>(fallback_sampler_) << 32) |
                    static_cast<std::uint64_t>(fallback_tex_);
    return fallback_set_;
}

VkDescriptorSet TextureCache::get(VkCommandBuffer cmd, std::uint32_t tcw, std::uint32_t tsp,
                                  bool tiled) {
    if (const VkDescriptorSet rset = rtt_set(tcw, tsp, tiled))
        return rset;

    const std::uint64_t tex_key = (static_cast<std::uint64_t>(tcw) << 6) | (tsp & 0x3Fu);
    Meta& m = meta_[tex_key];
    if (!m.ok) {
        TextureInfo info;
        m.ok = describe_texture(tcw, tsp, 0, info);
        m.indexed = m.ok && info.indexed();
        if (m.indexed) {
            m.palette_base = info.palette_base;
            m.palette_count = info.format == PixelFormat::Palette4 ? 16u : 256u;
        }
    }

    std::uint64_t ph = 0xCBF29CE484222325ull;
    if (m.indexed) {
        for (std::uint32_t b = m.palette_base / 16;
             b < (m.palette_base + m.palette_count) / 16 && b < 64; ++b)
            ph = (ph ^ bank_hash_[b]) * 0x100000001B3ull;
    }

    auto it = entries_.find(tex_key);
    if (it != entries_.end()) {
        if (it->second.tex == 0)
            return 0;
        if (!m.indexed || it->second.palette_hash == ph) {
            it->second.last_frame = frame_;
            const std::uint32_t sampler = sampler_for(tsp, tiled, it->second.info.mipmapped);
            return (static_cast<std::uint64_t>(sampler) << 32) | static_cast<std::uint64_t>(it->second.tex);
        }

        std::vector<std::uint32_t> pixels;
        if (!decode_texture(it->second.info, vram_, vram_size_, palette_, pixels))
            return 0;
        std::vector<std::vector<std::uint32_t>> mips;
        if (it->second.info.mipmapped) {
            for (std::uint32_t size = it->second.info.width / 2; size >= 8; size /= 2) {
                TextureInfo level = it->second.info;
                level.width = level.height = size;
                std::vector<std::uint32_t> px;
                if (!decode_texture(level, vram_, vram_size_, palette_, px))
                    break;
                mips.push_back(std::move(px));
            }
        }
        update_in_place(it->second, pixels, it->second.info.width, it->second.info.height,
                        mips.empty() ? nullptr : &mips);
        it->second.palette_hash = ph;
        it->second.last_frame = frame_;
        const std::uint32_t sampler = sampler_for(tsp, tiled, it->second.info.mipmapped);
        return (static_cast<std::uint64_t>(sampler) << 32) | static_cast<std::uint64_t>(it->second.tex);
    }

    Entry e{};
    if (!describe_texture(tcw, tsp, 0, e.info)) {
        entries_.emplace(tex_key, std::move(e));
        return 0;
    }
    std::uint64_t hash = 0;
    bool hashed = false;
    const png::Image* replacement = nullptr;
    if (replacer_.active() && !rendered_into(e.info)) {
        hashed = texture_hash(e.info, vram_, vram_size_, palette_, hash);
        if (hashed && replacer_.replacing()) {
            replacement = replacer_.find(e.info, hash);
            const std::uint32_t limit = ctx_->caps().max_texture_size;
            if (replacement && limit &&
                (replacement->width > limit || replacement->height > limit)) {
                std::fprintf(stderr, "texture pack: %ux%u is larger than this GPU allows (%u)\n",
                             replacement->width, replacement->height, limit);
                replacement = nullptr;
            }
        }
    }
    std::vector<std::uint32_t> pixels;
    if (!replacement || replacer_.dumping()) {
        if (!decode_texture(e.info, vram_, vram_size_, palette_, pixels)) {
            entries_.emplace(tex_key, std::move(e));
            return 0;
        }
        if (hashed)
            replacer_.dump(e.info, hash, pixels);
    }
    std::vector<std::vector<std::uint32_t>> mips;
    if (!replacement && e.info.mipmapped) {
        for (std::uint32_t size = e.info.width / 2; size >= 8; size /= 2) {
            TextureInfo level = e.info;
            level.width = level.height = size;
            std::vector<std::uint32_t> px;
            if (!decode_texture(level, vram_, vram_size_, palette_, px))
                break;
            mips.push_back(std::move(px));
        }
    }
    const bool uploaded =
        replacement
            ? upload(cmd, e, replacement->pixels, replacement->width, replacement->height)
            : upload(cmd, e, pixels, e.info.width, e.info.height, mips.empty() ? nullptr : &mips);
    if (!uploaded) {
        entries_.emplace(tex_key, std::move(e));
        return 0;
    }

    e.palette_hash = ph;
    e.last_frame = frame_;
    const std::uint32_t sampler = sampler_for(tsp, tiled, e.info.mipmapped);
    const VkDescriptorSet set = (static_cast<std::uint64_t>(sampler) << 32) |
                                static_cast<std::uint64_t>(e.tex);
    entries_.emplace(tex_key, std::move(e));
    return set;
}

}  // namespace dream::render::vk
