#include "imgui_backend/imgui_impl_android_native.h"

#include <android/native_window.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

#include "imgui_backend/input/evdev_input.h"
#include "log.h"

namespace
{

struct ImGui_ImplAndroidNative_Data
{
    ANativeWindow* Window = nullptr;

    std::unique_ptr<ns::EvdevInput> TouchListener;
    std::vector<ns::TouchEvent> TouchEvents;
    std::vector<ns::TouchEvent> InjectedEvents;
    std::mutex InjectMutex;

    std::chrono::steady_clock::time_point LastTime{};
    bool HasTime = false;

    int Orientation = 0;
    int CustomDisplayWidth = 0;
    int CustomDisplayHeight = 0;
    float LastDisplayWidth = 0.0f;
    float LastDisplayHeight = 0.0f;
    bool WarnedNoDisplaySize = false;

    int ForceTouchMode = -1;
    bool CustomTransform = false;
    bool InvertX = false;
    bool InvertY = false;
    bool SwapXY = false;

    int32_t ActivePointerId = -1;
    bool PointerDown = false;

    int LastEffectiveMode = 0;
    float LastRawX = 0.0f;
    float LastRawY = 0.0f;
    float LastNormX = 0.0f;
    float LastNormY = 0.0f;
    float LastMappedX = 0.0f;
    float LastMappedY = 0.0f;
    bool LastTouchDown = false;
};

ImGui_ImplAndroidNative_Data* ImGui_ImplAndroidNative_GetBackendData()
{
    return ImGui::GetCurrentContext()
               ? static_cast<ImGui_ImplAndroidNative_Data*>(ImGui::GetIO().BackendPlatformUserData)
               : nullptr;
}

int ImGui_ImplAndroidNative_OrientationToTouchMode(int degrees)
{
    switch (degrees)
    {
        case 90:
            return 1;
        case 180:
            return 2;
        case 270:
            return 3;
        default:
            return 0;
    }
}

void ImGui_ImplAndroidNative_UpdateDisplaySize(ImGui_ImplAndroidNative_Data* bd)
{
    ImGuiIO& io = ImGui::GetIO();

    float width = 0.0f;
    float height = 0.0f;

    if (bd->CustomDisplayWidth > 0 && bd->CustomDisplayHeight > 0)
    {
        width = static_cast<float>(bd->CustomDisplayWidth);
        height = static_cast<float>(bd->CustomDisplayHeight);
    }
    else if (bd->Window != nullptr)
    {
        const int32_t windowWidth = ANativeWindow_getWidth(bd->Window);
        const int32_t windowHeight = ANativeWindow_getHeight(bd->Window);
        if (windowWidth > 0 && windowHeight > 0)
        {
            width = static_cast<float>(windowWidth);
            height = static_cast<float>(windowHeight);
        }
    }

    if (width <= 0.0f || height <= 0.0f)
    {
        width = bd->LastDisplayWidth;
        height = bd->LastDisplayHeight;
    }

    if (width <= 0.0f || height <= 0.0f)
    {
        if (!bd->WarnedNoDisplaySize)
        {
            NS_LOGW(
                "ImGui_ImplAndroidNative: display size unknown, frames stay empty until "
                "SetWindow()/SetDisplaySize() provides a valid geometry");
            bd->WarnedNoDisplaySize = true;
        }
        width = 0.0f;
        height = 0.0f;
    }
    else
    {
        bd->WarnedNoDisplaySize = false;
    }

    bd->LastDisplayWidth = width;
    bd->LastDisplayHeight = height;
    io.DisplaySize = ImVec2(width, height);
}

void ImGui_ImplAndroidNative_UpdateDeltaTime(ImGui_ImplAndroidNative_Data* bd)
{
    ImGuiIO& io = ImGui::GetIO();
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    if (!bd->HasTime)
    {
        bd->HasTime = true;
        bd->LastTime = now;
        io.DeltaTime = 1.0f / 60.0f;
        return;
    }

    int64_t elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(now - bd->LastTime).count();
    bd->LastTime = now;
    if (elapsedNs <= 0)
    {
        elapsedNs = 1;
    }

    float deltaTime = static_cast<float>(static_cast<double>(elapsedNs) * 1e-9);
    deltaTime = std::min(deltaTime, 0.25f);
    io.DeltaTime = deltaTime;
}

void ImGui_ImplAndroidNative_MapTouch(ImGui_ImplAndroidNative_Data* bd, float normX, float normY, float displayWidth,
                                      float displayHeight, float* outX, float* outY)
{
    normX = std::clamp(normX, 0.0f, 1.0f);
    normY = std::clamp(normY, 0.0f, 1.0f);

    if (bd->CustomTransform)
    {
        bd->LastEffectiveMode = -2;
        const float x = bd->InvertX ? (1.0f - normX) : normX;
        const float y = bd->InvertY ? (1.0f - normY) : normY;
        if (bd->SwapXY)
        {
            *outX = y * displayWidth;
            *outY = x * displayHeight;
        }
        else
        {
            *outX = x * displayWidth;
            *outY = y * displayHeight;
        }
        return;
    }

    int mode = bd->ForceTouchMode;
    if (mode < 0)
    {
        mode = ImGui_ImplAndroidNative_OrientationToTouchMode(bd->Orientation);
    }
    bd->LastEffectiveMode = mode;

    switch (mode)
    {
        case 1:
            *outX = normY * displayWidth;
            *outY = (1.0f - normX) * displayHeight;
            break;
        case 2:
            *outX = (1.0f - normX) * displayWidth;
            *outY = (1.0f - normY) * displayHeight;
            break;
        case 3:
            *outX = (1.0f - normY) * displayWidth;
            *outY = normX * displayHeight;
            break;
        case 0:
        default:
            *outX = normX * displayWidth;
            *outY = normY * displayHeight;
            break;
    }
}

void ImGui_ImplAndroidNative_CollectEvents(ImGui_ImplAndroidNative_Data* bd)
{
    bd->TouchEvents.clear();

    if (bd->TouchListener && bd->TouchListener->isRunning())
    {
        bd->TouchListener->pollEvents(bd->TouchEvents);
    }

    std::lock_guard<std::mutex> lock(bd->InjectMutex);
    if (!bd->InjectedEvents.empty())
    {
        bd->TouchEvents.insert(bd->TouchEvents.end(), bd->InjectedEvents.begin(), bd->InjectedEvents.end());
        bd->InjectedEvents.clear();
    }
}

void ImGui_ImplAndroidNative_UpdateInput(ImGui_ImplAndroidNative_Data* bd)
{
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplAndroidNative_CollectEvents(bd);
    if (bd->TouchEvents.empty())
    {
        return;
    }

    const float displayWidth = io.DisplaySize.x;
    const float displayHeight = io.DisplaySize.y;

    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);

    for (const ns::TouchEvent& event : bd->TouchEvents)
    {
        switch (event.phase)
        {
            case ns::TouchEvent::Phase_Down:
                if (bd->PointerDown)
                {
                    continue;
                }
                bd->PointerDown = true;
                bd->ActivePointerId = event.id;
                break;
            case ns::TouchEvent::Phase_Up:
            case ns::TouchEvent::Phase_Cancel:
                if (!bd->PointerDown || event.id != bd->ActivePointerId)
                {
                    continue;
                }
                break;
            case ns::TouchEvent::Phase_Move:
            default:
                if (!bd->PointerDown || event.id != bd->ActivePointerId)
                {
                    continue;
                }
                break;
        }

        float mappedX = 0.0f;
        float mappedY = 0.0f;
        ImGui_ImplAndroidNative_MapTouch(bd, event.normX, event.normY, displayWidth, displayHeight, &mappedX, &mappedY);

        bd->LastRawX = event.rawX;
        bd->LastRawY = event.rawY;
        bd->LastNormX = event.normX;
        bd->LastNormY = event.normY;
        bd->LastMappedX = mappedX;
        bd->LastMappedY = mappedY;

        io.AddMousePosEvent(mappedX, mappedY);

        if (event.phase == ns::TouchEvent::Phase_Down)
        {
            io.AddMouseButtonEvent(0, true);
            bd->LastTouchDown = true;
            NS_LOGI("Touch DOWN at screen(%.1f, %.1f) [raw=(%.1f, %.1f), norm=(%.3f, %.3f), ori=%d, mode=%d]", mappedX,
                    mappedY, event.rawX, event.rawY, event.normX, event.normY, bd->Orientation, bd->LastEffectiveMode);
        }
        else if (event.phase == ns::TouchEvent::Phase_Up || event.phase == ns::TouchEvent::Phase_Cancel)
        {
            io.AddMouseButtonEvent(0, false);
            bd->LastTouchDown = false;
            bd->PointerDown = false;
            bd->ActivePointerId = -1;
            NS_LOGI("Touch %s at screen(%.1f, %.1f)", event.phase == ns::TouchEvent::Phase_Up ? "UP" : "CANCEL",
                    mappedX, mappedY);
        }
    }
}

}  // namespace

bool ImGui_ImplAndroidNative_Init(ANativeWindow* window)
{
    IMGUI_CHECKVERSION();
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendPlatformUserData == nullptr && "Already initialized a platform backend!");

    ImGui_ImplAndroidNative_Data* bd = IM_NEW(ImGui_ImplAndroidNative_Data)();
    io.BackendPlatformUserData = static_cast<void*>(bd);
    io.BackendPlatformName = "imgui_impl_android_native";

    bd->Window = window;
    bd->TouchListener = std::make_unique<ns::EvdevInput>();
    if (!bd->TouchListener->start())
    {
        NS_LOGW(
            "ImGui_ImplAndroidNative: built-in /dev/input source unavailable; "
            "use ImGui_ImplAndroidNative_PushTouchEvent() to supply input");
    }

    NS_LOGI("ImGui_ImplAndroidNative: platform backend initialized (render-API-agnostic)");
    return true;
}

void ImGui_ImplAndroidNative_Shutdown()
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    IM_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();

    if (bd->TouchListener)
    {
        bd->TouchListener->stop();
        bd->TouchListener.reset();
    }

    io.BackendPlatformName = nullptr;
    io.BackendPlatformUserData = nullptr;
    IM_DELETE(bd);

    NS_LOGI("ImGui_ImplAndroidNative: shutdown complete");
}

void ImGui_ImplAndroidNative_NewFrame()
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplAndroidNative_Init()?");

    ImGui_ImplAndroidNative_UpdateDisplaySize(bd);
    ImGui_ImplAndroidNative_UpdateDeltaTime(bd);
    ImGui_ImplAndroidNative_UpdateInput(bd);
}

void ImGui_ImplAndroidNative_SetWindow(ANativeWindow* window)
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    if (bd != nullptr)
    {
        bd->Window = window;
    }
}

ANativeWindow* ImGui_ImplAndroidNative_GetWindow()
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    return bd != nullptr ? bd->Window : nullptr;
}

void ImGui_ImplAndroidNative_SetDisplaySize(int width, int height)
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    if (bd == nullptr)
    {
        return;
    }
    bd->CustomDisplayWidth = (width > 0 && height > 0) ? width : 0;
    bd->CustomDisplayHeight = (width > 0 && height > 0) ? height : 0;
}

void ImGui_ImplAndroidNative_GetDisplaySize(int* outWidth, int* outHeight)
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    if (bd == nullptr)
    {
        if (outWidth)
            *outWidth = 0;
        if (outHeight)
            *outHeight = 0;
        return;
    }
    if (outWidth)
        *outWidth = static_cast<int>(bd->LastDisplayWidth);
    if (outHeight)
        *outHeight = static_cast<int>(bd->LastDisplayHeight);
}

void ImGui_ImplAndroidNative_SetOrientation(int orientation)
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    if (bd == nullptr)
    {
        return;
    }

    int degrees = orientation;
    if (degrees == 1)
    {
        degrees = 90;
    }
    else if (degrees == 2)
    {
        degrees = 180;
    }
    else if (degrees == 3)
    {
        degrees = 270;
    }
    if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270)
    {
        degrees = 0;
    }

    if (degrees == bd->Orientation)
    {
        return;
    }
    bd->Orientation = degrees;
    NS_LOGI("ImGui_ImplAndroidNative: orientation set to %d deg (input=%d)", degrees, orientation);
}

int ImGui_ImplAndroidNative_GetOrientation()
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    return bd != nullptr ? bd->Orientation : 0;
}

void ImGui_ImplAndroidNative_SetTouchMappingMode(int mode)
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    if (bd == nullptr)
    {
        return;
    }
    bd->ForceTouchMode = (mode < 0) ? -1 : std::min(mode, 3);
    NS_LOGI("ImGui_ImplAndroidNative: touch mapping mode set to %d", bd->ForceTouchMode);
}

int ImGui_ImplAndroidNative_GetTouchMappingMode()
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    return bd != nullptr ? bd->ForceTouchMode : -1;
}

int ImGui_ImplAndroidNative_GetEffectiveTouchMode()
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    return bd != nullptr ? bd->LastEffectiveMode : 0;
}

void ImGui_ImplAndroidNative_SetTouchTransform(bool swapXY, bool invertX, bool invertY)
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    if (bd == nullptr)
    {
        return;
    }
    bd->CustomTransform = true;
    bd->SwapXY = swapXY;
    bd->InvertX = invertX;
    bd->InvertY = invertY;
    NS_LOGI("ImGui_ImplAndroidNative: custom touch transform set (swapXY=%d, invX=%d, invY=%d)", swapXY, invertX,
            invertY);
}

void ImGui_ImplAndroidNative_GetLastTouchInfo(float* rawX, float* rawY, float* normX, float* normY, float* mappedX,
                                              float* mappedY, bool* isDown)
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    if (bd == nullptr)
    {
        return;
    }
    if (rawX)
        *rawX = bd->LastRawX;
    if (rawY)
        *rawY = bd->LastRawY;
    if (normX)
        *normX = bd->LastNormX;
    if (normY)
        *normY = bd->LastNormY;
    if (mappedX)
        *mappedX = bd->LastMappedX;
    if (mappedY)
        *mappedY = bd->LastMappedY;
    if (isDown)
        *isDown = bd->LastTouchDown;
}

bool ImGui_ImplAndroidNative_StartTouchListener(const char* devicePath)
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    if (bd == nullptr || !bd->TouchListener)
    {
        return false;
    }
    return bd->TouchListener->start(devicePath != nullptr ? devicePath : "");
}

void ImGui_ImplAndroidNative_StopTouchListener()
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    if (bd != nullptr && bd->TouchListener)
    {
        bd->TouchListener->stop();
    }
}

bool ImGui_ImplAndroidNative_IsTouchListenerRunning()
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    return bd != nullptr && bd->TouchListener && bd->TouchListener->isRunning();
}

void ImGui_ImplAndroidNative_PushTouchEvent(const ns::TouchEvent& event)
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    if (bd == nullptr)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(bd->InjectMutex);
    bd->InjectedEvents.push_back(event);
}

const char* ImGui_ImplAndroidNative_GetTouchDevicePath()
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    if (bd != nullptr && bd->TouchListener)
    {
        static std::string sPath;
        sPath = bd->TouchListener->getDevicePath();
        return sPath.c_str();
    }
    return "";
}

const char* ImGui_ImplAndroidNative_GetTouchDeviceName()
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    if (bd != nullptr && bd->TouchListener)
    {
        static std::string sName;
        sName = bd->TouchListener->getDeviceName();
        return sName.c_str();
    }
    return "";
}

uint64_t ImGui_ImplAndroidNative_GetTouchEventCount()
{
    ImGui_ImplAndroidNative_Data* bd = ImGui_ImplAndroidNative_GetBackendData();
    if (bd != nullptr && bd->TouchListener)
    {
        return bd->TouchListener->getEventCount();
    }
    return 0;
}
