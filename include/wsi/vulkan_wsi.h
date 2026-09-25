#ifndef NS_VULKAN_WSI_H
#define NS_VULKAN_WSI_H

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include <cstdint>
#include <vector>

struct ANativeWindow;

namespace ns
{

class VulkanWSI
{
   public:
    VulkanWSI();
    ~VulkanWSI();

    VulkanWSI(const VulkanWSI&) = delete;
    VulkanWSI& operator=(const VulkanWSI&) = delete;

    static std::vector<const char*> getRequiredInstanceExtensions();

    static const char* getRequiredDeviceExtension();

    bool createSurface(VkInstance instance, ANativeWindow* window);

    bool checkPresentationSupport(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex);

    bool initOrRecreateSwapchain(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height);

    VkResult acquireNextImage(VkDevice device, VkSemaphore semaphore, VkFence fence, uint32_t* outImageIndex);

    VkResult present(VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore);

    void cleanupSwapchain(VkDevice device);

    void release();

    void release(VkInstance instance, VkDevice device);

    VkSurfaceKHR getSurface() const
    {
        return mSurface;
    }
    VkSwapchainKHR getSwapchain() const
    {
        return mSwapchain;
    }
    VkFormat getFormat() const
    {
        return mFormat;
    }
    VkColorSpaceKHR getColorSpace() const
    {
        return mColorSpace;
    }
    VkExtent2D getExtent() const
    {
        return mExtent;
    }
    uint32_t getImageCount() const
    {
        return static_cast<uint32_t>(mImages.size());
    }
    const std::vector<VkImage>& getImages() const
    {
        return mImages;
    }
    const std::vector<VkImageView>& getImageViews() const
    {
        return mImageViews;
    }

   private:
    VkInstance mInstance = VK_NULL_HANDLE;
    VkDevice mDevice = VK_NULL_HANDLE;

    VkSurfaceKHR mSurface = VK_NULL_HANDLE;
    VkSwapchainKHR mSwapchain = VK_NULL_HANDLE;
    VkFormat mFormat = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR mColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D mExtent{0, 0};

    std::vector<VkImage> mImages;
    std::vector<VkImageView> mImageViews;
};

}  // namespace ns

#endif
