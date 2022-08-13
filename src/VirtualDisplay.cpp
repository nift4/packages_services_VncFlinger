//
// vncflinger - Copyright (C) 2021 Stefanie Kondik
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#define LOG_TAG "VNCFlinger:VirtualDisplay"
#include <utils/Log.h>

#include <gui/BufferQueue.h>
#include <gui/CpuConsumer.h>
#include <gui/IGraphicBufferConsumer.h>
#include <gui/SurfaceComposerClient.h>
#include <input/DisplayViewport.h>
#include "VirtualDisplay.h"

using namespace vncflinger;

VirtualDisplay::VirtualDisplay(ui::Size* mode, ui::Rotation* state,
                               uint32_t width, uint32_t height, uint32_t layerId,
                               sp<CpuConsumer::FrameAvailableListener> listener) {
    mWidth = width;
    mHeight = height;
    mLayerId = layerId;

    if (*state == ui::ROTATION_0 || *state == ui::ROTATION_180) {
        mSourceRect = Rect(mode->width, mode->height);
    } else {
        mSourceRect = Rect(mode->height, mode->width);
    }

    Rect displayRect = getDisplayRect();

    sp<IGraphicBufferConsumer> consumer;
    BufferQueue::createBufferQueue(&mProducer, &consumer);
    mCpuConsumer = new CpuConsumer(consumer, 1);
    mCpuConsumer->setName(String8("vds-to-cpu"));
    mCpuConsumer->setDefaultBufferSize(width, height);
    mProducer->setMaxDequeuedBufferCount(4);
    consumer->setDefaultBufferFormat(PIXEL_FORMAT_RGBX_8888);

    mCpuConsumer->setFrameAvailableListener(listener);

    mDisplayToken = SurfaceComposerClient::createDisplay(String8("VNC-VirtualDisplay"), true /*secure*/);

    SurfaceComposerClient::Transaction t;
    t.setDisplaySurface(mDisplayToken, mProducer);
    t.setDisplayProjection(mDisplayToken, *state, mSourceRect, displayRect);
    t.setDisplayLayerStack(mDisplayToken, mLayerId);
    t.apply();

    ALOGV("Virtual display (%ux%u [viewport=%ux%u] created", width, height, displayRect.getWidth(),
          displayRect.getHeight());
}

VirtualDisplay::~VirtualDisplay() {
    mCpuConsumer.clear();
    mProducer.clear();
    SurfaceComposerClient::destroyDisplay(mDisplayToken);

    ALOGV("Virtual display destroyed");
}

Rect VirtualDisplay::getDisplayRect() {
    uint32_t outWidth, outHeight;
    if (mWidth <= (uint32_t)((float)mHeight * aspectRatio())) {
        // limited by narrow width; reduce height
        outWidth = mWidth;
        outHeight = (uint32_t)((float)mWidth / aspectRatio());
    } else {
        // limited by short height; restrict width
        outHeight = mHeight;
        outWidth = (uint32_t)((float)mHeight * aspectRatio());
    }

    // position the desktop in the viewport while preserving
    // the source aspect ratio. we do this in case the client
    // has resized the window and to deal with orientation
    // changes set up by updateDisplayProjection
    uint32_t offX, offY;
    offX = (mWidth - outWidth) / 2;
    offY = (mHeight - outHeight) / 2;
    return Rect(offX, offY, offX + outWidth, offY + outHeight);
}
