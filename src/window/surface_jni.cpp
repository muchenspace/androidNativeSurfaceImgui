#include <jni.h>

#include "log.h"
#include "window/native_surface.h"

extern "C"
{
    JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
    {
        (void)reserved;
        ns::detail::SurfaceSetJavaVM(vm);
        NS_LOGI("NativeSurface: JavaVM cached in JNI_OnLoad");
        return JNI_VERSION_1_6;
    }

    JNIEXPORT void JNICALL Java_com_nativesurface_surface_SurfaceService_nativeOnGeometryChanged(
        JNIEnv* env, jclass clazz, jlong handle, jint width, jint height, jint rotation)
    {
        (void)env;
        (void)clazz;
        ns::detail::SurfaceNotifyGeometry(static_cast<int64_t>(handle), width, height, rotation);
    }
}
