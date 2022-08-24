package org.eu.droid_ng.vncflinger;

import android.app.Activity;
import android.os.Bundle;
import android.view.Surface;

public class MainActivity extends Activity {

	static {
		System.loadLibrary("vncflinger");
	}

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.activity_main);

		new Thread(this::workerThread).start();
	}

	public void workerThread() {

	}

	public native Surface initializeVncFlinger(String commandLineArgs);
	public native void mainLoop();
}