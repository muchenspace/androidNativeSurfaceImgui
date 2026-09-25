#include <android/log.h>
#include <android/native_window.h>
#include <jni.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#include <dirent.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#define STBRP_STATIC
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"
#include "imgui_android_native.h"

#define LOG_TAG "ImGuiExample"
#define LOGI(...)                                                    \
    do                                                               \
    {                                                                \
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); \
        std::printf("[I/ImGuiExample] " __VA_ARGS__);                \
        std::printf("\n");                                           \
        std::fflush(stdout);                                         \
    } while (0)
#define LOGW(...)                                                    \
    do                                                               \
    {                                                                \
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__); \
        std::printf("[W/ImGuiExample] " __VA_ARGS__);                \
        std::printf("\n");                                           \
        std::fflush(stdout);                                         \
    } while (0)
#define LOGE(...)                                                     \
    do                                                                \
    {                                                                 \
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__); \
        std::fprintf(stderr, "[E/ImGuiExample] " __VA_ARGS__);        \
        std::fprintf(stderr, "\n");                                   \
        std::fflush(stderr);                                          \
    } while (0)

namespace
{

std::atomic<bool> gStopRequested{false};

void HandleSignal(int)
{
    gStopRequested.store(true);
}

struct Options
{
    std::string name = "NativeSurfaceOverlay";
    int width = 0;
    int height = 0;
    bool trusted = true;
    bool watch = true;
    int watchMs = 250;
    long onceMs = 0;
};

void PrintUsage()
{
    std::printf(
        "Usage: androidNativeSurfaceImgui [options]\n"
        "  --name <s>       layer name (default NativeSurfaceOverlay)\n"
        "  --width <n>      buffer width  (default screen width)\n"
        "  --height <n>     buffer height (default screen height)\n"
        "  --no-trusted     skip setTrustedOverlay\n"
        "  --no-watch       disable rotation / size tracking\n"
        "  --watch-ms <n>   display poll interval (default 250)\n"
        "  --once [ms]      run for ms then exit (default 3000)\n"
        "  -h, --help       this help\n"
        "\n"
        "Frame rate is always vsync-locked (FIFO); there is no fps option.\n");
}

Options ParseOptions(const std::vector<std::string>& args)
{
    Options opt;
    size_t start = 0;
    if (!args.empty() && args[0].rfind("-", 0) != 0)
    {
        start = 1;
    }
    for (size_t i = start; i < args.size(); ++i)
    {
        const std::string& a = args[i];
        if (a == "--name" && i + 1 < args.size())
        {
            opt.name = args[++i];
        }
        else if (a == "--width" && i + 1 < args.size())
        {
            opt.width = std::atoi(args[++i].c_str());
        }
        else if (a == "--height" && i + 1 < args.size())
        {
            opt.height = std::atoi(args[++i].c_str());
        }
        else if (a == "--no-trusted")
        {
            opt.trusted = false;
        }
        else if (a == "--no-watch")
        {
            opt.watch = false;
        }
        else if (a == "--watch-ms" && i + 1 < args.size())
        {
            opt.watchMs = std::atoi(args[++i].c_str());
        }
        else if (a == "--once")
        {
            opt.onceMs = 3000;
            if (i + 1 < args.size() && args[i + 1][0] >= '0' && args[i + 1][0] <= '9')
            {
                opt.onceMs = std::atol(args[++i].c_str());
            }
        }
        else if (a == "-h" || a == "--help")
        {
            PrintUsage();
            std::exit(0);
        }
        else
        {
            std::printf("unknown option: %s\n", a.c_str());
            PrintUsage();
            std::exit(2);
        }
    }
    return opt;
}

bool ValidateChineseFont(const std::string& fontPath)
{
    std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        return false;
    }
    std::streamsize size = file.tellg();
    if (size < 1024 || size > 150 * 1024 * 1024)
    {
        return false;
    }
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
    {
        return false;
    }

    int offset = stbtt_GetFontOffsetForIndex(buffer.data(), 0);
    if (offset < 0)
    {
        return false;
    }

    stbtt_fontinfo info = {};
    if (!stbtt_InitFont(&info, buffer.data(), offset))
    {
        return false;
    }

    // Verify key Chinese characters exist: '测'(0x6D4B), '试'(0x8BD5), '中'(0x4E2D)
    static const int requiredCodepoints[] = {0x6D4B, 0x8BD5, 0x4E2D};
    for (int cp : requiredCodepoints)
    {
        if (stbtt_FindGlyphIndex(&info, cp) <= 0)
        {
            return false;
        }
    }

    int glyph = stbtt_FindGlyphIndex(&info, 0x6D4B);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBox(&info, glyph, &x0, &y0, &x1, &y1);
    if (x0 == 0 && y0 == 0 && x1 == 0 && y1 == 0)
    {
        return false;
    }

    return true;
}

std::string FindSystemChineseFont()
{
    static const char* candidateDirs[] = {
        "/system/fonts",
        "/product/fonts",
        "/system_ext/fonts",
        "/data/local/tmp"
    };

    static const char* priorityKeywords[] = {
        "MiSans", "SysSans", "Hans", "SC", "CN", "Harmony", "DroidSansFallback", "Chinese"
    };

    std::vector<std::string> priorityMatches;
    std::vector<std::string> otherMatches;

    for (const char* dirPath : candidateDirs)
    {
        DIR* dir = opendir(dirPath);
        if (!dir)
        {
            continue;
        }

        struct dirent* entry = nullptr;
        while ((entry = readdir(dir)) != nullptr)
        {
            if (entry->d_type != DT_REG && entry->d_type != DT_LNK && entry->d_type != DT_UNKNOWN)
            {
                continue;
            }

            std::string name = entry->d_name;
            if (name.size() < 4)
            {
                continue;
            }

            std::string ext = name.substr(name.size() - 4);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext != ".ttf" && ext != ".otf")
            {
                continue;
            }

            std::string fullPath = std::string(dirPath) + "/" + name;

            bool isPriority = false;
            for (const char* kw : priorityKeywords)
            {
                if (name.find(kw) != std::string::npos)
                {
                    isPriority = true;
                    break;
                }
            }

            if (isPriority)
            {
                priorityMatches.push_back(fullPath);
            }
            else
            {
                otherMatches.push_back(fullPath);
            }
        }
        closedir(dir);
    }

    for (const auto& path : priorityMatches)
    {
        if (ValidateChineseFont(path))
        {
            LOGI("Found valid priority Chinese font: %s", path.c_str());
            return path;
        }
    }

    for (const auto& path : otherMatches)
    {
        if (ValidateChineseFont(path))
        {
            LOGI("Found valid fallback Chinese font: %s", path.c_str());
            return path;
        }
    }

    LOGW("No valid Chinese font found on system, will use default font");
    return "";
}

struct UiState
{
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    std::vector<float> fpsHistory;
};

void RenderSampleUi(UiState& state, const ns::NativeSurface& surface, const ns::VulkanWSI& wsi, int orientation)
{
    int32_t winW = 0, winH = 0;
    surface.size(&winW, &winH);
    int32_t minDim = (winW > 0 && winH > 0) ? ((winW < winH) ? winW : winH) : 1080;
    float dpiScale = std::max(1.0f, static_cast<float>(minDim) / 360.0f);

    float panelW = std::min(static_cast<float>(winW > 0 ? winW : 1080) - 40.0f, 320.0f * dpiScale);
    float panelH = std::min(static_cast<float>(winH > 0 ? winH : 1920) - 80.0f, 480.0f * dpiScale);
    ImGui::SetNextWindowPos(ImVec2(20.0f * dpiScale, 20.0f * dpiScale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("androidNativeSurfaceImgui测试", nullptr, ImGuiWindowFlags_None))
    {
        ImGuiIO& io = ImGui::GetIO();
        float currentFps = io.Framerate;
        if (state.fpsHistory.size() >= 60)
        {
            state.fpsHistory.erase(state.fpsHistory.begin());
        }
        state.fpsHistory.push_back(currentFps);

        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "渲染后端: Vulkan  |  输入源: Linux Evdev");
        ImGui::Separator();

        ImGui::Text("实时帧率: %.1f FPS (帧耗时: %.2f ms)", currentFps, 1000.0f / (currentFps > 0 ? currentFps : 1.0f));
        if (!state.fpsHistory.empty())
        {
            ImGui::PlotLines("##帧率历史", state.fpsHistory.data(), static_cast<int>(state.fpsHistory.size()), 0,
                             nullptr, 0.0f, 120.0f, ImVec2(-1, 25.0f * dpiScale));
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("系统图层与 WSI 状态", ImGuiTreeNodeFlags_DefaultOpen))
        {
            int32_t winW = 0, winH = 0;
            surface.size(&winW, &winH);
            ImGui::BulletText("窗口分辨率: %d x %d", winW, winH);
            ImGui::BulletText("交换链尺寸: %u x %u (缓冲数: %u)", wsi.getExtent().width, wsi.getExtent().height,
                              wsi.getImageCount());
            ImGui::BulletText("屏幕方向: %d°", orientation);
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("触控映射测试", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* devPath = ImGui_ImplAndroidNative_GetTouchDevicePath();
            const char* devName = ImGui_ImplAndroidNative_GetTouchDeviceName();
            uint64_t evtCount = ImGui_ImplAndroidNative_GetTouchEventCount();

            ImGui::BulletText("输入设备: %s (%s)", (devPath && devPath[0]) ? devPath : "未检测到",
                              (devName && devName[0]) ? devName : "未知");
            if (io.MouseDown[0])
            {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "实时状态: [按下输入中]");
            }
            else
            {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "实时状态: [空闲抬起]");
            }
            ImGui::SameLine();
            ImGui::Text("| 事件流计数: %llu", static_cast<unsigned long long>(evtCount));
            ImGui::Text("触控坐标: (%.1f, %.1f)", io.MousePos.x, io.MousePos.y);

            ImVec2 canvasP0 = ImGui::GetCursorScreenPos();
            ImVec2 canvasSz = ImVec2(ImGui::GetContentRegionAvail().x, 60.0f * dpiScale);
            ImVec2 canvasP1 = ImVec2(canvasP0.x + canvasSz.x, canvasP0.y + canvasSz.y);

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(canvasP0, canvasP1, IM_COL32(30, 30, 35, 255), 6.0f);
            drawList->AddRect(canvasP0, canvasP1, IM_COL32(100, 100, 110, 255), 6.0f);

            drawList->AddText(ImVec2(canvasP0.x + 10, canvasP0.y + 10), IM_COL32(180, 180, 180, 255),
                              "在此区域滑动测试触控映射");

            if (io.MouseDown[0] && io.MousePos.x >= canvasP0.x && io.MousePos.x <= canvasP1.x &&
                io.MousePos.y >= canvasP0.y && io.MousePos.y <= canvasP1.y)
            {
                drawList->AddCircleFilled(io.MousePos, 8.0f * dpiScale, IM_COL32(255, 80, 80, 200));
                drawList->AddCircle(io.MousePos, 12.0f * dpiScale, IM_COL32(255, 255, 255, 220), 0, 2.0f);
            }
            ImGui::Dummy(canvasSz);
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("背景清除色与透明度"))
        {
            ImGui::ColorEdit4("清除色", state.clearColor);
        }

        ImGui::Separator();
        if (ImGui::Button("退出程序", ImVec2(-1, 16.0f * dpiScale)))
        {
            gStopRequested.store(true);
        }
    }
    ImGui::End();
}

constexpr uint32_t kFramesInFlight = 2;

struct VulkanContext
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex = 0;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkCommandBuffer> cmdBuffers;
    std::vector<VkSemaphore> acqSemaphores;
    std::vector<VkSemaphore> rendSemaphores;
    std::vector<VkFence> inFlightFences;
};

bool CreateVulkanInstance(VulkanContext& vk)
{
    std::vector<const char*> instanceExtensions = ns::VulkanWSI::getRequiredInstanceExtensions();

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "androidNativeSurfaceImgui";
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
    createInfo.ppEnabledExtensionNames = instanceExtensions.data();

    return vkCreateInstance(&createInfo, nullptr, &vk.instance) == VK_SUCCESS;
}

bool SelectPhysicalDevice(VulkanContext& vk)
{
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(vk.instance, &gpuCount, nullptr);
    if (gpuCount == 0)
    {
        return false;
    }

    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(vk.instance, &gpuCount, gpus.data());
    vk.physicalDevice = gpus[0];

    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vk.physicalDevice, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> qProps(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(vk.physicalDevice, &qCount, qProps.data());

    for (uint32_t i = 0; i < qCount; ++i)
    {
        if (qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            vk.queueFamilyIndex = i;
            return true;
        }
    }
    return false;
}

bool CreateLogicalDevice(VulkanContext& vk)
{
    float qPriority = 1.0f;
    VkDeviceQueueCreateInfo qCreateInfo = {};
    qCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qCreateInfo.queueFamilyIndex = vk.queueFamilyIndex;
    qCreateInfo.queueCount = 1;
    qCreateInfo.pQueuePriorities = &qPriority;

    std::vector<const char*> deviceExtensions = {ns::VulkanWSI::getRequiredDeviceExtension()};

    VkDeviceCreateInfo devCreateInfo = {};
    devCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devCreateInfo.queueCreateInfoCount = 1;
    devCreateInfo.pQueueCreateInfos = &qCreateInfo;
    devCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    devCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (vkCreateDevice(vk.physicalDevice, &devCreateInfo, nullptr, &vk.device) != VK_SUCCESS)
    {
        return false;
    }
    vkGetDeviceQueue(vk.device, vk.queueFamilyIndex, 0, &vk.graphicsQueue);
    return true;
}

bool CreateRenderPass(VulkanContext& vk, VkFormat format)
{
    VkAttachmentDescription attachment = {};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpCreateInfo = {};
    rpCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpCreateInfo.attachmentCount = 1;
    rpCreateInfo.pAttachments = &attachment;
    rpCreateInfo.subpassCount = 1;
    rpCreateInfo.pSubpasses = &subpass;
    rpCreateInfo.dependencyCount = 1;
    rpCreateInfo.pDependencies = &dependency;

    return vkCreateRenderPass(vk.device, &rpCreateInfo, nullptr, &vk.renderPass) == VK_SUCCESS;
}

bool CreateDescriptorPool(VulkanContext& vk)
{
    VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16}};
    VkDescriptorPoolCreateInfo dpCreateInfo = {};
    dpCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpCreateInfo.maxSets = 16;
    dpCreateInfo.poolSizeCount = 1;
    dpCreateInfo.pPoolSizes = poolSizes;

    return vkCreateDescriptorPool(vk.device, &dpCreateInfo, nullptr, &vk.descriptorPool) == VK_SUCCESS;
}

void CreateFramebuffers(VulkanContext& vk, const ns::VulkanWSI& wsi)
{
    for (VkFramebuffer fb : vk.framebuffers)
    {
        if (fb != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(vk.device, fb, nullptr);
        }
    }
    vk.framebuffers.clear();

    const auto& views = wsi.getImageViews();
    VkExtent2D extent = wsi.getExtent();
    vk.framebuffers.resize(views.size());

    for (size_t i = 0; i < views.size(); ++i)
    {
        VkImageView att[] = {views[i]};
        VkFramebufferCreateInfo fbInfo = {};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = vk.renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = att;
        fbInfo.width = extent.width;
        fbInfo.height = extent.height;
        fbInfo.layers = 1;
        vkCreateFramebuffer(vk.device, &fbInfo, nullptr, &vk.framebuffers[i]);
    }
}

bool CreateCommandAndSyncObjects(VulkanContext& vk)
{
    VkCommandPoolCreateInfo cpInfo = {};
    cpInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpInfo.queueFamilyIndex = vk.queueFamilyIndex;
    if (vkCreateCommandPool(vk.device, &cpInfo, nullptr, &vk.commandPool) != VK_SUCCESS)
    {
        return false;
    }

    vk.cmdBuffers.resize(kFramesInFlight);
    VkCommandBufferAllocateInfo cbAlloc = {};
    cbAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAlloc.commandPool = vk.commandPool;
    cbAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandBufferCount = kFramesInFlight;
    if (vkAllocateCommandBuffers(vk.device, &cbAlloc, vk.cmdBuffers.data()) != VK_SUCCESS)
    {
        return false;
    }

    vk.acqSemaphores.resize(kFramesInFlight);
    vk.rendSemaphores.resize(kFramesInFlight);
    vk.inFlightFences.resize(kFramesInFlight);

    VkSemaphoreCreateInfo semInfo = {};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < kFramesInFlight; ++i)
    {
        if (vkCreateSemaphore(vk.device, &semInfo, nullptr, &vk.acqSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(vk.device, &semInfo, nullptr, &vk.rendSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(vk.device, &fenceInfo, nullptr, &vk.inFlightFences[i]) != VK_SUCCESS)
        {
            return false;
        }
    }
    return true;
}

bool InitVulkan(VulkanContext& vk, ns::NativeSurface& surface, ns::VulkanWSI& wsi, int reqW, int reqH)
{
    if (!CreateVulkanInstance(vk))
    {
        LOGE("Failed to create VkInstance");
        return false;
    }
    if (!SelectPhysicalDevice(vk))
    {
        LOGE("Failed to select graphics physical device");
        return false;
    }
    if (!CreateLogicalDevice(vk))
    {
        LOGE("Failed to create VkDevice");
        return false;
    }
    if (!wsi.createSurface(vk.instance, surface.handle()))
    {
        LOGE("wsi.createSurface failed");
        return false;
    }
    if (!wsi.checkPresentationSupport(vk.physicalDevice, vk.queueFamilyIndex))
    {
        LOGE("wsi presentation not supported");
        return false;
    }

    int32_t winW = 0, winH = 0;
    surface.size(&winW, &winH);
    uint32_t targetW = winW > 0 ? static_cast<uint32_t>(winW) : static_cast<uint32_t>(reqW > 0 ? reqW : 1080);
    uint32_t targetH = winH > 0 ? static_cast<uint32_t>(winH) : static_cast<uint32_t>(reqH > 0 ? reqH : 1920);

    if (!wsi.initOrRecreateSwapchain(vk.device, vk.physicalDevice, targetW, targetH))
    {
        LOGE("wsi.initOrRecreateSwapchain failed");
        return false;
    }
    if (!CreateRenderPass(vk, wsi.getFormat()))
    {
        LOGE("Failed to create RenderPass");
        return false;
    }
    if (!CreateDescriptorPool(vk))
    {
        LOGE("Failed to create DescriptorPool");
        return false;
    }
    CreateFramebuffers(vk, wsi);
    if (!CreateCommandAndSyncObjects(vk))
    {
        LOGE("Failed to allocate command buffers and synchronization objects");
        return false;
    }
    return true;
}

void CleanupVulkan(VulkanContext& vk, ns::VulkanWSI& wsi)
{
    for (VkFramebuffer fb : vk.framebuffers)
    {
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(vk.device, fb, nullptr);
    }
    vk.framebuffers.clear();

    for (VkSemaphore s : vk.acqSemaphores)
    {
        if (s != VK_NULL_HANDLE)
            vkDestroySemaphore(vk.device, s, nullptr);
    }
    for (VkSemaphore s : vk.rendSemaphores)
    {
        if (s != VK_NULL_HANDLE)
            vkDestroySemaphore(vk.device, s, nullptr);
    }
    for (VkFence f : vk.inFlightFences)
    {
        if (f != VK_NULL_HANDLE)
            vkDestroyFence(vk.device, f, nullptr);
    }

    if (vk.commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(vk.device, vk.commandPool, nullptr);
    }
    if (vk.descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(vk.device, vk.descriptorPool, nullptr);
    }
    if (vk.renderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(vk.device, vk.renderPass, nullptr);
    }

    wsi.release();

    if (vk.device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(vk.device, nullptr);
    }
    if (vk.instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(vk.instance, nullptr);
    }
}

}  // namespace

int run(int argc, char** argv)
{
    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(argc > 0 ? argc : 0));
    for (int i = 0; i < argc; ++i)
    {
        args.emplace_back(argv[i] != nullptr ? argv[i] : "");
    }
    const Options opt = ParseOptions(args);

    std::signal(SIGTERM, HandleSignal);
    std::signal(SIGINT, HandleSignal);

    LOGI("=== androidNativeSurfaceImgui standalone ELF starting (pid=%d, argc=%d) ===", static_cast<int>(::getpid()), argc);

    ns::NativeSurface surface;
    ns::NativeSurface::Options surfaceOpt;
    surfaceOpt.name = opt.name.c_str();
    surfaceOpt.width = opt.width;
    surfaceOpt.height = opt.height;
    surfaceOpt.trustedOverlay = opt.trusted;
    surfaceOpt.watchDisplay = opt.watch;
    surfaceOpt.watchIntervalMs = opt.watchMs;
    if (!surface.create(surfaceOpt))
    {
        LOGE("surface.create failed");
        return 1;
    }

    VulkanContext vk;
    ns::VulkanWSI wsi;
    if (!InitVulkan(vk, surface, wsi, opt.width, opt.height))
    {
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    int32_t initW = 0, initH = 0;
    surface.size(&initW, &initH);
    int32_t minDim = (initW > 0 && initH > 0) ? ((initW < initH) ? initW : initH) : 1080;
    float dpiScale = std::max(1.0f, static_cast<float>(minDim) / 360.0f);

    ImGui::GetStyle().ScaleAllSizes(dpiScale);

    ImGuiIO& io = ImGui::GetIO();
    std::string fontPath = FindSystemChineseFont();
    ImFont* font = nullptr;
    if (!fontPath.empty())
    {
        static ImVector<ImWchar> ranges;
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        static const ImWchar extraRanges[] = {0x00B0, 0x00B0, 0x2000, 0x206F, 0x3000, 0x30FF, 0xFF00, 0xFFEF, 0};
        builder.AddRanges(extraRanges);
        builder.AddText(
            "在此区域滑动测试触控映射悬浮窗测试渲染后端输入源实时帧率耗时系统图层交换链尺寸缓冲数屏幕方向触控坐标状态按"
            "下抬起背景清除色透明度退出程序活跃空闲事件流计数未检测到未知");
        builder.BuildRanges(&ranges);
        float fontSize = std::round(14.0f * dpiScale);
        font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), fontSize, nullptr, ranges.Data);
    }
    if (font == nullptr)
    {
        io.Fonts->AddFontDefault();
        io.FontGlobalScale = dpiScale;
    }
    else
    {
        io.FontGlobalScale = 1.0f;
    }

    ImGui_ImplAndroidNative_Init(surface.handle());

    ImGui_ImplVulkan_InitInfo vkInitInfo = {};
    vkInitInfo.Instance = vk.instance;
    vkInitInfo.PhysicalDevice = vk.physicalDevice;
    vkInitInfo.Device = vk.device;
    vkInitInfo.QueueFamily = vk.queueFamilyIndex;
    vkInitInfo.Queue = vk.graphicsQueue;
    vkInitInfo.DescriptorPool = vk.descriptorPool;
    vkInitInfo.RenderPass = vk.renderPass;
    vkInitInfo.MinImageCount = wsi.getImageCount();
    vkInitInfo.ImageCount = wsi.getImageCount();
    vkInitInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    vkInitInfo.CheckVkResultFn = [](VkResult err)
    {
        if (err != VK_SUCCESS)
        {
            LOGE("Vulkan check_vk_result: %d", err);
        }
    };

    if (!ImGui_ImplVulkan_Init(&vkInitInfo))
    {
        LOGE("ImGui_ImplVulkan_Init failed");
        return 1;
    }

    UiState state;
    uint32_t frameIndex = 0;
    const auto startedAt = std::chrono::steady_clock::now();

    while (!gStopRequested.load(std::memory_order_acquire))
    {
        if (opt.onceMs > 0)
        {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt)
                    .count();
            if (elapsed >= opt.onceMs)
            {
                break;
            }
        }

        int32_t newW = 0, newH = 0;
        if (surface.checkSizeChanged(&newW, &newH))
        {
            vkDeviceWaitIdle(vk.device);
            wsi.initOrRecreateSwapchain(vk.device, vk.physicalDevice, static_cast<uint32_t>(newW),
                                        static_cast<uint32_t>(newH));
            CreateFramebuffers(vk, wsi);
            ImGui_ImplAndroidNative_SetWindow(surface.handle());
            ImGui_ImplVulkan_SetMinImageCount(wsi.getImageCount());
        }

        const int orientation = surface.orientation();
        ImGui_ImplAndroidNative_SetOrientation(orientation);

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplAndroidNative_NewFrame();
        ImGui::NewFrame();

        RenderSampleUi(state, surface, wsi, orientation);

        ImGui::Render();

        VkFence fence = vk.inFlightFences[frameIndex];
        VkSemaphore acqSem = vk.acqSemaphores[frameIndex];
        VkSemaphore rendSem = vk.rendSemaphores[frameIndex];
        VkCommandBuffer cmd = vk.cmdBuffers[frameIndex];

        vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX);

        uint32_t imgIndex = 0;
        VkResult acqRes = wsi.acquireNextImage(vk.device, acqSem, VK_NULL_HANDLE, &imgIndex);
        if (acqRes == VK_ERROR_OUT_OF_DATE_KHR)
        {
            vkDeviceWaitIdle(vk.device);
            int32_t curW = 0, curH = 0;
            surface.size(&curW, &curH);
            wsi.initOrRecreateSwapchain(vk.device, vk.physicalDevice, static_cast<uint32_t>(curW),
                                        static_cast<uint32_t>(curH));
            CreateFramebuffers(vk, wsi);
            ImGui_ImplAndroidNative_SetWindow(surface.handle());
            continue;
        }
        if (acqRes != VK_SUCCESS && acqRes != VK_SUBOPTIMAL_KHR)
        {
            LOGE("acquireNextImage failed: %d", acqRes);
            break;
        }

        vkResetFences(vk.device, 1, &fence);
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkClearValue clearValue = {
            {{state.clearColor[0], state.clearColor[1], state.clearColor[2], state.clearColor[3]}}};
        VkRenderPassBeginInfo rpInfo = {};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = vk.renderPass;
        rpInfo.framebuffer = vk.framebuffers[imgIndex];
        rpInfo.renderArea.extent = wsi.getExtent();
        rpInfo.clearValueCount = 1;
        rpInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &acqSem;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &rendSem;

        vkQueueSubmit(vk.graphicsQueue, 1, &submitInfo, fence);

        VkResult presRes = wsi.present(vk.graphicsQueue, imgIndex, rendSem);
        if (presRes == VK_ERROR_OUT_OF_DATE_KHR || presRes == VK_SUBOPTIMAL_KHR || acqRes == VK_SUBOPTIMAL_KHR)
        {
            vkDeviceWaitIdle(vk.device);
            int32_t curW = 0, curH = 0;
            surface.size(&curW, &curH);
            wsi.initOrRecreateSwapchain(vk.device, vk.physicalDevice, static_cast<uint32_t>(curW),
                                        static_cast<uint32_t>(curH));
            CreateFramebuffers(vk, wsi);
            ImGui_ImplAndroidNative_SetWindow(surface.handle());
        }

        frameIndex = (frameIndex + 1) % kFramesInFlight;
    }

    vkDeviceWaitIdle(vk.device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplAndroidNative_Shutdown();
    ImGui::DestroyContext();

    CleanupVulkan(vk, wsi);
    surface.release();

    LOGI("androidNativeSurfaceImgui finished cleanly");
    std::exit(0);
    return 0;
}

extern "C" JNIEXPORT void JNICALL Java_com_nativesurface_app_Entry_nativeMain(JNIEnv* env, jclass clazz,jobjectArray jargs)
{
    (void)clazz;

    std::vector<std::string> args;
    if (jargs != nullptr)
    {
        const jsize count = env->GetArrayLength(jargs);
        for (jsize i = 0; i < count; ++i)
        {
            jstring js = static_cast<jstring>(env->GetObjectArrayElement(jargs, i));
            if (js == nullptr)
            {
                continue;
            }
            const char* utf = env->GetStringUTFChars(js, nullptr);
            if (utf != nullptr)
            {
                args.emplace_back(utf);
                env->ReleaseStringUTFChars(js, utf);
            }
            env->DeleteLocalRef(js);
        }
    }

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (std::string& arg : args)
    {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    run(static_cast<int>(args.size()), argv.data());
}
