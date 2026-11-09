#include "dream/render/vk/offscreen.h"

#include <algorithm>
#include <GLES3/gl3.h>

namespace dream::render::vk {

Offscreen::~Offscreen() {
    destroy();
}

bool Offscreen::create(Context& ctx, std::uint32_t width, std::uint32_t height) {
    destroy();
    ctx_ = &ctx;
    extent_ = {width, height};
    if (width == 0 || height == 0) {
        error_ = "offscreen render target needs non-zero dimensions";
        return false;
    }

    glGenTextures(1, &color_tex_);
    glBindTexture(GL_TEXTURE_2D, color_tex_);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenRenderbuffers(1, &depth_rb_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rb_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, static_cast<GLsizei>(width),
                          static_cast<GLsizei>(height));

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_rb_);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        error_ = "offscreen framebuffer is incomplete";
        destroy();
        return false;
    }
    return true;
}

void Offscreen::destroy() {
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    if (color_tex_) {
        glDeleteTextures(1, &color_tex_);
        color_tex_ = 0;
    }
    if (depth_rb_) {
        glDeleteRenderbuffers(1, &depth_rb_);
        depth_rb_ = 0;
    }
    extent_ = {};
    have_pixels_ = false;
    pixel_buf_.clear();
}

bool Offscreen::render(Renderer& renderer, const Frame& frame, const FrameGeometry& geometry) {
    renderer.prepare(nullptr, frame);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, static_cast<GLsizei>(extent_.width), static_cast<GLsizei>(extent_.height));

    const VkExtent2D sc_ext = (area.width && area.height) ? area : extent_;
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, static_cast<GLsizei>(sc_ext.width), static_cast<GLsizei>(sc_ext.height));

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFF);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepthf(0.0f);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    renderer.area = area;
    renderer.draw(nullptr, frame, geometry, extent_);

    if (after_pass)
        after_pass(nullptr, colour_image(), extent_);

    have_pixels_ = false;
    ++frames;
    if (readback)
        copy_to_host();
    return true;
}

bool Offscreen::copy_to_host() {
    if (!fbo_ || extent_.width == 0 || extent_.height == 0)
        return false;
    pixel_buf_.resize(static_cast<std::size_t>(extent_.width) * extent_.height);
    GLint prev_read_fbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glReadPixels(0, 0, static_cast<GLsizei>(extent_.width), static_cast<GLsizei>(extent_.height),
                 GL_RGBA, GL_UNSIGNED_BYTE, pixel_buf_.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_read_fbo));

    for (std::uint32_t y = 0; y < extent_.height / 2; ++y) {
        std::uint32_t* top = pixel_buf_.data() + y * extent_.width;
        std::uint32_t* bot = pixel_buf_.data() + (extent_.height - 1 - y) * extent_.width;
        std::swap_ranges(top, top + extent_.width, bot);
    }
    have_pixels_ = true;
    return true;
}

const std::uint32_t* Offscreen::pixels() noexcept {
    if (!have_pixels_)
        copy_to_host();
    return have_pixels_ ? pixel_buf_.data() : nullptr;
}

bool Offscreen::wait() {
    glFinish();
    return true;
}

VkImageView Offscreen::sampled_view() const noexcept {
    return static_cast<VkImageView>(color_tex_);
}

VkImage Offscreen::colour_image() const noexcept {
    return static_cast<VkImage>(color_tex_);
}

}  // namespace dream::render::vk
