#include "vk/device.h"
#include <vector>
#include <stdexcept>
#include <set>

namespace bh2::vk {

static const std::vector<const char*> required_extensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

QueueFamilies Device::find_queue_families(VkPhysicalDevice dev, VkSurfaceKHR surface) {
    QueueFamilies qf;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, props.data());

    for (uint32_t i = 0; i < count; i++) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            VkBool32 present = VK_FALSE;
            if (surface) vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &present);
            if (!surface || present) qf.graphics = i;
        }
        if (props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            if (!qf.compute.has_value() || !(props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                qf.compute = i;
            }
        }
    }
    return qf;
}

void Device::pick_physical_device(VkInstance instance, VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) throw std::runtime_error("No Vulkan-capable GPU found");

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    for (auto dev : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);

        auto qf = find_queue_families(dev, surface);
        if (!qf.complete()) continue;

        uint32_t ext_count;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &ext_count, nullptr);
        std::vector<VkExtensionProperties> exts(ext_count);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &ext_count, exts.data());

        bool has_all = true;
        for (auto req : required_extensions) {
            bool found = false;
            for (auto& ext : exts) {
                if (strcmp(ext.extensionName, req) == 0) { found = true; break; }
            }
            if (!found) { has_all = false; break; }
        }
        if (!has_all) continue;

        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            physical_ = dev;
            families_ = qf;
            return;
        }

        if (!physical_) {
            physical_ = dev;
            families_ = qf;
        }
    }

    if (!physical_) throw std::runtime_error("No suitable GPU found");
}

void Device::init(VkInstance instance, VkSurfaceKHR surface) {
    pick_physical_device(instance, surface);

    std::set<uint32_t> unique_families = {
        families_.graphics.value(),
        families_.compute.value()
    };

    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queue_cis;
    for (uint32_t fam : unique_families) {
        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = fam;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        queue_cis.push_back(qci);
    }

    VkPhysicalDeviceFeatures features{};
    features.shaderFloat64 = VK_TRUE;

    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = static_cast<uint32_t>(queue_cis.size());
    ci.pQueueCreateInfos = queue_cis.data();
    ci.pEnabledFeatures = &features;
    ci.enabledExtensionCount = static_cast<uint32_t>(required_extensions.size());
    ci.ppEnabledExtensionNames = required_extensions.data();

    if (vkCreateDevice(physical_, &ci, nullptr, &device_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device");
    }

    vkGetDeviceQueue(device_, families_.graphics.value(), 0, &graphics_queue_);
    vkGetDeviceQueue(device_, families_.compute.value(), 0, &compute_queue_);
}

void Device::destroy() {
    if (device_) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
}

VkPhysicalDeviceProperties Device::properties() const {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical_, &props);
    return props;
}

} // namespace bh2::vk
