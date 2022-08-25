package org.eu.droid_ng.vncflinger;

import static android.hardware.display.DisplayManager.*;

import android.app.Activity;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.os.Bundle;
import android.os.SystemProperties;
import android.util.Log;
import android.view.Surface;

public class MainActivity extends Activity {

	static {
		System.loadLibrary("jni_vncflinger");
	}

	public VirtualDisplay display;
	public int w;
	public int h;
	public int dpi;
	public String[] args;
	public static boolean didInit = false;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		if (didInit) return;
		didInit = true;
		setContentView(R.layout.activity_main);

		w = 1280; h = 720; dpi = 220; boolean touch = true; boolean relativeInput = false; //TODO: get from intent
		args = new String[] { "vncflinger", "-rfbunixandroid", "0", "-rfbunixpath", "@vncflinger", "-SecurityTypes", "None" };

		display = ((DisplayManager)getSystemService(DISPLAY_SERVICE)).createVirtualDisplay("VNC", w, h, dpi, null, VIRTUAL_DISPLAY_FLAG_SECURE | VIRTUAL_DISPLAY_FLAG_PUBLIC | VIRTUAL_DISPLAY_FLAG_TRUSTED | VIRTUAL_DISPLAY_FLAG_SUPPORTS_TOUCH | VIRTUAL_DISPLAY_FLAG_SHOULD_SHOW_SYSTEM_DECORATIONS);
		//SystemProperties.set("sys.vnc.touch", String.valueOf(touch ? 1 : 0));
		//SystemProperties.set("sys.vnc.relative_input", String.valueOf(relativeInput ? 1 : 0));
		new Thread(this::workerThread).start();
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

	public void workerThread() {
		int exitCode;
		if ((exitCode = initializeVncFlinger(args)) == 0) {
			doSetDisplayProps();
			if ((exitCode = mainLoop()) == 0)
				return;
		}
		onError(exitCode);
	}

	private void doSetDisplayProps() {
		setDisplayProps(w, h, display.getDisplay().getRotation() * 90);
	}

	//used from native
	public void callback() {
		Log.e("VNCFlinger", "new surface!!");
		doSetDisplayProps();
		Surface s = getSurface();
		if (s == null)
			Log.e("VNCFlinger", "new surface is null!");
		else
			display.setSurface(s);
		try {
			Thread.sleep(500);
		} catch (InterruptedException e) {
			e.printStackTrace();
		}
	}

	private native int initializeVncFlinger(String[] commandLineArgs);
	private native void setDisplayProps(int w, int h, int rotation);
	private native int mainLoop();
	private native void quit();
	private native Surface getSurface();
}