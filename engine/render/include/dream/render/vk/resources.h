#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "dream/render/vk/context.h"

namespace dream::render::vk {

class HostBuffer {
public:
    HostBuffer() = default;
    ~HostBuffer();
    HostBuffer(const HostBuffer&) = delete;
    HostBuffer& operator=(const HostBuffer&) = delete;
    HostBuffer(HostBuffer&& o) noexcept;
    HostBuffer& operator=(HostBuffer&& o) noexcept;

    bool ensure(const Context& ctx, std::size_t bytes, std::uint32_t usage = 0, bool cached = false);
    void write(const void* data, std::size_t bytes, std::size_t offset = 0);
    void flush(std::size_t offset, std::size_t bytes);
    void destroy();

    void upload_to_target(std::uint32_t gl_target, std::size_t active_bytes = 0);

    VkBuffer handle() const noexcept;
    const void* mapped() const noexcept { return storage_.empty() ? nullptr : storage_.data(); }
    void* mapped() noexcept { return storage_.empty() ? nullptr : storage_.data(); }
    std::size_t capacity() const noexcept { return storage_.size(); }
    std::uint32_t gl_buffer() const noexcept { return gl_buf_; }

private:
    std::vector<std::uint8_t> storage_;
    std::uint32_t gl_buf_ = 0;
};

}  // namespace dream::render::vk
