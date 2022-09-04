#define LOG_TAG "VNCFlinger"
#include <utils/Log.h>
#include <jni.h>
#include <gui/Surface.h>
#include <android/native_window_jni.h>
#include <android_view_PointerIcon.h>

#include <fcntl.h>
#include <fstream>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>

#include "AndroidDesktop.h"
#include "AndroidSocket.h"

#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>

#include <network/Socket.h>
#include <network/TcpSocket.h>
#include <network/UnixSocket.h>
#include <rfb/Configuration.h>
#include <rfb/LogWriter.h>
#include <rfb/Logger_android.h>
#include <rfb/VNCServerST.h>
#include <rfb/util.h>

#include <cutils/properties.h>


using namespace vncflinger;
using namespace android;

static bool gCaughtSignal = false;
static std::string mPidFile;
static char gSerialNo[PROPERTY_VALUE_MAX];

static rfb::IntParameter rfbport("rfbport", "TCP port to listen for RFB protocol", 5900);
static rfb::BoolParameter localhostOnly("localhost", "Only allow connections from localhost", false);
static rfb::BoolParameter rfbunixandroid("rfbunixandroid", "Use android control socket to create UNIX socket", true);
static rfb::StringParameter rfbunixpath("rfbunixpath", "Unix socket to listen for RFB protocol", "");
static rfb::IntParameter rfbunixmode("rfbunixmode", "Unix socket access mode", 0600);

static sp<AndroidDesktop> desktop = NULL;
static JNIEnv* gEnv;
static jobject gThiz;
static jmethodID gMethodNewSurfaceAvailable;
static jmethodID gMethodResizeDisplay;
static jmethodID gMethodSetClipboard;
static jmethodID gMethodGetClipboard;

static void printVersion(FILE* fp) {
    fprintf(fp, "VNCFlinger 1.0");
}

static void CleanupSignalHandler(int)
{
    ALOGI("You killed me - cleaning up");
	desktop = NULL;
    if (mPidFile.length() != 0) {
        remove(mPidFile.c_str());
    }
    exit(1);
}

void runJniCallbackNewSurfaceAvailable() {
    gEnv->CallVoidMethod(gThiz, gMethodNewSurfaceAvailable);
}

void runJniCallbackResizeDisplay(int32_t width, int32_t height) {
    gEnv->CallVoidMethod(gThiz, gMethodResizeDisplay, width, height);
}

void runJniCallbackSetClipboard(const char* text) {
    jstring jtext = gEnv->NewStringUTF(text);
    gEnv->CallVoidMethod(gThiz, gMethodSetClipboard, jtext);
    gEnv->DeleteLocalRef(jtext);
}

const char* runJniCallbackGetClipboard() {
    jstring jtext = (jstring)gEnv->CallObjectMethod(gThiz, gMethodGetClipboard);
    const char* text = gEnv->GetStringUTFChars(jtext, NULL);
    char* result = strdup(text);
    gEnv->ReleaseStringUTFChars(jtext, text);
    gEnv->DeleteLocalRef(jtext);
    return result;
}

int desktopSetup(int argc, char** argv);
int startService();

extern "C" void Java_org_eu_droid_1ng_vncflinger_VncFlinger_notifyServerCursorChanged(
    JNIEnv* env, jobject thiz, jobject pointerIconObj) {
    PointerIcon pointerIcon;

    if (desktop != NULL) {
        status_t result = android_view_PointerIcon_getLoadedIcon(env, pointerIconObj, &pointerIcon);
        if (result) {
            ALOGE("Failed to load pointer icon.");
            return;
        }
        if (!pointerIcon.bitmap.isValid()) {
            ALOGE("Pointer icon bitmap not valid");
            return;
        }
        AndroidBitmapInfo bitmapInfo = pointerIcon.bitmap.getInfo();

	    desktop->setCursor(bitmapInfo.width, bitmapInfo.height, pointerIcon.hotSpotX,
                           pointerIcon.hotSpotY, (rdr::U8*)pointerIcon.bitmap.getPixels());
    }
    return;
}

extern "C" jint Java_org_eu_droid_1ng_vncflinger_VncFlinger_initializeVncFlinger(JNIEnv *env,
                                                                                   jobject thiz,
																				   jobjectArray command_line_args) {
	const int argc = env->GetArrayLength(command_line_args);
	char* argv[argc];
	for (int i=0; i<argc; i++) {
		jstring o = (jstring)(env->GetObjectArrayElement(command_line_args, i));
		const char *cmdline_temp = env->GetStringUTFChars(o, NULL);
		argv[i] = strdup(cmdline_temp);
		env->ReleaseStringUTFChars(o, cmdline_temp);
		env->DeleteLocalRef(o);
	}
	env->DeleteLocalRef(command_line_args);
	gThiz = thiz; gEnv = env;
    gMethodNewSurfaceAvailable =
        env->GetMethodID(env->GetObjectClass(thiz), "onNewSurfaceAvailable", "()V");
    gMethodResizeDisplay = env->GetMethodID(env->GetObjectClass(thiz), "onResizeDisplay", "(II)V");
    gMethodSetClipboard = env->GetMethodID(env->GetObjectClass(thiz), "setServerClipboard", "(Ljava/lang/String;)V");
    gMethodGetClipboard = env->GetMethodID(env->GetObjectClass(thiz), "getServerClipboard", "()Ljava/lang/String;");
	return desktopSetup(argc, argv);
}

extern "C" jobject Java_org_eu_droid_1ng_vncflinger_VncFlinger_getSurface(JNIEnv * env,
																			jobject thiz
) {
	if (desktop == NULL) {
		ALOGV("getSurface: desktop == NULL");
		return NULL;
	}
	if (desktop->mVirtualDisplay == NULL){
		ALOGW("getSurface: mVirtualDisplay == NULL");
		return NULL;
	}
	if (desktop->mVirtualDisplay->getProducer() == NULL){
		ALOGW("getSurface: getProducer() == NULL");
		return NULL;
	}
	ANativeWindow* w = new Surface(desktop->mVirtualDisplay->getProducer(), true);
	//Rect dr = desktop->mVirtualDisplay->getDisplayRect();
	//if we want to bring back window resizing without display resize, we need to scale buffer to dr
	if (w == NULL) {
		ALOGE("getSurface: w == NULL");
		return NULL;
	}
	jobject a = ANativeWindow_toSurface(env, w);
	if (a == NULL) {
		ALOGE("getSurface: a == NULL");
	}
	return a;
}

extern "C" jint Java_org_eu_droid_1ng_vncflinger_VncFlinger_startService(JNIEnv* env, jobject thiz) {
    return startService();
}

extern "C" void Java_org_eu_droid_1ng_vncflinger_VncFlinger_quit(JNIEnv *env, jobject thiz) {
	gCaughtSignal = true;
}

extern "C" void Java_org_eu_droid_1ng_vncflinger_VncFlinger_setDisplayProps(JNIEnv *env,
                                                                              jobject thiz, jint w,
                                                                              jint h, jint rotation, jint layerId, jboolean touch,
                                                                              jboolean relative) {
	if (desktop == NULL) {
		ALOGW("setDisplayProps: desktop == NULL");
		return;
	}
	desktop->_width = w; desktop->_height = h; desktop->_rotation = rotation; desktop->mLayerId = layerId; desktop->touch = touch; desktop->relative = relative;
}

extern "C" void Java_org_eu_droid_1ng_vncflinger_VncFlinger_notifyServerClipboardChanged(
    JNIEnv* env, jobject thiz) {
    if (desktop == NULL) {
        ALOGW("notifyClipboardChanged: desktop == NULL");
        return;
    }
    desktop->notifyClipboardChanged();
}

int desktopSetup(int argc, char** argv) {
	rfb::initAndroidLogger();
	rfb::LogWriter::setLogParams("*:android:30");

	rfb::Configuration::enableServerParams();

#ifdef SIGHUP
	signal(SIGHUP, CleanupSignalHandler);
#endif
	signal(SIGINT, CleanupSignalHandler);
	signal(SIGTERM, CleanupSignalHandler);

	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			if (i + 1 < argc) {
				if (rfb::Configuration::setParam(&argv[i][1], argv[i + 1])) {
					i++;
					continue;
				}
			}
		}

		if (rfb::Configuration::setParam(argv[i])) continue;

		if (argv[i][0] == '-') {
			if (strcmp(argv[i], "-pid") == 0) {
				if (i + 1 < argc) {
					mPidFile = std::string(argv[i + 1]);
					i++;
					continue;
				}
			}
			if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "-version") == 0 ||
			    strcmp(argv[i], "--version") == 0) {
				printVersion(stdout);
				return 4;
			}
			return 2;
		}

		free(argv[i]);

		ALOGE("Invalid input. i=%d", i);
		return 5;
	}

	desktop = new AndroidDesktop();

	return 0;
}

int startService() {
	property_get("ro.build.product", gSerialNo, "");
	std::string desktopName = "VNCFlinger";
	desktopName += " @ ";
	desktopName += (const char *) gSerialNo;

    sp<ProcessState> self = ProcessState::self();
    self->startThreadPool();

    std::list<network::SocketListener*> listeners;
    int ret = 0;
    try {
        rfb::VNCServerST server(desktopName.c_str(), desktop.get());

        if (rfbunixpath.getValueStr()[0] != '\0') {
			if (rfbunixandroid) {
				listeners.push_back(new AndroidListener(rfbunixpath));
			} else {
				if (rfbunixpath.getValueStr()[0] != '@') {
					listeners.push_back(new network::UnixListener(rfbunixpath, rfbunixmode));
				} else {
					listeners.push_back(new AbsUnixListener(rfbunixpath));
				}
			}
            ALOGI("Listening on %s (mode %04o)", (const char*)rfbunixpath, (int)rfbunixmode);
        } else {
            if (localhostOnly) {
                network::createLocalTcpListeners(&listeners, (int)rfbport);
            } else {
                network::createTcpListeners(&listeners, 0, (int)rfbport);
                ALOGI("Listening on port %d", (int)rfbport);
            }
        }

        int eventFd = desktop->getEventFd();
        fcntl(eventFd, F_SETFL, O_NONBLOCK);

        if (mPidFile.length() != 0) {
            // write a pid file
            ALOGI("pid file %s", mPidFile.c_str());
            pid_t pid = getpid();
            std::ofstream outfile(mPidFile);
            outfile << pid;
            outfile.close();
        }

        while (!gCaughtSignal) {
            int wait_ms;
            struct timeval tv;
            fd_set rfds, wfds;
            std::list<network::Socket*> sockets;
            std::list<network::Socket*>::iterator i;

            FD_ZERO(&rfds);
            FD_ZERO(&wfds);

            FD_SET(eventFd, &rfds);
            for (std::list<network::SocketListener*>::iterator i = listeners.begin();
                 i != listeners.end(); i++)
                FD_SET((*i)->getFd(), &rfds);

            server.getSockets(&sockets);
            int clients_connected = 0;
            for (i = sockets.begin(); i != sockets.end(); i++) {
                if ((*i)->isShutdown()) {
                    server.removeSocket(*i);
                    delete (*i);
                } else {
                    FD_SET((*i)->getFd(), &rfds);
                    if ((*i)->outStream().hasBufferedData()) {
                        FD_SET((*i)->getFd(), &wfds);
                    }
                    clients_connected++;
                }
            }

            wait_ms = 0;

            rfb::soonestTimeout(&wait_ms, rfb::Timer::checkTimeouts());

            tv.tv_sec = wait_ms / 1000;
            tv.tv_usec = (wait_ms % 1000) * 1000;

            int n = select(FD_SETSIZE, &rfds, &wfds, 0, wait_ms ? &tv : NULL);

            if (n < 0) {
                if (errno == EINTR) {
                    ALOGV("Interrupted select() system call");
                    continue;
                } else {
                    throw rdr::SystemException("select", errno);
                }
            }

            // Accept new VNC connections
            for (std::list<network::SocketListener*>::iterator i = listeners.begin();
                 i != listeners.end(); i++) {
                if (FD_ISSET((*i)->getFd(), &rfds)) {
                    network::Socket* sock = (*i)->accept();
                    if (sock) {
                        server.addSocket(sock);
                    } else {
                        ALOGW("Client connection rejected");
                    }
                }
            }

            rfb::Timer::checkTimeouts();

            // Client list could have been changed.
            server.getSockets(&sockets);

            // Nothing more to do if there are no client connections.
            if (sockets.empty()) continue;

            // Process events on existing VNC connections
            for (i = sockets.begin(); i != sockets.end(); i++) {
                if (FD_ISSET((*i)->getFd(), &rfds)) server.processSocketReadEvent(*i);
                if (FD_ISSET((*i)->getFd(), &wfds)) server.processSocketWriteEvent(*i);
            }

	        // Process events from the display
            uint64_t eventVal;
            int status = read(eventFd, &eventVal, sizeof(eventVal));
            if (status > 0 && eventVal > 0) {
                //ALOGV("status=%d eventval=%" PRIu64, status, eventVal);
	            desktop->processCursor();
                desktop->processFrames();
            }

        }
        ret = 0;
    } catch (rdr::Exception& e) {
        ALOGE("%s", e.str());
        ret = 3;
    }
	desktop = NULL;
    ALOGI("Bye - cleaning up");
	gEnv = NULL;
	gThiz = NULL;
	gSerialNo[0] = '\0';
	for (std::list<network::SocketListener*>::iterator i = listeners.begin();
	     i != listeners.end(); i++)
		delete (*i);
    if (mPidFile.length() != 0) {
        remove(mPidFile.c_str());
    }
    return ret;
}
