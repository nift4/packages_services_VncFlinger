package org.eu.droid_ng.vncflinger;

import static android.hardware.display.DisplayManager.*;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
//import android.hardware.input.InputManagerInternal;
import android.os.Bundle;
import android.util.Log;
import android.view.Surface;

public class MainActivity extends Activity {

	static {
		System.loadLibrary("jni_vncflinger");
	}

	public VirtualDisplay display;
	public boolean isInternal;
	public boolean touch;
	public boolean relative;
	public int w;
	public int h;
	public int dpi;
	public String[] args;
	public static boolean didInit = false;

	@SuppressLint("ServiceCast")
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		if (didInit) return;
		didInit = true;
		setContentView(R.layout.activity_main);

		w = 1280; h = 720; dpi = 220; touch = false; relative = false; isInternal = true; //TODO: move activity -> service and then get from intent
		args = new String[] { "vncflinger", "-rfbunixandroid", "0", "-rfbunixpath", "@vncflinger", "-SecurityTypes", "None" };

		if (!isInternal) display = ((DisplayManager)getSystemService(DISPLAY_SERVICE)).createVirtualDisplay("VNC", w, h, dpi, null, VIRTUAL_DISPLAY_FLAG_SECURE | VIRTUAL_DISPLAY_FLAG_PUBLIC | VIRTUAL_DISPLAY_FLAG_TRUSTED | VIRTUAL_DISPLAY_FLAG_SUPPORTS_TOUCH | VIRTUAL_DISPLAY_FLAG_SHOULD_SHOW_SYSTEM_DECORATIONS);
		new Thread(this::workerThread).start();

		//function added in private api 33. revisit when moving to a13 - or ignore if its not neccessary
		//if (touch) return;
		//if (!((InputManagerInternal)getSystemService(INPUT_SERVICE)).setVirtualMousePointerDisplayId(display.getDisplay().getDisplayId()))
		//	Log.w("VNCFlinger:java", "Failed to override pointer displayId");
	}

	@Override
	protected void onDestroy() {
		super.onDestroy();
		quit();
		display.release();
	}

	private void onError(int exitCode) {
		quit();
		display.release();
		throw new IllegalStateException("VNCFlinger died, exit code " + exitCode);
	}

	private void workerThread() {
		int exitCode;
		if ((exitCode = initializeVncFlinger(args)) == 0) {
			doSetDisplayProps();
			if ((exitCode = mainLoop()) == 0)
				return;
		}
		onError(exitCode);
	}

	private void doSetDisplayProps() {
		setDisplayProps(isInternal ? -1 : w, isInternal ? -1 : h, isInternal ? -1 : display.getDisplay().getRotation() * 90, isInternal ? 0 : -1, touch, relative);
	}

	//used from native
	private void callback() {
		doSetDisplayProps();
		if (isInternal) return;
		Log.i("VNCFlinger", "new surface");
		Surface s = getSurface();
		if (s == null)
			Log.i("VNCFlinger", "new surface is null");
		display.setSurface(s);
	}

	private native int initializeVncFlinger(String[] commandLineArgs);
	private native void setDisplayProps(int w, int h, int rotation, int layerId, boolean touch, boolean relative);
	private native int mainLoop();
	private native void quit();
	private native Surface getSurface();
}