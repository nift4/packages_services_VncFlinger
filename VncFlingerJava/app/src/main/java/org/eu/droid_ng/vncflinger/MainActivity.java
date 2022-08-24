package org.eu.droid_ng.vncflinger;

import static android.hardware.display.DisplayManager.*;

import android.app.Activity;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.os.Bundle;
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

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.activity_main);

		w = 1280; h = 720; dpi = 220; //TODO: get from intent
		display = ((DisplayManager)getSystemService(DISPLAY_SERVICE)).createVirtualDisplay("VNC", w, h, dpi, null, VIRTUAL_DISPLAY_FLAG_SECURE | VIRTUAL_DISPLAY_FLAG_PUBLIC | VIRTUAL_DISPLAY_FLAG_TRUSTED | VIRTUAL_DISPLAY_FLAG_SUPPORTS_TOUCH | VIRTUAL_DISPLAY_FLAG_SHOULD_SHOW_SYSTEM_DECORATIONS);

		args = new String[] { "vncflinger" };
		new Thread(this::workerThread).start();
	}

	@Override
	protected void onDestroy() {
		super.onDestroy();
		quit();
		display.release();
	}

	private void onError() {
		quit();
		display.release();
		throw new IllegalStateException("VNCFlinger died");
	}

	public void workerThread() {
		if (initializeVncFlinger(args) == 0) {
			callback();
			if (mainLoop() == 0)
				return;
		}
		onError();
	}

	//used from native
	public void callback() {
		Log.e("VNCFlinger", "new surface!!");
		Surface s = getSurface();
		if (s == null)
			Log.e("VNCFlinger", "new surface is null!");
		display.setSurface(s);
	}
	private native int initializeVncFlinger(String[] commandLineArgs);
	private native int mainLoop();
	private native void quit();
	private native Surface getSurface();
}