package org.eu.droid_ng.vncflinger;

import static android.content.ClipDescription.MIMETYPE_TEXT_PLAIN;
import static android.hardware.display.DisplayManager.*;

import android.annotation.SuppressLint;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
//import android.hardware.input.InputManagerInternal;
import android.os.IBinder;
import android.util.Log;
import android.view.Surface;

public class VncFlinger extends Service {

	static {
		System.loadLibrary("jni_vncflinger");
	}
	public final int ONGOING_NOTIFICATION_ID = 5;
	public final String CHANNEL_ID = "services";

	public VirtualDisplay display;
	public ClipboardManager mClipboard;
	public boolean isInternal;
	public boolean touch;
	public boolean relative;
	public int w;
	public int h;
	public int dpi;
	public String[] args;

	@SuppressLint("ServiceCast")
	@Override
	public int onStartCommand(Intent intent, int flags, int startId) {
		super.onStartCommand(intent, flags, startId);

		w = intent.getIntExtra("width", -1);
		h = intent.getIntExtra("height", -1);
		dpi = intent.getIntExtra("dpi", -1);
		touch = intent.getBooleanExtra("touch", false);
		relative = intent.getBooleanExtra("useRelativeInput", false);
		isInternal = intent.getBooleanExtra("isInternal", false);
		if ((w < 0 || h < 0 || dpi < 0) && !isInternal) {
			throw new IllegalStateException("invalid extras");
		}

		args = new String[] { "vncflinger", "-rfbunixandroid", "0", "-rfbunixpath", "@vncflinger", "-SecurityTypes", "None" };

		if (!isInternal)
			display = ((DisplayManager)getSystemService(DISPLAY_SERVICE))
					.createVirtualDisplay("VNC", w, h, dpi, null,
							VIRTUAL_DISPLAY_FLAG_SECURE | VIRTUAL_DISPLAY_FLAG_PUBLIC | VIRTUAL_DISPLAY_FLAG_TRUSTED | VIRTUAL_DISPLAY_FLAG_SUPPORTS_TOUCH | VIRTUAL_DISPLAY_FLAG_SHOULD_SHOW_SYSTEM_DECORATIONS);
		mClipboard = (ClipboardManager)getSystemService(Context.CLIPBOARD_SERVICE);
		mClipboard.addPrimaryClipChangedListener(() -> {
			if (mClipboard.hasPrimaryClip() && mClipboard.getPrimaryClipDescription().hasMimeType(MIMETYPE_TEXT_PLAIN)) {
				notifyServerClipboardChanged();
			}
		});

		new Thread(this::workerThread).start();

		//function added in private api 33. revisit when moving to a13 - or ignore if its not neccessary
		//if (touch) return;
		//if (!((InputManagerInternal)getSystemService(INPUT_SERVICE)).setVirtualMousePointerDisplayId(display.getDisplay().getDisplayId()))
		//	Log.w("VNCFlinger:java", "Failed to override pointer displayId");


		NotificationManager notificationManager = getSystemService(NotificationManager.class);
		if (notificationManager.getNotificationChannel(CHANNEL_ID) == null) {
			CharSequence name = getString(R.string.channel_name);
			String description = getString(R.string.channel_description);
			int importance = NotificationManager.IMPORTANCE_LOW;
			NotificationChannel channel = new NotificationChannel(CHANNEL_ID, name, importance);
			channel.setDescription(description);
			channel.setBlockable(true);
			notificationManager.createNotificationChannel(channel);
		}
		Notification notification =
				new Notification.Builder(this, CHANNEL_ID)
						.setContentTitle(getText(R.string.notification_title))
						.setContentText(getString(R.string.notification_message, getText(R.string.app_name)))
						.setSmallIcon(R.drawable.ic_desktop)
						.build();
		startForeground(ONGOING_NOTIFICATION_ID, notification);
		return START_NOT_STICKY;
	}

	@Override
	public void onDestroy() {
		super.onDestroy();
		quit();
		if (display != null) display.release();
	}

	@Override
	public IBinder onBind(Intent intent) {
		return null;
	}

	private void onError(int exitCode) {
		quit();
		if (display != null) display.release();
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
		try {
			display.setSurface(s);
		} catch (NullPointerException unused) {
			Log.w("VNCFlinger", "Failed to set new surface");
		}
	}

	private void setServerClipboard(String text) {
		ClipData clip = ClipData.newPlainText("VNCFlinger", text);
		mClipboard.setPrimaryClip(clip);
	}

	private String getServerClipboard() {
		String text = "";
		if (mClipboard.hasPrimaryClip() && mClipboard.getPrimaryClipDescription().hasMimeType(MIMETYPE_TEXT_PLAIN)) {
			ClipData clipData = mClipboard.getPrimaryClip();
			int i = 0;
			while (!MIMETYPE_TEXT_PLAIN.equals(mClipboard.getPrimaryClipDescription().getMimeType(i)))
				i++;
			ClipData.Item item = clipData.getItemAt(0);
			text = item.getText().toString();
		} else if (mClipboard.hasPrimaryClip()) {
			Log.w("VNCFlinger:Clipboard", "cannot paste :(");
		}

		return text;
	}

	private native int initializeVncFlinger(String[] commandLineArgs);
	private native void setDisplayProps(int w, int h, int rotation, int layerId, boolean touch, boolean relative);
	private native int mainLoop();
	private native void quit();
	private native Surface getSurface();
	private native void notifyServerClipboardChanged();
}