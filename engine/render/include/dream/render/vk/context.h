#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dream::render::vk {

using VkCommandBuffer = void*;
using VkRenderPass = std::uint64_t;
using VkFramebuffer = std::uint64_t;
using VkImage = std::uint64_t;
using VkImageView = std::uint64_t;
using VkSampler = std::uint64_t;
using VkDescriptorSet = std::uint64_t;
using VkDescriptorSetLayout = std::uint64_t;
using VkPipeline = std::uint64_t;
using VkPipelineLayout = std::uint64_t;
using VkBuffer = std::uint64_t;
using VkDeviceMemory = std::uint64_t;
using VkInstance = void*;
using VkPhysicalDevice = void*;
using VkDevice = void*;
using VkQueue = void*;

struct VkOffset2D {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

struct VkExtent2D {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct VkRect2D {
    VkOffset2D offset{};
    VkExtent2D extent{};
};

#ifndef VK_NULL_HANDLE
#define VK_NULL_HANDLE 0
#endif

struct DeviceCapabilities {
    bool fragment_stores_and_atomics = false;
    bool fragment_shader_interlock = false;
    bool independent_blend = false;
    bool sampler_anisotropy = false;
    bool win32_interop = false;
    bool dma_buf = false;
    bool zero_to_one_depth = false;
    std::uint32_t max_texture_size = 0;
    std::string device_name;
};

class Context {
public:
    Context() = default;
    ~Context() = default;
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    bool create(const std::vector<const char*>& surface_extensions = {});
    void destroy();

    VkInstance instance() const noexcept { return nullptr; }
    VkPhysicalDevice physical_device() const noexcept { return nullptr; }
    VkDevice device() const noexcept { return nullptr; }
    VkQueue queue() const noexcept { return nullptr; }
    std::uint32_t queue_family() const noexcept { return 0; }
    const DeviceCapabilities& caps() const noexcept { return caps_; }
    const std::string& error() const noexcept { return error_; }

private:
    DeviceCapabilities caps_;
    std::string error_;
};

}  // namespace dream::render::vk
