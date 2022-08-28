#define LOG_TAG "VNCFlinger:AndroidDesktop"
#include <utils/Log.h>

#include <fcntl.h>
#include <inttypes.h>
#include <sys/eventfd.h>
#include <sys/system_properties.h>

#include <gui/ISurfaceComposer.h>
#include <gui/SurfaceComposerClient.h>

#include <rfb/PixelFormat.h>
#include <rfb/Rect.h>
#include <rfb/ScreenSet.h>

#include "AndroidDesktop.h"
#include "AndroidPixelBuffer.h"
#include "InputDevice.h"
#include "VirtualDisplay.h"

using namespace vncflinger;
using namespace android;
//main.cpp
extern void runJniCallback();
extern void runJniCallbackResize(int32_t w, int32_t h);
extern void runJniCallbackSetClipboard(const char* text);
extern const char* getJniCallbackGetClipboard();

AndroidDesktop::AndroidDesktop() {
    mDisplayRect = Rect(0, 0);

    mEventFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (mEventFd < 0) {
        ALOGE("Failed to create event notifier");
        return;
    }

    runJniCallback();
}

AndroidDesktop::~AndroidDesktop() {
    close(mEventFd);
}

void AndroidDesktop::start(rfb::VNCServer* vs) {
    mServer = vs;
    mInputDevice = new InputDevice();

    mPixels = new AndroidPixelBuffer();
    mPixels->setDimensionsChangedListener(this);

    if (updateDisplayInfo(true) != NO_ERROR) {
        ALOGE("Failed to query display!");
        return;
    }

    rfb::ScreenSet screens = computeScreenLayout();
    mServer->setPixelBuffer(mPixels.get(), screens);
    ALOGV("Desktop is running");
}

void AndroidDesktop::stop() {
    Mutex::Autolock _L(mLock);

    ALOGV("Shutting down");

    mServer->setPixelBuffer(0);
    mPixels->reset();

    mVirtualDisplay.clear();
    mPixels.clear();
    mInputDevice->stop();

    runJniCallback();
}

void AndroidDesktop::handleClipboardRequest() {
    const char* data = getJniCallbackGetClipboard();
    if (strlen(data)) mServer->sendClipboardData(data);
}

void AndroidDesktop::handleClipboardAnnounce(bool available) {
    if (available) mServer->requestClipboard();
}

void AndroidDesktop::handleClipboardData(const char* data) {
    runJniCallbackSetClipboard(data);
}

void AndroidDesktop::notifyClipboardChanged() {
    if (mServer) mServer->announceClipboard(true);
}

void AndroidDesktop::processFrames() {
    Mutex::Autolock _l(mLock);

    updateDisplayInfo();

    // get a frame from the virtual display
    CpuConsumer::LockedBuffer imgBuffer;
    status_t res = mVirtualDisplay->getConsumer()->lockNextBuffer(&imgBuffer);
    if (res != OK) {
        ALOGE("Failed to lock next buffer: %s (%d)", strerror(-res), res);
        return;
    }

    mFrameNumber = imgBuffer.frameNumber;
    //ALOGV("processFrame: [%" PRIu64 "] format: %x (%dx%d, stride=%d)", mFrameNumber, imgBuffer.format,
    //      imgBuffer.width, imgBuffer.height, imgBuffer.stride);

    // we don't know if there was a stride change until we get
    // a buffer from the queue. if it changed, we need to resize

    rfb::Rect bufRect(0, 0, imgBuffer.width, imgBuffer.height);

    // performance is extremely bad if the gpu memory is used
    // directly without copying because it is likely uncached
    mPixels->imageRect(bufRect, imgBuffer.data, imgBuffer.stride);

    mVirtualDisplay->getConsumer()->unlockBuffer(imgBuffer);

    // update clients
    mServer->add_changed(bufRect);
}

// notifies the server loop that we have changes
void AndroidDesktop::notify() {
    static uint64_t notify = 1;
    write(mEventFd, &notify, sizeof(notify));
}

// called when a client resizes the window
unsigned int AndroidDesktop::setScreenLayout(int reqWidth, int reqHeight,
                                             const rfb::ScreenSet& layout) {
	if (mLayerId < 0) {
        runJniCallbackResize(reqWidth, reqHeight);
        // if we return success, we crash because the mode change took too long.
        return rfb::resultInvalid;
    }

    Mutex::Autolock _l(mLock);

    char* dbg = new char[1024];
    layout.print(dbg, 1024);

    ALOGD("setScreenLayout: cur: %s  new: %dx%d", dbg, reqWidth, reqHeight);
    delete[] dbg;

    if (reqWidth == mDisplayRect.getWidth() && reqHeight == mDisplayRect.getHeight()) {
        return rfb::resultInvalid;
    }

    if (reqWidth > 0 && reqHeight > 0) {
        mPixels->setWindowSize(reqWidth, reqHeight);

        rfb::ScreenSet screens;
        screens.add_screen(rfb::Screen(0, 0, 0, mPixels->width(), mPixels->height(), 0));
        mServer->setScreenLayout(screens);
        return rfb::resultSuccess;
    }

    return rfb::resultInvalid;
}

// cpuconsumer frame listener, called from binder thread
void AndroidDesktop::onFrameAvailable(const BufferItem& item) {
    //ALOGV("onFrameAvailable: [%" PRIu64 "] mTimestamp=%" PRId64, item.mFrameNumber, item.mTimestamp);

    notify();
}

void AndroidDesktop::keyEvent(rdr::U32 keysym, __unused_attr rdr::U32 keycode, bool down) {
    mInputDevice->keyEvent(down, keysym);
}

void AndroidDesktop::pointerEvent(const rfb::Point& pos, int buttonMask) {
    if (pos.x < mDisplayRect.left || pos.x > mDisplayRect.right || pos.y < mDisplayRect.top ||
        pos.y > mDisplayRect.bottom) {
        ALOGV("pointer dropped x=%d y=%d left=%d right=%d top=%d bottom=%d", pos.x, pos.y, mDisplayRect.left, mDisplayRect.right, mDisplayRect.top, mDisplayRect.bottom);
        // outside viewport
        return;
    }
    uint32_t mx = (mPixels->width() - mDisplayRect.getWidth()) / 2;
    uint32_t my = (mPixels->height() - mDisplayRect.getHeight()) / 2;
    uint32_t x = (((pos.x - mx) * mDisplayMode.width) / ((float)(mDisplayRect.getWidth())));
    uint32_t y = (((pos.y - my) * mDisplayMode.height) / ((float)(mDisplayRect.getHeight())));

    ALOGV("pointer xlate x1=%d y1=%d x2=%d y2=%d", pos.x, pos.y, x, y);

    mServer->setCursorPos(rfb::Point(x, y));
    mInputDevice->pointerEvent(buttonMask, x, y);
}

// refresh the display dimensions
status_t AndroidDesktop::updateDisplayInfo(bool force) {
    if (mLayerId == 0) {
        const auto displayToken = SurfaceComposerClient::getInternalDisplayToken();
        if (displayToken == nullptr) {
            ALOGE("Failed to get display token\n");
            return -1;
        }

        ui::DisplayMode tempDisplayMode;
        status_t err = SurfaceComposerClient::getActiveDisplayMode(displayToken, &tempDisplayMode);
        if (err != NO_ERROR) {
            ALOGE("Failed to get display configuration\n");
            return err;
        }
        mDisplayMode = tempDisplayMode.resolution;
        ALOGV("updateDisplayInfo: [%d:%d]", mDisplayMode.width, mDisplayMode.height);

        ui::DisplayState tempDisplayState;
        err = SurfaceComposerClient::getDisplayState(displayToken, &tempDisplayState);
        if (err != NO_ERROR) {
            ALOGE("Failed to get current display status");
            return err;
        }
        mDisplayState = tempDisplayState.orientation;

        mLayerId = 0; // internal display constant id
    } else if (_width > 0 && _height > 0 && _rotation > -1) {
        mDisplayMode = ui::Size(_width, _height);
        mDisplayState = _rotation == 270 ? ui::ROTATION_270 : (_rotation == 180 ? ui::ROTATION_180 : (_rotation == 90 ? ui::ROTATION_90 : ui::ROTATION_0));
    } else {
        ALOGE("Invalid rect");
        return -1;
    }

    ALOGV("updateDisplayInfo: [%d:%d], rotated %d, layerId %d", mDisplayMode.width, mDisplayMode.height, mDisplayState, mLayerId);
	if (mPixels != NULL)
        mPixels->setDisplayInfo(&mDisplayMode, &mDisplayState, force);
    return NO_ERROR;
}

rfb::ScreenSet AndroidDesktop::computeScreenLayout() {
    rfb::ScreenSet screens;
    screens.add_screen(rfb::Screen(0, 0, 0, mPixels->width(), mPixels->height(), 0));
    return screens;
    mServer->setScreenLayout(screens);
}

void AndroidDesktop::onBufferDimensionsChanged(uint32_t width, uint32_t height) {
    ALOGI("Dimensions changed: old=(%ux%u) new=(%ux%u)", mDisplayRect.getWidth(),
          mDisplayRect.getHeight(), width, height);

    mVirtualDisplay.clear();
    mVirtualDisplay = new VirtualDisplay(&mDisplayMode,  &mDisplayState,
                                         mPixels->width(), mPixels->height(), mLayerId, this);
    runJniCallback();

    mDisplayRect = mVirtualDisplay->getDisplayRect();

    mInputDevice->reconfigure(mDisplayMode.width, mDisplayMode.height, touch, relative);

    mServer->setPixelBuffer(mPixels.get(), computeScreenLayout());
    mServer->setScreenLayout(computeScreenLayout());
}

void AndroidDesktop::queryConnection(network::Socket* sock, __unused_attr const char* userName) {
    mServer->approveConnection(sock, true, NULL);
}

void AndroidDesktop::terminate() {
    kill(getpid(), SIGTERM);
}
