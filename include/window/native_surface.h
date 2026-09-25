#ifndef NS_NATIVE_SURFACE_H
#define NS_NATIVE_SURFACE_H

#include <cstdint>

struct ANativeWindow;

namespace ns
{

class NativeSurface
{
   public:
    struct Impl;

    struct Options
    {
        const char* name = "NativeSurfaceOverlay";
        int width = 0;
        int height = 0;
        bool trustedOverlay = true;
        bool watchDisplay = true;
        int watchIntervalMs = 250;
    };

    NativeSurface();
    ~NativeSurface();

    NativeSurface(const NativeSurface&) = delete;
    NativeSurface& operator=(const NativeSurface&) = delete;

    bool create();
    bool create(const Options& options);

    bool attach(ANativeWindow* window);

    ANativeWindow* handle() const;

    bool isValid() const;

    void size(int32_t* outWidth, int32_t* outHeight) const;

    bool checkSizeChanged(int32_t* outWidth = nullptr, int32_t* outHeight = nullptr);

    int orientation() const;

    void release();

   private:
    Impl* mImpl;
};

namespace detail
{

void SurfaceSetJavaVM(void* javaVm);
void SurfaceNotifyGeometry(int64_t handle, int width, int height, int rotation);

}  // namespace detail

}  // namespace ns

#endif
