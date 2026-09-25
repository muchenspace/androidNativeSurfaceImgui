#include "window/native_surface.h"

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <atomic>
#include <mutex>
#include <unordered_set>

#include "log.h"

namespace ns
{

namespace
{

constexpr const char* kSurfaceServiceClass = "com/nativesurface/surface/SurfaceService";

std::atomic<JavaVM*> gJavaVM{nullptr};

class ScopedEnv
{
   public:
    ScopedEnv()
    {
        JavaVM* vm = gJavaVM.load(std::memory_order_acquire);
        if (vm == nullptr)
        {
            return;
        }
        void* env = nullptr;
        const jint status = vm->GetEnv(&env, JNI_VERSION_1_6);
        if (status == JNI_OK)
        {
            mEnv = static_cast<JNIEnv*>(env);
            return;
        }
        if (status == JNI_EDETACHED && vm->AttachCurrentThread(&mEnv, nullptr) == JNI_OK)
        {
            mAttached = true;
        }
    }

    ~ScopedEnv()
    {
        if (mAttached)
        {
            JavaVM* vm = gJavaVM.load(std::memory_order_acquire);
            if (vm != nullptr)
            {
                vm->DetachCurrentThread();
            }
        }
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

    JNIEnv* env() const
    {
        return mEnv;
    }
    bool ok() const
    {
        return mEnv != nullptr;
    }

   private:
    JNIEnv* mEnv = nullptr;
    bool mAttached = false;
};

bool ResolveStatic(JNIEnv* env, const char* name, const char* signature, jclass* outClass, jmethodID* outMethod)
{
    jclass cls = env->FindClass(kSurfaceServiceClass);
    if (cls == nullptr)
    {
        env->ExceptionClear();
        NS_LOGE("NativeSurface: cannot find class %s", kSurfaceServiceClass);
        return false;
    }
    jmethodID method = env->GetStaticMethodID(cls, name, signature);
    if (method == nullptr)
    {
        env->ExceptionClear();
        NS_LOGE("NativeSurface: cannot find static method %s%s", name, signature);
        env->DeleteLocalRef(cls);
        return false;
    }
    *outClass = cls;
    *outMethod = method;
    return true;
}

}

struct NativeSurface::Impl
{
    int64_t handle = 0;
    ANativeWindow* window = nullptr;

    bool ownsWindow = false;
    bool hasJavaLayer = false;

    std::atomic<int32_t> width{0};
    std::atomic<int32_t> height{0};
    std::atomic<int> orientation{0};

    std::atomic<int32_t> pendingWidth{0};
    std::atomic<int32_t> pendingHeight{0};
    std::atomic<int> pendingOrientation{-1};

    std::mutex apiMutex;

    Impl();
    ~Impl();
};

namespace
{

std::mutex gRegistryMutex;
std::unordered_set<NativeSurface::Impl*> gActiveImpls;

void RegisterImpl(NativeSurface::Impl* impl)
{
    std::lock_guard<std::mutex> lock(gRegistryMutex);
    gActiveImpls.insert(impl);
}

void UnregisterImpl(NativeSurface::Impl* impl)
{
    std::lock_guard<std::mutex> lock(gRegistryMutex);
    gActiveImpls.erase(impl);
}

bool IsValidImpl(NativeSurface::Impl* impl)
{
    std::lock_guard<std::mutex> lock(gRegistryMutex);
    return gActiveImpls.find(impl) != gActiveImpls.end();
}

}

NativeSurface::Impl::Impl() : handle(reinterpret_cast<int64_t>(this))
{
    RegisterImpl(this);
}

NativeSurface::Impl::~Impl()
{
    UnregisterImpl(this);
}

namespace detail
{

void SurfaceSetJavaVM(void* javaVm)
{
    gJavaVM.store(static_cast<JavaVM*>(javaVm), std::memory_order_release);
}

void SurfaceNotifyGeometry(int64_t handle, int width, int height, int rotation)
{
    auto* impl = reinterpret_cast<NativeSurface::Impl*>(handle);
    if (impl == nullptr || !IsValidImpl(impl))
    {
        return;
    }
    impl->pendingWidth.store(width, std::memory_order_release);
    impl->pendingHeight.store(height, std::memory_order_release);
    if (rotation >= 0)
    {
        impl->pendingOrientation.store(rotation, std::memory_order_release);
    }
}

}

NativeSurface::NativeSurface() : mImpl(new Impl())
{
}

NativeSurface::~NativeSurface()
{
    release();
    delete mImpl;
}

bool NativeSurface::create()
{
    return create(Options());
}

bool NativeSurface::create(const Options& options)
{
    std::lock_guard<std::mutex> lock(mImpl->apiMutex);

    if (mImpl->window != nullptr)
    {
        NS_LOGW("NativeSurface: already created, release() first");
        return false;
    }

    ScopedEnv scoped;
    if (!scoped.ok())
    {
        NS_LOGE("NativeSurface: no JavaVM available; JNI_OnLoad never ran?");
        return false;
    }
    JNIEnv* env = scoped.env();

    jclass serviceClass = nullptr;
    jmethodID createMethod = nullptr;
    if (!ResolveStatic(env, "createSurface", "(Ljava/lang/String;IIZIJ)Landroid/view/Surface;", &serviceClass,
                       &createMethod))
    {
        return false;
    }

    const jint watchInterval = options.watchDisplay ? static_cast<jint>(options.watchIntervalMs) : 0;

    jstring jname = env->NewStringUTF(options.name != nullptr ? options.name : "NativeSurfaceOverlay");
    jobject surface = env->CallStaticObjectMethod(
        serviceClass, createMethod, jname, static_cast<jint>(options.width), static_cast<jint>(options.height),
        options.trustedOverlay ? JNI_TRUE : JNI_FALSE, watchInterval, static_cast<jlong>(mImpl->handle));
    env->DeleteLocalRef(jname);
    env->DeleteLocalRef(serviceClass);

    if (env->ExceptionCheck())
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        NS_LOGE("NativeSurface: SurfaceService.createSurface threw");
        return false;
    }
    if (surface == nullptr)
    {
        NS_LOGE("NativeSurface: SurfaceService.createSurface returned null");
        return false;
    }

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    env->DeleteLocalRef(surface);
    if (window == nullptr)
    {
        NS_LOGE("NativeSurface: ANativeWindow_fromSurface failed");
        return false;
    }

    mImpl->window = window;
    mImpl->ownsWindow = true;
    mImpl->hasJavaLayer = true;
    const int32_t w = ANativeWindow_getWidth(window);
    const int32_t h = ANativeWindow_getHeight(window);
    mImpl->width.store(w > 0 ? w : 0, std::memory_order_release);
    mImpl->height.store(h > 0 ? h : 0, std::memory_order_release);

    if (w > 0 && h > 0)
    {
        ANativeWindow_setBuffersGeometry(window, w, h, WINDOW_FORMAT_RGBA_8888);
    }

    NS_LOGI("NativeSurface: created '%s' %dx%d", options.name, w, h);
    return true;
}

bool NativeSurface::attach(ANativeWindow* window)
{
    std::lock_guard<std::mutex> lock(mImpl->apiMutex);

    if (window == nullptr)
    {
        NS_LOGE("NativeSurface::attach: null window");
        return false;
    }
    if (mImpl->window != nullptr)
    {
        NS_LOGW("NativeSurface::attach: already holds a window, release() first");
        return false;
    }

    mImpl->window = window;
    mImpl->ownsWindow = true;
    const int32_t w = ANativeWindow_getWidth(window);
    const int32_t h = ANativeWindow_getHeight(window);
    mImpl->width.store(w > 0 ? w : 0, std::memory_order_release);
    mImpl->height.store(h > 0 ? h : 0, std::memory_order_release);

    NS_LOGI("NativeSurface: attached external window %p (%dx%d)", window, w, h);
    return true;
}

void NativeSurface::release()
{
    std::lock_guard<std::mutex> lock(mImpl->apiMutex);

    if (!mImpl->ownsWindow && !mImpl->hasJavaLayer)
    {
        return;
    }

    if (mImpl->hasJavaLayer)
    {
        mImpl->hasJavaLayer = false;
        ScopedEnv scoped;
        if (scoped.ok())
        {
            JNIEnv* env = scoped.env();
            jclass serviceClass = nullptr;
            jmethodID destroyMethod = nullptr;
            if (ResolveStatic(env, "destroySurface", "(J)V", &serviceClass, &destroyMethod))
            {
                env->CallStaticVoidMethod(serviceClass, destroyMethod, static_cast<jlong>(mImpl->handle));
                if (env->ExceptionCheck())
                {
                    env->ExceptionDescribe();
                    env->ExceptionClear();
                    NS_LOGW("NativeSurface: SurfaceService.destroySurface threw (ignored)");
                }
                env->DeleteLocalRef(serviceClass);
            }
        }
    }

    if (mImpl->window != nullptr)
    {
        NS_LOGI("NativeSurface: releasing ANativeWindow (%p)", mImpl->window);
        ANativeWindow_release(mImpl->window);
        mImpl->window = nullptr;
    }
    mImpl->ownsWindow = false;

    mImpl->width.store(0, std::memory_order_release);
    mImpl->height.store(0, std::memory_order_release);
    mImpl->pendingWidth.store(0, std::memory_order_release);
    mImpl->pendingHeight.store(0, std::memory_order_release);
    mImpl->pendingOrientation.store(-1, std::memory_order_release);
}

ANativeWindow* NativeSurface::handle() const
{
    return mImpl->window;
}

bool NativeSurface::isValid() const
{
    return mImpl->window != nullptr;
}

void NativeSurface::size(int32_t* outWidth, int32_t* outHeight) const
{
    if (mImpl->window != nullptr)
    {
        const int32_t w = ANativeWindow_getWidth(mImpl->window);
        const int32_t h = ANativeWindow_getHeight(mImpl->window);
        if (w > 0 && h > 0)
        {
            mImpl->width.store(w, std::memory_order_release);
            mImpl->height.store(h, std::memory_order_release);
        }
    }
    if (outWidth)
        *outWidth = mImpl->width.load(std::memory_order_acquire);
    if (outHeight)
        *outHeight = mImpl->height.load(std::memory_order_acquire);
}

bool NativeSurface::checkSizeChanged(int32_t* outWidth, int32_t* outHeight)
{
    const int32_t pendingW = mImpl->pendingWidth.exchange(0, std::memory_order_acq_rel);
    const int32_t pendingH = mImpl->pendingHeight.exchange(0, std::memory_order_acq_rel);
    const int pendingOri = mImpl->pendingOrientation.exchange(-1, std::memory_order_acq_rel);

    if (pendingOri >= 0)
    {
        int degrees = pendingOri;
        if (degrees == 1)
            degrees = 90;
        else if (degrees == 2)
            degrees = 180;
        else if (degrees == 3)
            degrees = 270;
        if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270)
        {
            degrees = 0;
        }
        mImpl->orientation.store(degrees, std::memory_order_release);
    }

    const bool changed = (pendingW > 0 && pendingH > 0) && (pendingW != mImpl->width.load(std::memory_order_acquire) ||
                                                            pendingH != mImpl->height.load(std::memory_order_acquire));

    if (changed)
    {
        mImpl->width.store(pendingW, std::memory_order_release);
        mImpl->height.store(pendingH, std::memory_order_release);

        if (mImpl->window != nullptr)
        {
            ANativeWindow_setBuffersGeometry(mImpl->window, pendingW, pendingH, WINDOW_FORMAT_RGBA_8888);
        }
    }

    if (outWidth)
        *outWidth = mImpl->width.load(std::memory_order_acquire);
    if (outHeight)
        *outHeight = mImpl->height.load(std::memory_order_acquire);
    return changed;
}

int NativeSurface::orientation() const
{
    return mImpl->orientation.load(std::memory_order_acquire);
}

}  // namespace ns
