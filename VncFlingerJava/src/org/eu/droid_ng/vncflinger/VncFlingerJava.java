/*
 * Copyright (C) 2022 droid-ng
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.eu.droid_ng.vncflinger;

import static android.hardware.display.DisplayManager.*;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.SurfaceTexture;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.os.IBinder;
import android.os.SystemProperties;
import android.util.Log;
import android.view.Surface;

public class VncFlingerJava extends Service {
    private static final String TAG = "VncFlingerJava";
    private SurfaceTexture t;
    private Surface s;
    private VirtualDisplay vd;

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        super.onStartCommand(intent, flags, startId);

        Log.i(TAG, "Creating...");
        int w = intent.getIntExtra("width", -1);
        int h = intent.getIntExtra("height", -1);
        int dpi = intent.getIntExtra("dpi", -1);
        boolean touch = intent.getBooleanExtra("touch", false);
        boolean relativeInput = intent.getBooleanExtra("useRelativeInput", false);

        if (w < 0 || h < 0 || dpi < 0) {
            throw new IllegalStateException("invalid extras");
        }

        // Step 1: create dummy surface which should do nothing i hope
        t = new SurfaceTexture(false);
        s = new Surface(t);

        // Step 2: make the virtual display
        DisplayManager dm = (DisplayManager) getApplicationContext().getSystemService(Context.DISPLAY_SERVICE);
        vd = dm.createVirtualDisplay("VncFlingerJava", w, h, dpi, s, VIRTUAL_DISPLAY_FLAG_SECURE | VIRTUAL_DISPLAY_FLAG_PUBLIC | VIRTUAL_DISPLAY_FLAG_TRUSTED | VIRTUAL_DISPLAY_FLAG_SUPPORTS_TOUCH | VIRTUAL_DISPLAY_FLAG_SHOULD_SHOW_SYSTEM_DECORATIONS);
        Log.i(TAG, "created " + vd.getDisplay().getDisplayId() + " " + w + "x" + h + " " + dpi + "dpi display rotated " + vd.getDisplay().getRotation());

        // Step 3: set props
        SystemProperties.set("sys.vnc.layer_id", String.valueOf(vd.getDisplay().getDisplayId()));
        SystemProperties.set("sys.vnc.width", String.valueOf(w));
        SystemProperties.set("sys.vnc.height", String.valueOf(h));
        SystemProperties.set("sys.vnc.rotation", String.valueOf(vd.getDisplay().getRotation() * 90));
        SystemProperties.set("sys.vnc.touch", String.valueOf(touch ? 1 : 0));
        SystemProperties.set("sys.vnc.relative_input", String.valueOf(relativeInput ? 1 : 0));

        SystemProperties.set("sys.vnc.enable", "true");

        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        super.onDestroy();

        SystemProperties.set("sys.vnc.enable", "false");

        Log.i(TAG, "Destroying...");
        vd.release();
        s.release();
        t.release();

        SystemProperties.set("sys.vnc.layer_id", String.valueOf(-1));
        SystemProperties.set("sys.vnc.width", String.valueOf(-1));
        SystemProperties.set("sys.vnc.height", String.valueOf(-1));
        SystemProperties.set("sys.vnc.rotation", String.valueOf(-1));
        SystemProperties.set("sys.vnc.touch", String.valueOf(0));
        SystemProperties.set("sys.vnc.relative_input", String.valueOf(0));
    }
}
