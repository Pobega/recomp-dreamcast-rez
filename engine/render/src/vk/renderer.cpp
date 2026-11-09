#include "dream/render/vk/renderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include <GLES3/gl3.h>

#include "dream/render/tsp.h"

namespace dream::render::vk {

namespace {

constexpr std::size_t kVertexBytes = sizeof(Vertex) + sizeof(float);

struct LineInstance {
    float a[4];
    float b[4];
    std::uint32_t base;
    std::uint32_t offset;
    float half_width;
};

constexpr const char* kGeomVertSource = R"(#version 300 es
precision highp float;

uniform vec2 u_scale;
uniform vec2 u_offset;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_base;
layout(location = 3) in vec4 in_offset;
layout(location = 4) in float in_fog;

out mediump vec4 v_base;
out mediump vec4 v_offset;
out vec2 v_uv;
out mediump float v_fog;

vec4 clip_position(vec3 p) {
    float inv_w = clamp(p.z, 1e-20, 131072.0);
    float w = 1.0 / inv_w;
    return vec4((p.xy * u_scale + u_offset) * w, (2.0 / 131072.0) - w, w);
}

void main() {
    v_base = in_base.zyxw;
    v_offset = in_offset.zyxw;
    v_uv = in_uv;
    v_fog = in_fog;
    gl_Position = clip_position(in_pos);
}
)";

constexpr const char* kLinesVertSource = R"(#version 300 es
precision highp float;

uniform vec2 u_scale;
uniform vec2 u_offset;

layout(location = 0) in vec4 in_a;
layout(location = 1) in vec4 in_b;
layout(location = 2) in vec4 in_base;
layout(location = 3) in vec4 in_offset;
layout(location = 4) in float in_half;

out mediump vec4 v_base;
out mediump vec4 v_offset;
out vec2 v_uv;
out mediump float v_fog;

vec4 clip_position(vec3 p) {
    float inv_w = clamp(p.z, 1e-20, 131072.0);
    float w = 1.0 / inv_w;
    return vec4((p.xy * u_scale + u_offset) * w, (2.0 / 131072.0) - w, w);
}

void main() {
    float f6 = in_b.x - in_a.x;
    float f5 = in_b.y - in_a.y;
    float s = inversesqrt(f6 * f6 + f5 * f5) * in_half;
    f5 *= s;
    f6 *= s;
    if (0.0 > f5) {
        f5 = -f5;
        f6 = -f6;
    }
    vec3 pos;
    float fog;
    switch (gl_VertexID) {
        case 0:
            pos = vec3(in_b.x - f5, in_b.y + f6, in_b.z);
            fog = in_b.w;
            break;
        case 1:
            pos = vec3(in_b.x + f5, in_b.y - f6, in_b.z);
            fog = in_b.w;
            break;
        case 2:
            pos = vec3(in_a.x - f5, in_a.y + f6, in_a.z);
            fog = in_a.w;
            break;
        default:
            pos = vec3(in_a.x + f5, in_a.y - f6, in_a.z);
            fog = in_a.w;
            break;
    }
    v_base = in_base.zyxw;
    v_offset = in_offset.zyxw;
    v_uv = vec2(0.0);
    v_fog = fog;
    gl_Position = clip_position(pos);
}
)";

constexpr const char* kFragNoDiscardSource = R"(#version 300 es
#define NO_DISCARD 1
precision highp float;
precision highp int;

uniform int u_mode;
uniform float u_alpha_ref;
uniform mediump vec4 u_fog_vert;
uniform mediump vec4 u_fog_ram;
uniform vec4 u_clip;
uniform mediump sampler2D s_tex;

in mediump vec4 v_base;
in mediump vec4 v_offset;
in vec2 v_uv;
in mediump float v_fog;

layout(location = 0) out mediump vec4 o_colour;

void main() {
    int mode = u_mode;
    mediump vec4 base = v_base;
    if ((mode & 64) == 0)
        base.a = 1.0;
    mediump vec4 colour = base;
    mediump vec3 fog_ram = u_fog_ram.rgb;
    int fog = (mode >> 7) & 3;
    if (fog == 3)
        colour = vec4(fog_ram, v_fog);

    if ((mode & 4) != 0) {
        mediump vec4 texel = texture(s_tex, v_uv);
        if ((mode & 8) != 0)
            texel.a = 1.0;
        int shading = mode & 3;
        if (shading == 0) {
            colour = texel;
        } else if (shading == 1) {
            colour = vec4(texel.rgb * base.rgb, texel.a);
        } else if (shading == 2) {
            colour = vec4(mix(base.rgb, texel.rgb, texel.a), base.a);
        } else {
            colour = texel * base;
        }
    }
    if ((mode & 16) != 0)
        colour.rgb += v_offset.rgb;

    if (fog == 0) {
        colour.rgb = mix(colour.rgb, fog_ram, clamp(v_fog, 0.0, 1.0));
    } else if (fog == 1 && (mode & 16) != 0) {
        mediump vec3 fog_vert = u_fog_vert.rgb;
        colour.rgb = mix(colour.rgb, fog_vert, clamp(v_offset.a, 0.0, 1.0));
    }

    o_colour = clamp(colour, 0.0, 1.0);
}
)";

constexpr const char* kFragDiscardSource = R"(#version 300 es
precision highp float;
precision highp int;

uniform int u_mode;
uniform float u_alpha_ref;
uniform mediump vec4 u_fog_vert;
uniform mediump vec4 u_fog_ram;
uniform vec4 u_clip;
uniform mediump sampler2D s_tex;

in mediump vec4 v_base;
in mediump vec4 v_offset;
in vec2 v_uv;
in mediump float v_fog;

layout(location = 0) out mediump vec4 o_colour;

void main() {
    int mode = u_mode;
    if ((mode & 512) != 0 && gl_FragCoord.x >= u_clip.x && gl_FragCoord.x < u_clip.z &&
        gl_FragCoord.y >= u_clip.y && gl_FragCoord.y < u_clip.w)
        discard;

    mediump vec4 base = v_base;
    if ((mode & 64) == 0)
        base.a = 1.0;
    mediump vec4 colour = base;
    mediump vec3 fog_ram = u_fog_ram.rgb;
    int fog = (mode >> 7) & 3;
    if (fog == 3)
        colour = vec4(fog_ram, v_fog);

    if ((mode & 4) != 0) {
        mediump vec4 texel = texture(s_tex, v_uv);
        if ((mode & 8) != 0)
            texel.a = 1.0;
        int shading = mode & 3;
        if (shading == 0) {
            colour = texel;
        } else if (shading == 1) {
            colour = vec4(texel.rgb * base.rgb, texel.a);
        } else if (shading == 2) {
            colour = vec4(mix(base.rgb, texel.rgb, texel.a), base.a);
        } else {
            colour = texel * base;
        }
    }
    if ((mode & 16) != 0)
        colour.rgb += v_offset.rgb;

    if (fog == 0) {
        colour.rgb = mix(colour.rgb, fog_ram, clamp(v_fog, 0.0, 1.0));
    } else if (fog == 1 && (mode & 16) != 0) {
        mediump vec3 fog_vert = u_fog_vert.rgb;
        colour.rgb = mix(colour.rgb, fog_vert, clamp(v_offset.a, 0.0, 1.0));
    }

    if ((mode & 32) != 0) {
        highp float a = round(float(colour.a) * 255.0) / 255.0;
        if (a < u_alpha_ref)
            discard;
        colour.a = 1.0;
    }

    o_colour = clamp(colour, 0.0, 1.0);
}
)";

constexpr const char* kModvolVertSource = R"(#version 300 es
precision highp float;

uniform vec2 u_scale;
uniform vec2 u_offset;

layout(location = 0) in vec3 in_pos;

vec4 clip_position(vec3 p) {
    float inv_w = clamp(p.z, 1e-20, 131072.0);
    float w = 1.0 / inv_w;
    return vec4((p.xy * u_scale + u_offset) * w, (2.0 / 131072.0) - w, w);
}

void main() {
    gl_Position = clip_position(in_pos);
}
)";

constexpr const char* kModvolFragSource = R"(#version 300 es
precision highp float;

uniform float u_alpha;
layout(location = 0) out vec4 o_colour;

void main() {
    o_colour = vec4(0.0, 0.0, 0.0, u_alpha);
}
)";

GLuint compile_shader(GLenum type, const char* source, std::string& err) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &source, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetShaderInfoLog(sh, sizeof buf, nullptr, buf);
        err = buf;
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint link_program(GLuint vs, GLuint fs, std::string& err) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetProgramInfoLog(prog, sizeof buf, nullptr, buf);
        err = buf;
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

void query_uniforms(GLuint prog, ShaderUniforms& u) {
    u.u_scale = glGetUniformLocation(prog, "u_scale");
    u.u_offset = glGetUniformLocation(prog, "u_offset");
    u.u_mode = glGetUniformLocation(prog, "u_mode");
    u.u_alpha_ref = glGetUniformLocation(prog, "u_alpha_ref");
    u.u_fog_vert = glGetUniformLocation(prog, "u_fog_vert");
    u.u_fog_ram = glGetUniformLocation(prog, "u_fog_ram");
    u.u_clip = glGetUniformLocation(prog, "u_clip");
    u.u_tex = glGetUniformLocation(prog, "s_tex");
}

constexpr std::uint32_t isp_depth_mode(std::uint32_t isp) {
    return (isp >> 29) & 7u;
}
constexpr std::uint32_t isp_cull_mode(std::uint32_t isp) {
    return (isp >> 27) & 3u;
}
constexpr std::uint32_t isp_z_write_disable(std::uint32_t isp) {
    return (isp >> 26) & 1u;
}
constexpr std::uint32_t pcw_texture(std::uint32_t p) {
    return (p >> 3) & 1u;
}
constexpr std::uint32_t pcw_offset(std::uint32_t p) {
    return (p >> 2) & 1u;
}
constexpr std::uint32_t pcw_shadow(std::uint32_t p) {
    return (p >> 7) & 1u;
}

GLenum depth_compare(std::uint32_t mode) {
    switch (mode) {
        case 0: return GL_NEVER;
        case 1: return GL_LESS;
        case 2: return GL_EQUAL;
        case 3: return GL_LEQUAL;
        case 4: return GL_GREATER;
        case 5: return GL_NOTEQUAL;
        case 6: return GL_GEQUAL;
        default: return GL_ALWAYS;
    }
}

GLenum src_factor(std::uint32_t instr) {
    switch (instr) {
        case 0: return GL_ZERO;
        case 1: return GL_ONE;
        case 2: return GL_DST_COLOR;
        case 3: return GL_ONE_MINUS_DST_COLOR;
        case 4: return GL_SRC_ALPHA;
        case 5: return GL_ONE_MINUS_SRC_ALPHA;
        case 6: return GL_DST_ALPHA;
        default: return GL_ONE_MINUS_DST_ALPHA;
    }
}

GLenum dst_factor(std::uint32_t instr) {
    switch (instr) {
        case 0: return GL_ZERO;
        case 1: return GL_ONE;
        case 2: return GL_SRC_COLOR;
        case 3: return GL_ONE_MINUS_SRC_COLOR;
        case 4: return GL_SRC_ALPHA;
        case 5: return GL_ONE_MINUS_SRC_ALPHA;
        case 6: return GL_DST_ALPHA;
        default: return GL_ONE_MINUS_DST_ALPHA;
    }
}

void apply_pvr_cull_mode(std::uint8_t cull_mode) {
    if (cull_mode < 2) {
        glDisable(GL_CULL_FACE);
        return;
    }
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CW);
    glCullFace(cull_mode == 2 ? GL_FRONT : GL_BACK);
}

constexpr std::uint64_t texture_key(const Polygon& p) {
    return (static_cast<std::uint64_t>(p.tcw) << 32) | (p.tsp & 0x3Fu) |
           (static_cast<std::uint64_t>((p.tsp >> 8) & 0x7FFu) << 7);
}

inline std::uint32_t depth_key(float f) noexcept {
    if (std::isnan(f))
        return 0xFFFFFFFFu;
    std::uint32_t b = 0;
    std::memcpy(&b, &f, 4);
    if (b == 0x80000000u)
        b = 0;
    return (b & 0x80000000u) ? ~b : (b | 0x80000000u);
}

void sort_back_to_front(std::vector<std::uint64_t>& pairs) {
    const std::size_t n = pairs.size();
    thread_local std::vector<std::uint64_t> spare;
    if (n < 256) {
        std::sort(pairs.begin(), pairs.end());
    } else {
        spare.resize(n);
        for (unsigned shift = 32; shift < 64; shift += 8) {
            std::size_t count[257] = {};
            for (std::size_t i = 0; i < n; ++i) ++count[((pairs[i] >> shift) & 0xFFu) + 1];
            if (count[1 + ((pairs[0] >> shift) & 0xFFu)] == n)
                continue;
            for (unsigned b = 1; b < 257; ++b) count[b] += count[b - 1];
            for (std::size_t i = 0; i < n; ++i) spare[count[(pairs[i] >> shift) & 0xFFu]++] = pairs[i];
            pairs.swap(spare);
        }
    }
}

struct PushConstants {
    float scale[2] = {};
    float offset[2] = {};
    std::int32_t mode = 0;
    float alpha_ref = 0.0f;
    float fog_vert[4] = {};
    float fog_ram[4] = {};
    float clip[4] = {};
};

struct DrawState {
    VkDescriptorSet set = 0;
    PushConstants push{};
    VkRect2D scissor{};
    bool ok = false;
    bool additive = false;
    std::uint32_t key = 0;
};

thread_local std::vector<DrawState> t_states;
constexpr std::uint32_t kNoState = ~0u;
constexpr std::uint32_t kLineBatch = ~0u;

struct Item {
    float depth;
    std::uint32_t first, count;
    std::uint32_t state;
};

struct Box {
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    void grow(float x, float y) noexcept {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
    }
    void grow(const Vertex& v) noexcept { grow(v.x, v.y); }
    bool finite() const noexcept {
        return std::isfinite(x0) && std::isfinite(y0) && std::isfinite(x1) && std::isfinite(y1);
    }
    bool overlaps(const Box& o) const noexcept {
        return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1;
    }
};

struct Translucent {
    Item item;
    Box bb;
};

constexpr std::size_t kMaxRegroupLookahead = 512;

void regroup(const std::vector<Translucent>& in, std::vector<Item>& out) {
    const std::size_t n = in.size();
    if (n == 0)
        return;
    out.reserve(out.size() + n);
    thread_local std::vector<std::uint8_t> emitted;
    emitted.assign(n, 0);

    for (std::size_t i = 0; i < n; ++i) {
        if (emitted[i])
            continue;
        emitted[i] = 1;
        const auto& cur = in[i];
        out.push_back(cur.item);
        const std::uint32_t state = cur.item.state;
        if (state == kNoState || !cur.bb.finite())
            continue;

        const bool cur_additive = state < t_states.size() && t_states[state].additive;
        Box blocker_box{};
        bool has_blocker = false;

        const std::size_t limit = std::min(n, i + 1 + kMaxRegroupLookahead);
        for (std::size_t j = i + 1; j < limit; ++j) {
            if (emitted[j])
                continue;
            const auto& cand = in[j];
            if (cand.item.state == state &&
                (!has_blocker || !blocker_box.overlaps(cand.bb))) {
                emitted[j] = 1;
                out.push_back(cand.item);
            } else {
                const bool cand_additive =
                    cand.item.state < t_states.size() && t_states[cand.item.state].additive;
                if (!(cur_additive && cand_additive) && cand.bb.finite()) {
                    blocker_box.grow(cand.bb.x0, cand.bb.y0);
                    blocker_box.grow(cand.bb.x1, cand.bb.y1);
                    has_blocker = true;
                }
            }
        }
    }
}

bool same_rect(const VkRect2D& a, const VkRect2D& b) noexcept {
    return a.offset.x == b.offset.x && a.offset.y == b.offset.y &&
           a.extent.width == b.extent.width && a.extent.height == b.extent.height;
}

constexpr std::uint32_t kOddWindingBit = 0x80000000u;
constexpr std::uint32_t kGlPipelineStateMask = 0x7FFFu;

}  // namespace

Renderer::PipelineKey Renderer::key_from_bits(std::uint32_t bits) {
    PipelineKey k{};
    k.depth_mode = bits & 7u;
    k.depth_write = (bits >> 3) & 1u;
    k.cull_mode = (bits >> 4) & 3u;
    k.src_blend = (bits >> 6) & 7u;
    k.dst_blend = (bits >> 9) & 7u;
    k.blend = (bits >> 12) & 1u;
    k.shadow = (bits >> 13) & 1u;
    k.discard = (bits >> 14) & 1u;
    k.shader = (bits >> 16) & 0x3FFu;
    k.padding = 0;
    return k;
}

bool Renderer::create(Context& ctx, VkRenderPass) {
    ctx_ = &ctx;

    GLuint vs_geom = compile_shader(GL_VERTEX_SHADER, kGeomVertSource, error_);
    GLuint vs_lines = compile_shader(GL_VERTEX_SHADER, kLinesVertSource, error_);
    GLuint fs_nd = compile_shader(GL_FRAGMENT_SHADER, kFragNoDiscardSource, error_);
    GLuint fs_d = compile_shader(GL_FRAGMENT_SHADER, kFragDiscardSource, error_);
    GLuint vs_mv = compile_shader(GL_VERTEX_SHADER, kModvolVertSource, error_);
    GLuint fs_mv = compile_shader(GL_FRAGMENT_SHADER, kModvolFragSource, error_);

    if (!vs_geom || !vs_lines || !fs_nd || !fs_d || !vs_mv || !fs_mv)
        return false;

    prog_geom_nd_ = link_program(vs_geom, fs_nd, error_);
    prog_geom_d_ = link_program(vs_geom, fs_d, error_);
    prog_lines_nd_ = link_program(vs_lines, fs_nd, error_);
    prog_lines_d_ = link_program(vs_lines, fs_d, error_);
    prog_modvol_ = link_program(vs_mv, fs_mv, error_);

    glDeleteShader(vs_geom);
    glDeleteShader(vs_lines);
    glDeleteShader(fs_nd);
    glDeleteShader(fs_d);
    glDeleteShader(vs_mv);
    glDeleteShader(fs_mv);

    if (!prog_geom_nd_ || !prog_geom_d_ || !prog_lines_nd_ || !prog_lines_d_ || !prog_modvol_)
        return false;

    query_uniforms(prog_geom_nd_, u_geom_nd_);
    query_uniforms(prog_geom_d_, u_geom_d_);
    query_uniforms(prog_lines_nd_, u_lines_nd_);
    query_uniforms(prog_lines_d_, u_lines_d_);

    u_mv_scale_ = glGetUniformLocation(prog_modvol_, "u_scale");
    u_mv_offset_ = glGetUniformLocation(prog_modvol_, "u_offset");
    u_mv_alpha_ = glGetUniformLocation(prog_modvol_, "u_alpha");

    glGenVertexArrays(1, &geom_vao_);
    glGenVertexArrays(1, &lines_vao_);
    glGenVertexArrays(1, &modvol_vao_);

    if (!textures_.create(ctx)) {
        error_ = textures_.error();
        return false;
    }

    return true;
}

void Renderer::destroy() {
    if (geom_vao_) {
        glDeleteVertexArrays(1, &geom_vao_);
        geom_vao_ = 0;
    }
    if (lines_vao_) {
        glDeleteVertexArrays(1, &lines_vao_);
        lines_vao_ = 0;
    }
    if (modvol_vao_) {
        glDeleteVertexArrays(1, &modvol_vao_);
        modvol_vao_ = 0;
    }
    if (prog_geom_nd_) {
        glDeleteProgram(prog_geom_nd_);
        prog_geom_nd_ = 0;
    }
    if (prog_geom_d_) {
        glDeleteProgram(prog_geom_d_);
        prog_geom_d_ = 0;
    }
    if (prog_lines_nd_) {
        glDeleteProgram(prog_lines_nd_);
        prog_lines_nd_ = 0;
    }
    if (prog_lines_d_) {
        glDeleteProgram(prog_lines_d_);
        prog_lines_d_ = 0;
    }
    if (prog_modvol_) {
        glDeleteProgram(prog_modvol_);
        prog_modvol_ = 0;
    }
    for (auto& b : vertex_ring_) b.destroy();
    for (auto& b : index_ring_) b.destroy();
    for (auto& b : line_ring_) b.destroy();
    for (auto& b : modvol_ring_) b.destroy();
    textures_.destroy();
    frame_sets_.clear();
}

Renderer::~Renderer() {
    destroy();
}

void Renderer::set_memory(const std::uint8_t* vram, std::size_t vram_size,
                          const std::uint32_t* palette_ram, PaletteFormat palette_format) {
    textures_.set_memory(vram, vram_size, palette_ram, palette_format);
    frame_sets_.clear();
}

void Renderer::use_pipeline_cache(const std::string& path) {
    pipeline_cache_path_ = path;
}

std::uint32_t Renderer::prewarm(const std::vector<std::uint32_t>& keys) {
    prewarmed = static_cast<std::uint32_t>(keys.size());
    return prewarmed;
}

void Renderer::prepare(VkCommandBuffer upload, const Frame& frame) {
    frame_sets_.clear();
    fallback_set_ = textures_.fallback(upload);
    struct Use {
        std::uint32_t tcw = 0, tsp = 0;
        bool tiled = false;
    };
    thread_local std::unordered_map<std::uint64_t, Use> used;
    used.clear();
    for (const auto& list : frame.lists) {
        for (const Polygon& p : list) {
            if (!pcw_texture(p.pcw))
                continue;
            Use& u = used[texture_key(p)];
            u.tcw = p.tcw;
            u.tsp = p.tsp;
            u.tiled = u.tiled || polygon_tiles(frame, p);
        }
    }
    for (const auto& [key, u] : used)
        frame_sets_[key] = textures_.get(upload, u.tcw, u.tsp, u.tiled);
}

void Renderer::draw_modifier_volumes(const Frame& frame, const FrameGeometry& geometry) {
    if (frame.modifiers.empty())
        return;
    modvol_tris_.clear();
    for (const ModifierTriangle& t : frame.modifiers)
        if (!t.translucent)
            modvol_tris_.push_back(t);
    if (modvol_tris_.empty())
        return;

    const std::vector<ModifierTriangle>& mods = modvol_tris_;
    const std::size_t tris = mods.size();
    staging_.resize((tris + 2) * 9);
    float* out = staging_.data();
    for (const ModifierTriangle& t : mods) {
        for (unsigned i = 0; i < 3; ++i) {
            *out++ = t.x[i];
            *out++ = t.y[i];
            *out++ = t.z[i];
        }
    }
    const float l = geometry.left, t = geometry.top;
    const float r = geometry.left + geometry.width, b = geometry.top + geometry.height;
    const float quad[18] = {l, t, 1.0f, r, t, 1.0f, l, b, 1.0f, r, t, 1.0f, r, b, 1.0f, l, b, 1.0f};
    std::copy(std::begin(quad), std::end(quad), out);

    HostBuffer& buf = modvol_ring_[modvol_next_];
    modvol_next_ = (modvol_next_ + 1) % kModvolRing;
    const std::size_t bytes = staging_.size() * sizeof(float);
    buf.ensure(*ctx_, bytes);
    buf.write(staging_.data(), bytes);
    buf.upload_to_target(GL_ARRAY_BUFFER, bytes);

    glBindVertexArray(modvol_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, buf.gl_buffer());
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (const void*)0);
    glEnableVertexAttribArray(0);

    glUseProgram(prog_modvol_);
    const float sc[2] = {2.0f / geometry.width, 2.0f / geometry.height};
    const float off[2] = {-1.0f - 2.0f * geometry.left / geometry.width,
                          -1.0f - 2.0f * geometry.top / geometry.height};
    if (u_mv_scale_ >= 0) glUniform2fv(u_mv_scale_, 1, sc);
    if (u_mv_offset_ >= 0) glUniform2fv(u_mv_offset_, 1, off);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);

    std::size_t base = SIZE_MAX;
    for (std::size_t first = 0; first < tris;) {
        std::size_t end = first + 1;
        while (end < tris && mods[end].header == mods[first].header)
            ++end;
        const std::uint32_t isp = mods[first].isp;
        const std::uint32_t mv_mode = isp >> 29;
        const bool volume_last = mods[first].volume_last;
        const std::uint32_t cull = (isp >> 27) & 3u;
        if (base == SIZE_MAX)
            base = first;

        apply_pvr_cull_mode(static_cast<std::uint8_t>(cull));

        if (!volume_last && mv_mode > 0) {
            glStencilFunc(GL_ALWAYS, 1, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        } else {
            glStencilFunc(GL_ALWAYS, 0, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
        }
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(first * 3), static_cast<GLsizei>((end - first) * 3));

        if (mv_mode == 1 || mv_mode == 2) {
            if (mv_mode == 1) {
                glStencilFunc(GL_ALWAYS, 0, 0xFF);
                glStencilOp(GL_KEEP, GL_KEEP, GL_INCR_WRAP);
            } else {
                glStencilFunc(GL_ALWAYS, 0, 0xFF);
                glStencilOp(GL_KEEP, GL_KEEP, GL_DECR_WRAP);
            }
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(base * 3), static_cast<GLsizei>((end - base) * 3));
            base = SIZE_MAX;
        }
        first = end;
    }

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDepthFunc(GL_ALWAYS);
    glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    if (u_mv_alpha_ >= 0) glUniform1f(u_mv_alpha_, 1.0f - geometry.shadow_scale);
    glDrawArrays(GL_TRIANGLES, static_cast<GLint>(tris * 3), 6);

    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
}

void Renderer::draw(VkCommandBuffer, const Frame& frame, const FrameGeometry& geometry,
                    VkExtent2D target) {
    if (frame.vertices.empty() && frame.line_edges.empty())
        return;

    bool table_fog = false;
    for (const auto& list : frame.lists) {
        for (const Polygon& p : list) {
            const std::uint32_t f = tsp_fog_control(p.tsp);
            if (f == 0 || f == 3)
                table_fog = true;
        }
    }

    const std::size_t vcount = frame.vertices.size();
    const std::size_t max_verts = vcount + frame.line_edges.size() * 4;
    const unsigned ring_slot = ring_next_;
    HostBuffer& vb = vertex_ring_[ring_slot];
    ring_next_ = (ring_next_ + 1) % kVertexRing;

    std::uint8_t* vb_base = nullptr;
    if (max_verts != 0) {
        vb.ensure(*ctx_, max_verts * kVertexBytes);
        vb_base = static_cast<std::uint8_t*>(vb.mapped());
        std::uint8_t* out = vb_base;
        for (const Vertex& v : frame.vertices) {
            std::memcpy(out, &v, sizeof v);
            const float fog = table_fog ? fog_table_value(geometry.fog, v.z) : 0.0f;
            std::memcpy(out + sizeof v, &fog, sizeof fog);
            out += kVertexBytes;
        }
    }
    std::uint32_t line_vert_at = static_cast<std::uint32_t>(vcount);

    PushConstants push{};
    push.scale[0] = 2.0f / geometry.width;
    push.scale[1] = 2.0f / geometry.height;
    push.offset[0] = -1.0f - 2.0f * geometry.left / geometry.width;
    push.offset[1] = -1.0f - 2.0f * geometry.top / geometry.height;
    for (int c = 0; c < 4; ++c) {
        push.fog_vert[c] = static_cast<float>((geometry.fog.vertex_colour >> (8 * c)) & 0xFFu) / 255.0f;
        push.fog_ram[c] = static_cast<float>((geometry.fog.table_colour >> (8 * c)) & 0xFFu) / 255.0f;
    }

    const float sx = static_cast<float>(target.width) / geometry.width;
    const float sy = static_cast<float>(target.height) / geometry.height;
    const VkExtent2D limit{area.width ? std::min(area.width, target.width) : target.width,
                           area.height ? std::min(area.height, target.height) : target.height};
    const VkRect2D full_scissor{{0, 0}, limit};

    auto clip_rect = [&](const Polygon& p) {
        const float x0 = std::max(0.0f, (static_cast<float>(p.clip_x0) - geometry.left) * sx);
        const float y0 = std::max(0.0f, (static_cast<float>(p.clip_y0) - geometry.top) * sy);
        const float x1 = std::min(static_cast<float>(limit.width), (static_cast<float>(p.clip_x1) - geometry.left) * sx);
        const float y1 = std::min(static_cast<float>(limit.height), (static_cast<float>(p.clip_y1) - geometry.top) * sy);
        return VkRect2D{
            {static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0)},
            {static_cast<std::uint32_t>(std::max(0.0f, x1 - x0)),
             static_cast<std::uint32_t>(std::max(0.0f, y1 - y0))},
        };
    };

    const auto state_of = [&](const Polygon& p, bool blended, bool alpha_test, DrawState& st) {
        PipelineKey key{};
        key.depth_mode = isp_depth_mode(p.isp) & 7u;
        if (alpha_test || (blended && geometry.autosort))
            key.depth_mode = 6u;
        key.depth_write = alpha_test ? 1u : (blended || isp_z_write_disable(p.isp)) ? 0u : 1u;
        key.cull_mode = isp_cull_mode(p.isp) & 3u;
        key.blend = blended ? 1u : 0u;
        key.src_blend = tsp_src_instr(p.tsp) & 7u;
        key.dst_blend = tsp_dst_instr(p.tsp) & 7u;
        st.additive = blended && key.dst_blend == 1u &&
                      (key.src_blend == 0u || key.src_blend == 1u || key.src_blend == 4u || key.src_blend == 5u);
        key.shadow = (!blended) ? (pcw_shadow(p.pcw) & 1u) : 0u;
        const std::uint32_t mode = p.tile_clip & 3u;
        key.discard = (alpha_test || mode == 3) ? 1u : 0u;
        key.shader = 0;
        key.padding = 0;

        st.set = 0;
        if (pcw_texture(p.pcw)) {
            const auto it = frame_sets_.find(texture_key(p));
            if (it != frame_sets_.end())
                st.set = it->second;
        }
        st.push = push;
        st.push.mode = 0;
        if (st.set) {
            st.push.mode = static_cast<std::int32_t>(tsp_shading_instruction(p.tsp)) | 4;
            if (tsp_ignore_texture_alpha(p.tsp))
                st.push.mode |= 8;
        }
        if (pcw_offset(p.pcw) && pcw_texture(p.pcw))
            st.push.mode |= 16;
        if (alpha_test)
            st.push.mode |= 32;
        if (tsp_use_alpha(p.tsp))
            st.push.mode |= 64;
        st.scissor = (mode == 2) ? clip_rect(p) : full_scissor;
        if (mode == 3) {
            const VkRect2D r = clip_rect(p);
            st.push.mode |= 512;
            st.push.clip[0] = static_cast<float>(r.offset.x);
            st.push.clip[1] = static_cast<float>(r.offset.y);
            st.push.clip[2] = static_cast<float>(r.offset.x + r.extent.width);
            st.push.clip[3] = static_cast<float>(r.offset.y + r.extent.height);
        } else {
            st.push.clip[0] = st.push.clip[1] = st.push.clip[2] = st.push.clip[3] = 0.0f;
        }
        st.push.mode |= static_cast<std::int32_t>((tsp_fog_control(p.tsp) & 3u) << 7);
        st.push.alpha_ref = geometry.alpha_ref;
        if (!st.set)
            st.set = fallback_set_;

        st.key = key.bits();
        st.ok = true;
        return true;
    };

    const auto same_state = [](const DrawState& a, const DrawState& b) {
        return a.key == b.key && a.set == b.set && same_rect(a.scissor, b.scissor) &&
               a.push.mode == b.push.mode && std::memcmp(a.push.clip, b.push.clip, sizeof a.push.clip) == 0;
    };

    t_states.clear();
    const auto intern = [&](const DrawState& cur) -> std::uint32_t {
        for (std::uint32_t i = 0; i < t_states.size(); ++i) {
            if (same_state(t_states[i], cur))
                return i;
        }
        const std::uint32_t id = static_cast<std::uint32_t>(t_states.size());
        t_states.push_back(cur);
        return id;
    };

    thread_local std::vector<Item> items;
    items.clear();
    thread_local std::vector<Translucent> translucent;
    thread_local std::vector<std::uint64_t> pairs;
    translucent.clear();
    pairs.clear();

    const bool per_triangle = geometry.autosort;
    std::size_t indices = 0;

    const auto add_translucent = [&](const Item& item, const Box& bb) {
        const std::uint32_t dkey = depth_key(item.depth);
        pairs.push_back((static_cast<std::uint64_t>(dkey) << 32) | translucent.size());
        translucent.push_back({item, bb});
    };

    const auto box_off_region = [&](const Box& bb) {
        return bb.x1 < geometry.left || bb.x0 > geometry.left + geometry.width ||
               bb.y1 < geometry.top || bb.y0 > geometry.top + geometry.height;
    };

    const auto same_header = [](const Polygon& a, const Polygon& b) {
        return a.pcw == b.pcw && a.isp == b.isp && a.tsp == b.tsp && a.tcw == b.tcw && a.tile_clip == b.tile_clip &&
               a.clip_x0 == b.clip_x0 && a.clip_y0 == b.clip_y0 && a.clip_x1 == b.clip_x1 && a.clip_y1 == b.clip_y1;
    };

    const auto add_list = [&](ListType list, bool blended, bool alpha_test) {
        const auto count = [&](std::uint32_t id, std::uint32_t verts) {
            if (id != kNoState && verts >= 3)
                indices += std::size_t{verts - 2} * 3;
        };
        const Polygon* last = nullptr;
        std::uint32_t even = kNoState;
        for (const Polygon& p : frame.lists[static_cast<unsigned>(list)]) {
            if (!last || !same_header(*last, p)) {
                DrawState st;
                even = state_of(p, blended, alpha_test, st) ? intern(st) : kNoState;
                last = &p;
            }
            if (p.line_batch) {
                const LineBatch& lb = frame.lines[p.first];
                float sum = 0.0f;
                Box bb;
                for (std::uint32_t e = lb.first; e < lb.first + lb.count; ++e) {
                    const LineEdge& le = frame.line_edges[e];
                    sum += le.za + le.zb;
                    bb.grow(le.xa, le.ya);
                    bb.grow(le.xb, le.yb);
                }
                const float hw = std::abs(lb.half_width);
                bb.x0 -= hw;
                bb.y0 -= hw;
                bb.x1 += hw;
                bb.y1 += hw;
                if (box_off_region(bb))
                    continue;
                if (even != kNoState)
                    indices += std::size_t{lb.count} * 6;
                const Item item{lb.count ? sum / static_cast<float>(2 * lb.count) : 0.0f, p.first, kLineBatch, even};
                if (blended)
                    add_translucent(item, bb);
                else
                    items.push_back(item);
                continue;
            }
            const bool is_additive = even < t_states.size() && t_states[even].additive;
            if (blended && per_triangle && !is_additive && p.count > 3) {
                for (std::uint32_t i = 0; i + 2 < p.count; ++i) {
                    const Vertex& a = frame.vertices[p.first + i];
                    const Vertex& b = frame.vertices[p.first + i + 1];
                    const Vertex& c = frame.vertices[p.first + i + 2];
                    if ((a.x == b.x && a.y == b.y) || (b.x == c.x && b.y == c.y) ||
                        (a.x == c.x && a.y == c.y))
                        continue;
                    count(even, 3u);
                    Box bb;
                    bb.grow(a);
                    bb.grow(b);
                    bb.grow(c);
                    const std::uint32_t encoded_first = (p.first + i) | ((i & 1u) ? kOddWindingBit : 0u);
                    add_translucent({(a.z + b.z + c.z) * (1.0f / 3.0f), encoded_first, 3u, even}, bb);
                }
                continue;
            }
            float nearest = 1e30f;
            for (std::uint32_t i = 0; i < p.count; ++i)
                nearest = std::min(nearest, frame.vertices[p.first + i].z);
            const float key = per_triangle ? p.sort_triangle : p.sort_strip;
            if (blended && !std::isnan(key))
                nearest = key;
            const Item item{nearest, p.first, p.count, even};
            count(even, p.count);
            if (!blended) {
                items.push_back(item);
                continue;
            }
            Box bb;
            for (std::uint32_t q = 0; q < p.count; ++q) bb.grow(frame.vertices[p.first + q]);
            add_translucent(item, bb);
        }
    };

    add_list(ListType::Opaque, false, false);
    add_list(ListType::PunchThrough, false, true);
    const std::size_t translucent_start = items.size();
    add_list(ListType::Translucent, true, false);

    thread_local std::vector<Translucent> in_order;
    sort_back_to_front(pairs);
    in_order.resize(pairs.size());
    for (std::size_t k = 0; k < pairs.size(); ++k)
        in_order[k] = translucent[static_cast<std::uint32_t>(pairs[k])];
    regroup(in_order, items);

    std::uint32_t* idx = nullptr;
    HostBuffer* ib = nullptr;
    if (indices != 0) {
        ib = &index_ring_[ring_slot];
        ib->ensure(*ctx_, indices * sizeof(std::uint32_t));
        idx = static_cast<std::uint32_t*>(ib->mapped());
    }
    std::uint32_t at = 0;

    enum class CmdType { Triangles, ModifierVolume };
    struct DrawCmd {
        CmdType type;
        std::uint32_t state_id;
        std::uint32_t first;
        std::uint32_t count;
    };
    thread_local std::vector<DrawCmd> cmds;
    cmds.clear();
    std::uint32_t cur_tri_state = kNoState;
    std::uint32_t cur_tri_first = 0;
    std::uint32_t cur_tri_count = 0;

    auto flush_tris = [&] {
        if (cur_tri_count != 0) {
            cmds.push_back({CmdType::Triangles, cur_tri_state, cur_tri_first, cur_tri_count});
            cur_tri_count = 0;
        }
    };

    const auto line_off_region = [&](const LineEdge& le, float hw) {
        return (std::max(le.xa, le.xb) + hw < geometry.left) ||
               (std::min(le.xa, le.xb) - hw > geometry.left + geometry.width) ||
               (std::max(le.ya, le.yb) + hw < geometry.top) ||
               (std::min(le.ya, le.yb) - hw > geometry.top + geometry.height);
    };

    bool volumes_drawn = false;
    for (std::size_t index = 0; index < items.size(); ++index) {
        const Item& item = items[index];
        if (!volumes_drawn && index == translucent_start) {
            flush_tris();
            cmds.push_back({CmdType::ModifierVolume, 0, 0, 0});
            volumes_drawn = true;
        }
        if (item.count == kLineBatch) {
            if (vb_base && idx && item.state != kNoState) {
                const LineBatch& b = frame.lines[item.first];
                if (b.count != 0) {
                    const std::uint32_t first_index = at;
                    const float hw = std::abs(b.half_width);
                    for (std::uint32_t e = b.first; e < b.first + b.count; ++e) {
                        const LineEdge& le = frame.line_edges[e];
                        if (line_off_region(le, hw))
                            continue;
                        float f6 = le.xb - le.xa;
                        float f5 = le.yb - le.ya;
                        const float len_sq = f6 * f6 + f5 * f5;
                        if (len_sq <= 0.0f)
                            continue;
                        const float s = (1.0f / std::sqrt(len_sq)) * b.half_width;
                        f5 *= s;
                        f6 *= s;
                        if (0.0f > f5) {
                            f5 = -f5;
                            f6 = -f6;
                        }
                        const float fog_a = table_fog ? fog_table_value(geometry.fog, le.za) : 0.0f;
                        const float fog_b = table_fog ? fog_table_value(geometry.fog, le.zb) : 0.0f;
                        auto emit_line_vert = [&](float x, float y, float z, float fog) {
                            Vertex lv{x, y, z, 0.0f, 0.0f, b.base, b.offset};
                            std::uint8_t* dst = vb_base + static_cast<std::size_t>(line_vert_at) * kVertexBytes;
                            std::memcpy(dst, &lv, sizeof lv);
                            std::memcpy(dst + sizeof lv, &fog, sizeof fog);
                            return line_vert_at++;
                        };
                        const std::uint32_t v0 = emit_line_vert(le.xb - f5, le.yb + f6, le.zb, fog_b);
                        const std::uint32_t v1 = emit_line_vert(le.xb + f5, le.yb - f6, le.zb, fog_b);
                        const std::uint32_t v2 = emit_line_vert(le.xa - f5, le.ya + f6, le.za, fog_a);
                        const std::uint32_t v3 = emit_line_vert(le.xa + f5, le.ya - f6, le.za, fog_a);
                        idx[at + 0] = v0;
                        idx[at + 1] = v1;
                        idx[at + 2] = v2;
                        idx[at + 3] = v1;
                        idx[at + 4] = v3;
                        idx[at + 5] = v2;
                        at += 6;
                    }
                    if (at > first_index) {
                        if (cur_tri_count != 0 && item.state == cur_tri_state) {
                            cur_tri_count += at - first_index;
                        } else {
                            flush_tris();
                            cur_tri_state = item.state;
                            cur_tri_first = first_index;
                            cur_tri_count = at - first_index;
                        }
                    }
                }
            }
            continue;
        }
        const std::uint32_t id = item.state;
        if (id == kNoState || item.count < 3)
            continue;
        const std::uint32_t first_index = at;
        const bool odd_start = (item.first & kOddWindingBit) != 0;
        const std::uint32_t base_v = item.first & ~kOddWindingBit;
        for (std::uint32_t j = 0; j + 2 < item.count; ++j) {
            const std::uint32_t v = base_v + j;
            const bool flip = ((j & 1u) != 0) ^ odd_start;
            idx[at] = v;
            idx[at + 1] = flip ? v + 2 : v + 1;
            idx[at + 2] = flip ? v + 1 : v + 2;
            at += 3;
        }
        if (cur_tri_count != 0 && id == cur_tri_state) {
            cur_tri_count += at - first_index;
        } else {
            flush_tris();
            cur_tri_state = id;
            cur_tri_first = first_index;
            cur_tri_count = at - first_index;
        }
    }
    flush_tris();
    if (!volumes_drawn)
        cmds.push_back({CmdType::ModifierVolume, 0, 0, 0});

    GLuint active_vao = 0;
    if (ib && at > 0 && line_vert_at > 0) {
        vb.upload_to_target(GL_ARRAY_BUFFER, static_cast<std::size_t>(line_vert_at) * kVertexBytes);
        glBindVertexArray(geom_vao_);
        active_vao = geom_vao_;
        glBindBuffer(GL_ARRAY_BUFFER, vb.gl_buffer());
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(kVertexBytes), reinterpret_cast<const void*>(0));
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(kVertexBytes), reinterpret_cast<const void*>(12));
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, static_cast<GLsizei>(kVertexBytes), reinterpret_cast<const void*>(20));
        glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, static_cast<GLsizei>(kVertexBytes), reinterpret_cast<const void*>(24));
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(kVertexBytes), reinterpret_cast<const void*>(28));
        for (GLuint i = 0; i < 5; ++i) {
            glEnableVertexAttribArray(i);
            glVertexAttribDivisor(i, 0);
        }
        ib->upload_to_target(GL_ELEMENT_ARRAY_BUFFER, at * sizeof(std::uint32_t));
    }

    auto prog_for = [this](const PipelineKey& key) -> GLuint {
        return key.discard ? prog_geom_d_ : prog_geom_nd_;
    };

    auto uniforms_for = [this](const PipelineKey& key) -> const ShaderUniforms& {
        return key.discard ? u_geom_d_ : u_geom_nd_;
    };

    GLuint active_prog = 0;
    std::uint32_t active_state_bits = ~0u;
    GLuint active_tex = ~0u;
    GLuint active_sampler = ~0u;
    VkRect2D active_scissor{};
    bool has_scissor = false;

    bool prog_frame_inited[4] = {};
    std::int32_t prog_last_mode[4] = {
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::min(),
    };
    float prog_last_clip[4][4] = {};
    bool prog_has_clip[4] = {};

    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_SCISSOR_TEST);

    for (const auto& cmd : cmds) {
        if (cmd.type == CmdType::ModifierVolume) {
            if (!frame.modifiers.empty()) {
                draw_modifier_volumes(frame, geometry);
                active_prog = 0;
                active_vao = 0;
                active_state_bits = ~0u;
                active_tex = ~0u;
                active_sampler = ~0u;
                has_scissor = false;
                glEnable(GL_SCISSOR_TEST);
                glActiveTexture(GL_TEXTURE0);
            }
            continue;
        }

        const DrawState& st = t_states[cmd.state_id];
        const PipelineKey key = key_from_bits(st.key);
        const unsigned prog_idx = key.discard ? 1u : 0u;
        const GLuint prog = prog_for(key);

        if (active_prog != prog) {
            glUseProgram(prog);
            active_prog = prog;
        }

        const auto& u = uniforms_for(key);
        if (!prog_frame_inited[prog_idx]) {
            if (u.u_tex >= 0) glUniform1i(u.u_tex, 0);
            if (u.u_scale >= 0) glUniform2fv(u.u_scale, 1, push.scale);
            if (u.u_offset >= 0) glUniform2fv(u.u_offset, 1, push.offset);
            if (u.u_alpha_ref >= 0) glUniform1f(u.u_alpha_ref, geometry.alpha_ref);
            if (u.u_fog_vert >= 0) glUniform4fv(u.u_fog_vert, 1, push.fog_vert);
            if (u.u_fog_ram >= 0) glUniform4fv(u.u_fog_ram, 1, push.fog_ram);
            prog_frame_inited[prog_idx] = true;
        }

        if (prog_last_mode[prog_idx] != st.push.mode) {
            if (u.u_mode >= 0) glUniform1i(u.u_mode, st.push.mode);
            prog_last_mode[prog_idx] = st.push.mode;
        }
        if ((st.push.mode & 512) != 0) {
            if (!prog_has_clip[prog_idx] ||
                std::memcmp(prog_last_clip[prog_idx], st.push.clip, sizeof st.push.clip) != 0) {
                if (u.u_clip >= 0) glUniform4fv(u.u_clip, 1, st.push.clip);
                std::memcpy(prog_last_clip[prog_idx], st.push.clip, sizeof st.push.clip);
                prog_has_clip[prog_idx] = true;
            }
        }

        const std::uint32_t new_state_bits = st.key & kGlPipelineStateMask;
        if (new_state_bits != active_state_bits) {
            if (active_state_bits == ~0u) {
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(depth_compare(key.depth_mode));
                glDepthMask(key.depth_write ? GL_TRUE : GL_FALSE);
                apply_pvr_cull_mode(static_cast<std::uint8_t>(key.cull_mode));
                if (key.blend) {
                    glEnable(GL_BLEND);
                    glBlendFuncSeparate(src_factor(key.src_blend), dst_factor(key.dst_blend),
                                        src_factor(key.src_blend), dst_factor(key.dst_blend));
                } else {
                    glDisable(GL_BLEND);
                }
                if (key.shadow) {
                    glEnable(GL_STENCIL_TEST);
                    glStencilFunc(GL_EQUAL, 0, 0xFF);
                    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
                } else {
                    glDisable(GL_STENCIL_TEST);
                }
            } else {
                const PipelineKey prev = key_from_bits(active_state_bits);
                if (prev.depth_mode != key.depth_mode)
                    glDepthFunc(depth_compare(key.depth_mode));
                if (prev.depth_write != key.depth_write)
                    glDepthMask(key.depth_write ? GL_TRUE : GL_FALSE);
                if (prev.cull_mode != key.cull_mode)
                    apply_pvr_cull_mode(static_cast<std::uint8_t>(key.cull_mode));
                if (prev.blend != key.blend) {
                    if (key.blend) {
                        glEnable(GL_BLEND);
                        glBlendFuncSeparate(src_factor(key.src_blend), dst_factor(key.dst_blend),
                                            src_factor(key.src_blend), dst_factor(key.dst_blend));
                    } else {
                        glDisable(GL_BLEND);
                    }
                } else if (key.blend && (prev.src_blend != key.src_blend || prev.dst_blend != key.dst_blend)) {
                    glBlendFuncSeparate(src_factor(key.src_blend), dst_factor(key.dst_blend),
                                        src_factor(key.src_blend), dst_factor(key.dst_blend));
                }
                if (prev.shadow != key.shadow) {
                    if (key.shadow) {
                        glEnable(GL_STENCIL_TEST);
                        glStencilFunc(GL_EQUAL, 0, 0xFF);
                        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
                    } else {
                        glDisable(GL_STENCIL_TEST);
                    }
                }
            }
            active_state_bits = new_state_bits;
        }

        const std::uint32_t sampler = static_cast<std::uint32_t>(st.set >> 32);
        const std::uint32_t tex = static_cast<std::uint32_t>(st.set & 0xFFFFFFFFu);
        if (tex != active_tex) {
            glBindTexture(GL_TEXTURE_2D, tex);
            active_tex = tex;
        }
        if (sampler != active_sampler) {
            glBindSampler(0, sampler);
            active_sampler = sampler;
        }

        if (!has_scissor || !same_rect(active_scissor, st.scissor)) {
            const GLint x = static_cast<GLint>(st.scissor.offset.x);
            const GLint y = static_cast<GLint>(st.scissor.offset.y);
            const GLsizei w = static_cast<GLsizei>(st.scissor.extent.width);
            const GLsizei h = static_cast<GLsizei>(st.scissor.extent.height);
            glScissor(std::max(0, x), std::max(0, y), std::max(0, w), std::max(0, h));
            active_scissor = st.scissor;
            has_scissor = true;
        }

        if (active_vao != geom_vao_) {
            glBindVertexArray(geom_vao_);
            active_vao = geom_vao_;
        }
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(cmd.count), GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(static_cast<std::uintptr_t>(cmd.first) * sizeof(std::uint32_t)));
    }

    glBindVertexArray(0);
}

}  // namespace dream::render::vk
