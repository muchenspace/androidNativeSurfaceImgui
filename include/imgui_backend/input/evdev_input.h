#ifndef NS_INPUT_EVDEV_INPUT_H
#define NS_INPUT_EVDEV_INPUT_H

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "touch_event.h"

namespace ns
{

struct TouchPoint
{
    float rawX = 0.0f;
    float rawY = 0.0f;
    float normX = 0.0f;
    float normY = 0.0f;
    bool isDown = false;
};

class EvdevInput
{
   public:
    using EventCallback = std::function<void(const TouchEvent&)>;

    static constexpr size_t kMaxPendingEvents = 256;

    EvdevInput();
    ~EvdevInput();

    EvdevInput(const EvdevInput&) = delete;
    EvdevInput& operator=(const EvdevInput&) = delete;

    bool start(const std::string& devicePath = "");

    void stop();

    bool isRunning() const
    {
        return mRunning.load(std::memory_order_acquire);
    }

    std::string getDevicePath() const;
    std::string getDeviceName() const;
    uint64_t getEventCount() const;

    void pollEvents(std::vector<TouchEvent>& out);

    void setCallback(EventCallback cb);

    TouchPoint getLatestTouch() const;

   private:
    std::string findTouchscreenDevice();
    bool openDevice(const std::string& path);
    void readLoop();

    void publish(const TouchEvent& event);

    float normalizeX(float raw) const;
    float normalizeY(float raw) const;

    int mFd = -1;
    std::string mDevicePath;
    std::string mDeviceName;

    bool mHasSlotProtocol = false;
    int32_t mSlotCount = 1;
    int32_t mMinX = 0;
    int32_t mMaxX = 0;
    int32_t mMinY = 0;
    int32_t mMaxY = 0;

    std::thread mThread;
    std::atomic<bool> mRunning{false};

    mutable std::mutex mQueueMutex;
    std::deque<TouchEvent> mPending;
    EventCallback mCallback;

    std::atomic<float> mLastRawX{0.0f};
    std::atomic<float> mLastRawY{0.0f};
    std::atomic<float> mLastNormX{0.0f};
    std::atomic<float> mLastNormY{0.0f};
    std::atomic<bool> mLastDown{false};
    std::atomic<uint64_t> mEventCount{0};
};

}  // namespace ns

#endif
