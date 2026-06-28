// Context — Vulkan instance, physical/logical device, queue, VMA allocator,
// command pool and one-shot command helpers.
#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

struct GLFWwindow;

#define VK_CHECK(call)                                                        \
    do {                                                                      \
        VkResult res_ = (call);                                               \
        if (res_ != VK_SUCCESS)                                               \
            throw std::runtime_error(std::string("Vulkan error ") +          \
                                     std::to_string(res_) + " at " #call);    \
    } while (0)

namespace vk {

class Context
{
public:
    // window may be null for headless (offline) rendering.
    Context(GLFWwindow* window, bool enableValidation);
    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    VkInstance instance() const { return m_instance; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkDevice device() const { return m_device; }
    VkQueue queue() const { return m_queue; }
    uint32_t queueFamily() const { return m_queueFamily; }
    VkSurfaceKHR surface() const { return m_surface; }
    VmaAllocator allocator() const { return m_allocator; }
    VkCommandPool commandPool() const { return m_commandPool; }

    // Record + submit + wait a transient command buffer.
    void oneShot(const std::function<void(VkCommandBuffer)>& record) const;

    void waitIdle() const { vkDeviceWaitIdle(m_device); }

private:
    void createInstance(GLFWwindow* window, bool validation);
    void pickPhysicalDevice();
    void createDevice();

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamily = 0;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    bool m_validation = false;
};

} // namespace vk
