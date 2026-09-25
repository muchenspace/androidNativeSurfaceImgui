#ifndef IMGUI_IMPL_ANDROID_NATIVE_H
#define IMGUI_IMPL_ANDROID_NATIVE_H

#include "imgui.h"
#include "input/touch_event.h"

struct ANativeWindow;

IMGUI_IMPL_API bool ImGui_ImplAndroidNative_Init(ANativeWindow* window);
IMGUI_IMPL_API void ImGui_ImplAndroidNative_Shutdown();
IMGUI_IMPL_API void ImGui_ImplAndroidNative_NewFrame();

IMGUI_IMPL_API void ImGui_ImplAndroidNative_SetWindow(ANativeWindow* window);
IMGUI_IMPL_API ANativeWindow* ImGui_ImplAndroidNative_GetWindow();

IMGUI_IMPL_API void ImGui_ImplAndroidNative_SetDisplaySize(int width, int height);
IMGUI_IMPL_API void ImGui_ImplAndroidNative_GetDisplaySize(int* outWidth, int* outHeight);

IMGUI_IMPL_API void ImGui_ImplAndroidNative_SetOrientation(int orientation);

IMGUI_IMPL_API int ImGui_ImplAndroidNative_GetOrientation();

IMGUI_IMPL_API void ImGui_ImplAndroidNative_SetTouchMappingMode(int mode);
IMGUI_IMPL_API int ImGui_ImplAndroidNative_GetTouchMappingMode();

IMGUI_IMPL_API int ImGui_ImplAndroidNative_GetEffectiveTouchMode();

IMGUI_IMPL_API void ImGui_ImplAndroidNative_SetTouchTransform(bool swapXY, bool invertX, bool invertY);

IMGUI_IMPL_API void ImGui_ImplAndroidNative_GetLastTouchInfo(float* rawX, float* rawY, float* normX, float* normY,
                                                             float* mappedX, float* mappedY, bool* isDown);

IMGUI_IMPL_API bool ImGui_ImplAndroidNative_StartTouchListener(const char* devicePath = nullptr);
IMGUI_IMPL_API void ImGui_ImplAndroidNative_StopTouchListener();
IMGUI_IMPL_API bool ImGui_ImplAndroidNative_IsTouchListenerRunning();

IMGUI_IMPL_API void ImGui_ImplAndroidNative_PushTouchEvent(const ns::TouchEvent& event);

IMGUI_IMPL_API const char* ImGui_ImplAndroidNative_GetTouchDevicePath();
IMGUI_IMPL_API const char* ImGui_ImplAndroidNative_GetTouchDeviceName();
IMGUI_IMPL_API uint64_t ImGui_ImplAndroidNative_GetTouchEventCount();

#endif
