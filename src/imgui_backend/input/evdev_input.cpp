#include "imgui_backend/input/evdev_input.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>

#include "../log.h"

namespace ns
{
namespace
{

constexpr size_t kBitsPerLong = sizeof(unsigned long) * 8;

constexpr size_t NumBits(size_t count)
{
    return ((count - 1) / kBitsPerLong) + 1;
}

inline bool TestBit(const unsigned long* arr, size_t bit)
{
    return ((arr[bit / kBitsPerLong] >> (bit % kBitsPerLong)) & 1) != 0;
}

struct SlotState
{
    bool present = false;
    bool downSent = false;
    float rawX = 0.0f;
    float rawY = 0.0f;
    bool hasPos = false;
    bool moved = false;
};

uint64_t monotonicMicros()
{
    struct timespec ts
    {
    };
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + static_cast<uint64_t>(ts.tv_nsec) / 1000ULL;
}

}  // namespace

EvdevInput::EvdevInput() = default;

EvdevInput::~EvdevInput()
{
    stop();
}

bool EvdevInput::start(const std::string& devicePath)
{
    if (mRunning.load(std::memory_order_acquire))
    {
        return true;
    }

    std::string path = devicePath;
    if (path.empty())
    {
        path = findTouchscreenDevice();
    }

    if (path.empty() || !openDevice(path))
    {
        NS_LOGW("EvdevInput: no usable touchscreen device (%s), input listening disabled", path.c_str());
        return false;
    }

    mRunning.store(true, std::memory_order_release);
    mThread = std::thread(&EvdevInput::readLoop, this);
    NS_LOGI("EvdevInput: listener started on %s (%s), MT protocol %s, slots=%d", mDeviceName.c_str(),
            mDevicePath.c_str(), mHasSlotProtocol ? "B" : "A", mSlotCount);
    return true;
}

void EvdevInput::stop()
{
    if (!mRunning.exchange(false, std::memory_order_acq_rel))
    {
        return;
    }

    if (mFd >= 0)
    {
        ::close(mFd);
        mFd = -1;
    }
    if (mThread.joinable())
    {
        mThread.join();
    }
    NS_LOGI("EvdevInput: listener stopped");
}

void EvdevInput::pollEvents(std::vector<TouchEvent>& out)
{
    std::lock_guard<std::mutex> lock(mQueueMutex);
    if (mPending.empty())
    {
        return;
    }
    out.reserve(out.size() + mPending.size());
    out.insert(out.end(), mPending.begin(), mPending.end());
    mPending.clear();
}

void EvdevInput::setCallback(EventCallback cb)
{
    std::lock_guard<std::mutex> lock(mQueueMutex);
    mCallback = std::move(cb);
}

TouchPoint EvdevInput::getLatestTouch() const
{
    TouchPoint pt;
    pt.rawX = mLastRawX.load(std::memory_order_relaxed);
    pt.rawY = mLastRawY.load(std::memory_order_relaxed);
    pt.normX = mLastNormX.load(std::memory_order_relaxed);
    pt.normY = mLastNormY.load(std::memory_order_relaxed);
    pt.isDown = mLastDown.load(std::memory_order_relaxed);
    return pt;
}

std::string EvdevInput::getDevicePath() const
{
    std::lock_guard<std::mutex> lock(mQueueMutex);
    return mDevicePath;
}

std::string EvdevInput::getDeviceName() const
{
    std::lock_guard<std::mutex> lock(mQueueMutex);
    return mDeviceName;
}

uint64_t EvdevInput::getEventCount() const
{
    return mEventCount.load(std::memory_order_relaxed);
}

float EvdevInput::normalizeX(float raw) const
{
    const float span = static_cast<float>(mMaxX - mMinX);
    if (span <= 0.0f)
        return 0.0f;
    return std::min(1.0f, std::max(0.0f, (raw - static_cast<float>(mMinX)) / span));
}

float EvdevInput::normalizeY(float raw) const
{
    const float span = static_cast<float>(mMaxY - mMinY);
    if (span <= 0.0f)
        return 0.0f;
    return std::min(1.0f, std::max(0.0f, (raw - static_cast<float>(mMinY)) / span));
}

void EvdevInput::publish(const TouchEvent& event)
{
    mEventCount.fetch_add(1, std::memory_order_relaxed);
    mLastRawX.store(event.rawX, std::memory_order_relaxed);
    mLastRawY.store(event.rawY, std::memory_order_relaxed);
    mLastNormX.store(event.normX, std::memory_order_relaxed);
    mLastNormY.store(event.normY, std::memory_order_relaxed);
    if (event.phase == TouchEvent::Phase_Down)
    {
        mLastDown.store(true, std::memory_order_relaxed);
    }
    else if (event.phase == TouchEvent::Phase_Up || event.phase == TouchEvent::Phase_Cancel)
    {
        mLastDown.store(false, std::memory_order_relaxed);
    }

    EventCallback callback;
    {
        std::lock_guard<std::mutex> lock(mQueueMutex);
        if (mPending.size() >= kMaxPendingEvents)
        {
            mPending.pop_front();
        }
        mPending.push_back(event);
        callback = mCallback;
    }
    if (callback)
    {
        callback(event);
    }
}

std::string EvdevInput::findTouchscreenDevice()
{
    for (int i = 0; i < 32; ++i)
    {
        const std::string path = "/dev/input/event" + std::to_string(i);
        const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0)
        {
            continue;
        }

        unsigned long evBits[NumBits(EV_MAX)] = {0};
        unsigned long absBits[NumBits(ABS_MAX)] = {0};

        const bool isTouchscreen = ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), evBits) >= 0 && TestBit(evBits, EV_ABS) &&
                                   ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) >= 0 &&
                                   TestBit(absBits, ABS_MT_POSITION_X) && TestBit(absBits, ABS_MT_POSITION_Y);

        if (isTouchscreen)
        {
            char name[128] = {0};
            ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
            ::close(fd);
            NS_LOGI("EvdevInput: detected multi-touch screen: %s at %s", name, path.c_str());
            return path;
        }
        ::close(fd);
    }
    return std::string();
}

bool EvdevInput::openDevice(const std::string& path)
{
    mFd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (mFd < 0)
    {
        NS_LOGE("EvdevInput: failed to open %s: %s", path.c_str(), std::strerror(errno));
        return false;
    }

    char name[128] = {0};
    if (ioctl(mFd, EVIOCGNAME(sizeof(name) - 1), name) >= 0)
    {
        mDeviceName = name;
    }
    else
    {
        mDeviceName = "Unknown Touchscreen";
    }
    mDevicePath = path;

    unsigned long absBits[NumBits(ABS_MAX)] = {0};
    mHasSlotProtocol = ioctl(mFd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) >= 0 && TestBit(absBits, ABS_MT_SLOT);
    if (mHasSlotProtocol)
    {
        struct input_absinfo slotInfo
        {
        };
        if (ioctl(mFd, EVIOCGABS(ABS_MT_SLOT), &slotInfo) >= 0)
        {
            mSlotCount = std::min(64, std::max(1, slotInfo.maximum + 1));
        }
    }

    struct input_absinfo absX
    {
    }, absY{};
    if (ioctl(mFd, EVIOCGABS(ABS_MT_POSITION_X), &absX) >= 0 && ioctl(mFd, EVIOCGABS(ABS_MT_POSITION_Y), &absY) >= 0)
    {
        mMinX = absX.minimum;
        mMaxX = absX.maximum;
        mMinY = absY.minimum;
        mMaxY = absY.maximum;
    }
    else if (ioctl(mFd, EVIOCGABS(ABS_X), &absX) >= 0 && ioctl(mFd, EVIOCGABS(ABS_Y), &absY) >= 0)
    {
        mMinX = absX.minimum;
        mMaxX = absX.maximum;
        mMinY = absY.minimum;
        mMaxY = absY.maximum;
    }
    else
    {
        mMinX = 0;
        mMaxX = 1080;
        mMinY = 0;
        mMaxY = 2400;
    }

    NS_LOGI("EvdevInput: touch limits: X[%d, %d], Y[%d, %d]", mMinX, mMaxX, mMinY, mMaxY);
    return true;
}

void EvdevInput::readLoop()
{
    const size_t slotCount = static_cast<size_t>(std::max(1, mSlotCount));
    std::vector<SlotState> slots(slotCount);
    int32_t currentSlot = 0;

    bool protocolAMoved = false;
    bool btnTouch = false;

    auto makeEvent = [&](TouchEvent::Phase phase, int32_t id, const SlotState& slot)
    {
        TouchEvent e;
        e.phase = phase;
        e.id = id;
        e.rawX = slot.rawX;
        e.rawY = slot.rawY;
        e.normX = normalizeX(slot.rawX);
        e.normY = normalizeY(slot.rawY);
        e.timestampUs = monotonicMicros();
        return e;
    };

    auto flushProtocolB = [&]()
    {
        for (size_t i = 0; i < slotCount; ++i)
        {
            SlotState& slot = slots[i];
            const int32_t id = static_cast<int32_t>(i);
            if (slot.present && !slot.downSent)
            {
                slot.downSent = true;
                publish(makeEvent(TouchEvent::Phase_Down, id, slot));
            }
            else if (slot.present && slot.downSent && slot.moved)
            {
                publish(makeEvent(TouchEvent::Phase_Move, id, slot));
            }
            else if (!slot.present && slot.downSent)
            {
                slot.downSent = false;
                publish(makeEvent(TouchEvent::Phase_Up, id, slot));
            }
            slot.moved = false;
        }
    };

    auto flushProtocolA = [&]()
    {
        SlotState& slot = slots[0];
        if (btnTouch && !slot.downSent)
        {
            slot.downSent = true;
            publish(makeEvent(TouchEvent::Phase_Down, 0, slot));
        }
        else if (btnTouch && slot.downSent && protocolAMoved)
        {
            publish(makeEvent(TouchEvent::Phase_Move, 0, slot));
        }
        else if (!btnTouch && slot.downSent)
        {
            slot.downSent = false;
            publish(makeEvent(TouchEvent::Phase_Up, 0, slot));
        }
        protocolAMoved = false;
    };

    struct input_event buffer[64];
    struct pollfd pfd
    {
    };
    pfd.fd = mFd;
    pfd.events = POLLIN;

    while (mRunning.load(std::memory_order_acquire))
    {
        const int ret = ::poll(&pfd, 1, 100);
        if (ret < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            NS_LOGW("EvdevInput: poll failed: %s", std::strerror(errno));
            break;
        }
        if (ret == 0)
        {
            continue;
        }
        if (!(pfd.revents & POLLIN))
        {
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            {
                NS_LOGW("EvdevInput: poll revents error (0x%x)", pfd.revents);
                break;
            }
            continue;
        }

        const ssize_t bytes = ::read(mFd, buffer, sizeof(buffer));
        if (bytes < 0)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;
            }
            NS_LOGW("EvdevInput: read failed: %s", std::strerror(errno));
            break;
        }
        if (bytes == 0)
        {
            continue;
        }

        const size_t count = static_cast<size_t>(bytes) / sizeof(struct input_event);
        for (size_t i = 0; i < count; ++i)
        {
            const struct input_event& ev = buffer[i];

            if (ev.type == EV_ABS)
            {
                switch (ev.code)
                {
                    case ABS_MT_SLOT:
                        if (ev.value >= 0 && static_cast<size_t>(ev.value) < slotCount)
                        {
                            currentSlot = ev.value;
                        }
                        break;
                    case ABS_MT_TRACKING_ID:
                        slots[currentSlot].present = (ev.value >= 0);
                        if (ev.value < 0)
                        {
                            slots[currentSlot].moved = true;
                        }
                        break;
                    case ABS_MT_POSITION_X:
                    case ABS_X:
                        slots[currentSlot].rawX = static_cast<float>(ev.value);
                        slots[currentSlot].hasPos = true;
                        slots[currentSlot].moved = true;
                        protocolAMoved = true;
                        break;
                    case ABS_MT_POSITION_Y:
                    case ABS_Y:
                        slots[currentSlot].rawY = static_cast<float>(ev.value);
                        slots[currentSlot].hasPos = true;
                        slots[currentSlot].moved = true;
                        protocolAMoved = true;
                        break;
                    default:
                        break;
                }
            }
            else if (ev.type == EV_KEY && ev.code == BTN_TOUCH)
            {
                btnTouch = (ev.value != 0);
            }
            else if (ev.type == EV_SYN && ev.code == SYN_REPORT)
            {
                if (mHasSlotProtocol)
                {
                    flushProtocolB();
                }
                else
                {
                    flushProtocolA();
                }
            }
        }
    }

    for (size_t i = 0; i < slotCount; ++i)
    {
        if (slots[i].downSent)
        {
            publish(makeEvent(TouchEvent::Phase_Cancel, static_cast<int32_t>(i), slots[i]));
        }
    }
}

}  // namespace ns
