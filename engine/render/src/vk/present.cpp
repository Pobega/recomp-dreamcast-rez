#include "dream/render/vk/present.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <GLES3/gl3.h>

namespace dream::render::vk {

namespace {

constexpr const char* kPresentVertSrc = R"(#version 300 es
precision highp float;

uniform vec2 u_scale;
uniform vec2 u_offset;

out vec2 v_uv;

void main() {
    v_uv = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vec2 pos = v_uv * 2.0 - 1.0;
    vec2 p = pos * u_scale + u_offset;
    gl_Position = vec4(p.x, -p.y, 0.0, 1.0);
}
)";

constexpr const char* kPresentFragSrc = R"(#version 300 es
precision highp float;
precision lowp sampler2D;

uniform sampler2D u_image;
uniform vec2 u_scale;
uniform vec2 u_offset;
uniform vec2 u_texel;
uniform vec2 u_taps;
uniform vec2 u_mode;

in vec2 v_uv;
out vec4 colour;

const int kMaxTaps = 4;

void main() {
    if (any(lessThan(v_uv, vec2(0.0))) || any(greaterThan(v_uv, vec2(1.0))))
        discard;
    ivec2 n = ivec2(clamp(ceil(u_taps - 0.05), vec2(1.0), vec2(float(kMaxTaps))));
    if (n.x == 1 && n.y == 1) {
        vec4 t = texture(u_image, v_uv);
        colour = u_mode.x > 0.5 ? t : vec4(t.rgb, 1.0);
        return;
    }
    vec2 first = v_uv - 0.5 * (vec2(n) - 1.0) * u_texel;
    vec4 sum = vec4(0.0);
    for (int y = 0; y < n.y; ++y)
        for (int x = 0; x < n.x; ++x)
            sum += texture(u_image, first + vec2(float(x), float(y)) * u_texel);
    sum /= float(n.x * n.y);
    colour = u_mode.x > 0.5 ? sum : vec4(sum.rgb, 1.0);
}
)";

GLuint compile_shader(GLenum type, const char* src) {
    const GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link_program(GLuint vs, GLuint fs) {
    const GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = GL_FALSE;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

}  // namespace

Presenter::~Presenter() {
    destroy();
}

bool Presenter::create(Context& ctx, VkRenderPass) {
    destroy();
    ctx_ = &ctx;

    const GLuint vs = compile_shader(GL_VERTEX_SHADER, kPresentVertSrc);
    const GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kPresentFragSrc);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        error_ = "present shader compilation failed";
        return false;
    }

    program_ = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!program_) {
        error_ = "present program linking failed";
        return false;
    }

    u_scale_ = glGetUniformLocation(program_, "u_scale");
    u_offset_ = glGetUniformLocation(program_, "u_offset");
    u_texel_ = glGetUniformLocation(program_, "u_texel");
    u_taps_ = glGetUniformLocation(program_, "u_taps");
    u_mode_ = glGetUniformLocation(program_, "u_mode");
    u_image_ = glGetUniformLocation(program_, "u_image");

    glGenVertexArrays(1, &vao_);
    return true;
}

void Presenter::destroy() {
    if (tex_) {
        glDeleteTextures(1, &tex_);
        tex_ = 0;
    }
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    uploaded_width_ = 0;
    uploaded_height_ = 0;
    staging_.clear();
}

bool Presenter::upload(VkCommandBuffer, const std::uint32_t* rgba, std::uint32_t width,
                       std::uint32_t height,
                       const std::function<void(std::uint32_t* pixels, std::uint32_t width,
                                                std::uint32_t height)>& decorate) {
    if (width == 0 || height == 0)
        return false;

    if (!tex_ || width != uploaded_width_ || height != uploaded_height_) {
        if (tex_)
            glDeleteTextures(1, &tex_);
        glGenTextures(1, &tex_);
        glBindTexture(GL_TEXTURE_2D, tex_);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        const GLenum filter = smooth ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        uploaded_width_ = width;
        uploaded_height_ = height;
    }

    const std::uint32_t* src_px = rgba;
    if (decorate) {
        staging_.resize(static_cast<std::size_t>(width) * height);
        if (rgba)
            std::memcpy(staging_.data(), rgba, staging_.size() * sizeof(std::uint32_t));
        else
            std::memset(staging_.data(), 0, staging_.size() * sizeof(std::uint32_t));
        decorate(staging_.data(), width, height);
        src_px = staging_.data();
    }

    if (src_px) {
        glBindTexture(GL_TEXTURE_2D, tex_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                        GL_RGBA, GL_UNSIGNED_BYTE, src_px);
    }
    return true;
}

bool Presenter::draw(VkCommandBuffer cmd, VkExtent2D target) {
    if (!tex_)
        return false;
    return draw_view(cmd, static_cast<VkImageView>(tex_), uploaded_width_, uploaded_height_, target);
}

bool Presenter::draw_view(VkCommandBuffer, VkImageView view, std::uint32_t width,
                         std::uint32_t height, VkExtent2D target) {
    if (!program_ || width == 0 || height == 0 || target.width == 0 || target.height == 0)
        return false;

    float scale[2] = {1.0f, 1.0f};
    float offset[2] = {0.0f, 0.0f};

    if (overlay_layer) {
        scale[0] = 1.0f;
        scale[1] = 1.0f;
    } else {
        const auto& opts = present_options();
        const float src_aspect = opts.display_aspect > 0.0f
                                     ? opts.display_aspect
                                     : (static_cast<float>(width) / static_cast<float>(height));
        const float dst_aspect = static_cast<float>(target.width) / static_cast<float>(target.height);

        switch (opts.fit) {
            case PresentOptions::Fit::Stretch:
                scale[0] = 1.0f;
                scale[1] = 1.0f;
                break;
            case PresentOptions::Fit::Crop:
                if (dst_aspect > src_aspect) {
                    scale[0] = 1.0f;
                    scale[1] = dst_aspect / src_aspect;
                } else {
                    scale[0] = src_aspect / dst_aspect;
                    scale[1] = 1.0f;
                }
                break;
            case PresentOptions::Fit::Letterbox:
            default:
                if (dst_aspect > src_aspect) {
                    scale[0] = src_aspect / dst_aspect;
                    scale[1] = 1.0f;
                } else {
                    scale[0] = 1.0f;
                    scale[1] = dst_aspect / src_aspect;
                }
                break;
        }
    }

    const float out_w = static_cast<float>(target.width) * scale[0];
    const float out_h = static_cast<float>(target.height) * scale[1];
    const float taps[2] = {
        std::max(1.0f, static_cast<float>(width) / std::max(1.0f, out_w)),
        std::max(1.0f, static_cast<float>(height) / std::max(1.0f, out_h)),
    };
    const float texel[2] = {
        1.0f / static_cast<float>(width),
        1.0f / static_cast<float>(height),
    };

    glUseProgram(program_);
    if (u_scale_ >= 0)
        glUniform2f(u_scale_, scale[0], scale[1]);
    if (u_offset_ >= 0)
        glUniform2f(u_offset_, offset[0], offset[1]);
    if (u_texel_ >= 0)
        glUniform2f(u_texel_, texel[0], texel[1]);
    if (u_taps_ >= 0)
        glUniform2f(u_taps_, taps[0], taps[1]);
    if (u_mode_ >= 0)
        glUniform2f(u_mode_, overlay_layer ? 1.0f : 0.0f, 0.0f);
    if (u_image_ >= 0)
        glUniform1i(u_image_, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(view));
    glBindSampler(0, 0);

    if (overlay_layer) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    if (overlay_layer)
        glDisable(GL_BLEND);

    return true;
}

}  // namespace dream::render::vk
