#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace bh2::vk {

struct InstanceCreateInfo {
    std::string app_name = "BlackHole2";
    bool enable_validation = true;
};

class Instance {
public:
    void init(const InstanceCreateInfo& info);
    void destroy();

    VkInstance handle() const { return instance_; }
    bool validation_enabled() const { return debug_messenger_ != VK_NULL_HANDLE; }

private:
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;

    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* user_data);
};

} // namespace bh2::vk
