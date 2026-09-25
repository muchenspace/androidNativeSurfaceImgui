#include "wsi/vulkan_wsi.h"

#include <android/native_window.h>

#include <algorithm>

#include "log.h"

namespace ns
{

VulkanWSI::VulkanWSI() = default;

VulkanWSI::~VulkanWSI()
{
    release();
}

std::vector<const char*> VulkanWSI::getRequiredInstanceExtensions()
{
    return {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
}

const char* VulkanWSI::getRequiredDeviceExtension()
{
    return VK_KHR_SWAPCHAIN_EXTENSION_NAME;
}

bool VulkanWSI::createSurface(VkInstance instance, ANativeWindow* window)
{
    if (instance == VK_NULL_HANDLE || window == nullptr)
    {
        NS_LOGE("VulkanWSI::createSurface: invalid instance or window");
        return false;
    }

    mInstance = instance;

    VkAndroidSurfaceCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    createInfo.window = window;

    VkResult res = vkCreateAndroidSurfaceKHR(instance, &createInfo, nullptr, &mSurface);
    if (res != VK_SUCCESS)
    {
        NS_LOGE("VulkanWSI::createSurface: vkCreateAndroidSurfaceKHR failed: %d", res);
        return false;
    }

    NS_LOGI("VulkanWSI::createSurface: VkSurfaceKHR successfully created (%p)", mSurface);
    return true;
}

bool VulkanWSI::checkPresentationSupport(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex)
{
    if (physicalDevice == VK_NULL_HANDLE || mSurface == VK_NULL_HANDLE)
    {
        return false;
    }

    VkBool32 supported = VK_FALSE;
    VkResult res = vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, mSurface, &supported);
    if (res != VK_SUCCESS || !supported)
    {
        NS_LOGE("VulkanWSI: surface presentation not supported on queue family %u", queueFamilyIndex);
        return false;
    }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, mSurface, &formatCount, nullptr);
    if (formatCount == 0)
    {
        NS_LOGE("VulkanWSI: zero surface formats found");
        return false;
    }

    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, mSurface, &formatCount, formats.data());

    mFormat = formats[0].format;
    mColorSpace = formats[0].colorSpace;
    for (const auto& f : formats)
    {
        if ((f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            mFormat = f.format;
            mColorSpace = f.colorSpace;
            break;
        }
    }

    NS_LOGI("VulkanWSI: format negotiated: %d, colorSpace: %d", mFormat, mColorSpace);
    return true;
}

bool VulkanWSI::initOrRecreateSwapchain(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width,
                                        uint32_t height)
{
    if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || mSurface == VK_NULL_HANDLE)
    {
        NS_LOGE("VulkanWSI: cannot recreate swapchain without valid device/surface");
        return false;
    }

    mDevice = device;

    vkDeviceWaitIdle(device);

    VkSurfaceCapabilitiesKHR caps{};
    VkResult res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, mSurface, &caps);
    if (res != VK_SUCCESS)
    {
        NS_LOGE("VulkanWSI: vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed: %d", res);
        return false;
    }

    uint32_t targetW = width > 0 ? width : 1080;
    uint32_t targetH = height > 0 ? height : 1920;

    mExtent.width = std::clamp(targetW, caps.minImageExtent.width, caps.maxImageExtent.width);
    mExtent.height = std::clamp(targetH, caps.minImageExtent.height, caps.maxImageExtent.height);

    if (mExtent.width == 0 || mExtent.height == 0)
    {
        NS_LOGW("VulkanWSI: surface extent is 0, skipping swapchain recreation");
        return false;
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
    {
        imageCount = caps.maxImageCount;
    }

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
    {
        compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    }
    else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
    {
        compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }
    else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
    {
        compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }

    VkSurfaceTransformFlagBitsKHR preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    if (!(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR))
    {
        preTransform = caps.currentTransform;
    }

    VkSwapchainKHR oldSwapchain = mSwapchain;

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = mSurface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = mFormat;
    createInfo.imageColorSpace = mColorSpace;
    createInfo.imageExtent = mExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = preTransform;
    createInfo.compositeAlpha = compositeAlpha;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = oldSwapchain;

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    res = vkCreateSwapchainKHR(device, &createInfo, nullptr, &newSwapchain);
    if (res != VK_SUCCESS)
    {
        NS_LOGE("VulkanWSI: vkCreateSwapchainKHR failed: %d", res);
        return false;
    }

    cleanupSwapchain(device);
    mSwapchain = newSwapchain;

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(device, mSwapchain, &actualCount, nullptr);
    mImages.resize(actualCount);
    vkGetSwapchainImagesKHR(device, mSwapchain, &actualCount, mImages.data());

    mImageViews.resize(actualCount);
    for (size_t i = 0; i < actualCount; ++i)
    {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = mImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = mFormat;
        viewInfo.components = {
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
        };
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &mImageViews[i]) != VK_SUCCESS)
        {
            NS_LOGE("VulkanWSI: vkCreateImageView failed for image %zu", i);
            return false;
        }
    }

    const char* alphaName = (compositeAlpha == VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
                                ? "PRE_MULTIPLIED"
                                : (compositeAlpha == VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR ? "INHERIT" : "OPAQUE");
    NS_LOGI("VulkanWSI: Swapchain active (%ux%u, imageCount=%u, compositeAlpha=%s, presentMode=FIFO)", mExtent.width,
            mExtent.height, actualCount, alphaName);
    return true;
}

VkResult VulkanWSI::acquireNextImage(VkDevice device, VkSemaphore semaphore, VkFence fence, uint32_t* outImageIndex)
{
    if (mSwapchain == VK_NULL_HANDLE)
        return VK_ERROR_INITIALIZATION_FAILED;
    return vkAcquireNextImageKHR(device, mSwapchain, UINT64_MAX, semaphore, fence, outImageIndex);
}

VkResult VulkanWSI::present(VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore)
{
    if (mSwapchain == VK_NULL_HANDLE)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    if (waitSemaphore != VK_NULL_HANDLE)
    {
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &waitSemaphore;
    }
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &mSwapchain;
    presentInfo.pImageIndices = &imageIndex;

    return vkQueuePresentKHR(queue, &presentInfo);
}

void VulkanWSI::cleanupSwapchain(VkDevice device)
{
    if (device == VK_NULL_HANDLE)
    {
        device = mDevice;
    }
    if (device == VK_NULL_HANDLE)
        return;

    for (auto iv : mImageViews)
    {
        if (iv != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, iv, nullptr);
        }
    }
    mImageViews.clear();
    mImages.clear();

    if (mSwapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device, mSwapchain, nullptr);
        mSwapchain = VK_NULL_HANDLE;
    }
}

void VulkanWSI::release()
{
    release(mInstance, mDevice);
}

void VulkanWSI::release(VkInstance instance, VkDevice device)
{
    if (device == VK_NULL_HANDLE)
    {
        device = mDevice;
    }
    if (instance == VK_NULL_HANDLE)
    {
        instance = mInstance;
    }

    cleanupSwapchain(device);

    if (mSurface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE)
    {
        NS_LOGI("VulkanWSI: destroying VkSurfaceKHR (%p)", mSurface);
        vkDestroySurfaceKHR(instance, mSurface, nullptr);
        mSurface = VK_NULL_HANDLE;
    }

    mDevice = VK_NULL_HANDLE;
    mInstance = VK_NULL_HANDLE;
}

}  // namespace ns
