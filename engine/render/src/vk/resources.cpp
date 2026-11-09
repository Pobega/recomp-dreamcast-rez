#include "dream/render/vk/resources.h"

#include <cstring>
#include <GLES3/gl3.h>

namespace dream::render::vk {

HostBuffer::~HostBuffer() {
    destroy();
}

HostBuffer::HostBuffer(HostBuffer&& o) noexcept {
    *this = std::move(o);
}

HostBuffer& HostBuffer::operator=(HostBuffer&& o) noexcept {
    if (this != &o) {
        destroy();
        storage_ = std::move(o.storage_);
        gl_buf_ = o.gl_buf_;
        o.gl_buf_ = 0;
    }
    return *this;
}

bool HostBuffer::ensure(const Context&, std::size_t bytes, std::uint32_t, bool) {
    if (bytes > storage_.size())
        storage_.resize(bytes);
    if (!gl_buf_)
        glGenBuffers(1, &gl_buf_);
    return true;
}

void HostBuffer::write(const void* data, std::size_t bytes, std::size_t offset) {
    if (!data || offset + bytes > storage_.size())
        return;
    std::memcpy(storage_.data() + offset, data, bytes);
}

void HostBuffer::flush(std::size_t, std::size_t) {
}

void HostBuffer::upload_to_target(std::uint32_t gl_target) {
    if (!gl_buf_)
        glGenBuffers(1, &gl_buf_);
    glBindBuffer(gl_target, gl_buf_);
    glBufferData(gl_target, static_cast<GLsizeiptr>(storage_.size()), storage_.data(), GL_DYNAMIC_DRAW);
}

void HostBuffer::destroy() {
    if (gl_buf_) {
        glDeleteBuffers(1, &gl_buf_);
        gl_buf_ = 0;
    }
    storage_.clear();
}

VkBuffer HostBuffer::handle() const noexcept {
    return static_cast<VkBuffer>(static_cast<std::uintptr_t>(gl_buf_));
}

}  // namespace dream::render::vk
