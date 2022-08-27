#ifndef ANDROID_DESKTOP_H_
#define ANDROID_DESKTOP_H_

#include <memory>

#include <utils/Condition.h>
#include <utils/Mutex.h>
#include <utils/RefBase.h>
#include <utils/Thread.h>

#include <gui/CpuConsumer.h>

#include <ui/DisplayMode.h>
#include <ui/DisplayState.h>

#include <rfb/PixelBuffer.h>
#include <rfb/SDesktop.h>
#include <rfb/ScreenSet.h>

#include "AndroidPixelBuffer.h"
#include "InputDevice.h"
#include "VirtualDisplay.h"

using namespace android;

namespace vncflinger {

class AndroidDesktop : public rfb::SDesktop,
                       public CpuConsumer::FrameAvailableListener,
                       public AndroidPixelBuffer::BufferDimensionsListener {
  public:
    AndroidDesktop();

    virtual ~AndroidDesktop();

    virtual void start(rfb::VNCServer* vs);
    virtual void stop();
    virtual void terminate();
    virtual void handleClipboardRequest();
    virtual void handleClipboardAnnounce(bool available);
    virtual void handleClipboardData(const char* data);
    virtual void notifyClipboardChanged();

    virtual unsigned int setScreenLayout(int fb_width, int fb_height, const rfb::ScreenSet& layout);

    virtual void keyEvent(rdr::U32 keysym, rdr::U32 keycode, bool down);
    virtual void pointerEvent(const rfb::Point& pos, int buttonMask);

    virtual void processFrames();

    virtual int getEventFd() {
        return mEventFd;
    }

    virtual void onBufferDimensionsChanged(uint32_t width, uint32_t height);

    virtual void onFrameAvailable(const BufferItem& item);

    virtual void queryConnection(network::Socket* sock, const char* userName);

	// Virtual display controller
	int32_t mLayerId = -1;
    sp<VirtualDisplay> mVirtualDisplay;
    int32_t _width = 1, _height = 1, _rotation = 0;
	bool touch = false, relative = false;
  private:
    virtual void notify();

    virtual status_t updateDisplayInfo(bool force = false);

    virtual rfb::ScreenSet computeScreenLayout();

    Rect mDisplayRect;

    Mutex mLock;

    uint64_t mFrameNumber;

    int mEventFd;

    // Server instance
    rfb::VNCServer* mServer;

    // Pixel buffer
    sp<AndroidPixelBuffer> mPixels = NULL;

    // Primary display
    ui::Size mDisplayMode = {};
    ui::Rotation mDisplayState = {};

    // Virtual input device
    sp<InputDevice> mInputDevice;
};
};

#endif
