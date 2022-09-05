/*
 * Copyright (C) 2022 LibreMobileOS Foundation
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

#define LOG_TAG "audiostreamer"

#include "socketmanager.h"
#include <android/content/AttributionSourceState.h>
#include <cutils/log.h>
#include <errno.h>
#include <media/AudioRecord.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>
#include <jni.h>

using namespace android;

#define kBufferOutputSize (1452)
#define READ_AUDIO_MAX 2048
#define DEFAULT_SOCKET_TCP_PORT (9200)
#define DEFAULT_SOCKET_UNIX_NAME "audiostreamer"

static char *gProgramName;
static int gSocketTCPPort = DEFAULT_SOCKET_TCP_PORT;
static bool gUsingSocketAndroid = false;
static bool gUsingSocketUnix = false;
static bool gRunning = false;
static char *gSocketName;
sp<AudioRecord> pAudioRecord = NULL;

uint32_t sampleRate = 48000;
int channel = 2;

using android::content::AttributionSourceState;

static int audiostreamer_init() {
    size_t framecount = 0;

    AudioRecord::getMinFrameCount(&framecount, sampleRate, AUDIO_FORMAT_PCM_16_BIT, audio_channel_in_mask_from_count(channel));
    ALOGI("%s: sampleRate: %d, channel: %d framecount: %d", __FUNCTION__, sampleRate, channel, (int)framecount);

    AttributionSourceState attributionSource;
    attributionSource.packageName = "com.libremobileos.vncflinger";
    attributionSource.token = sp<BBinder>::make();

    pAudioRecord = new AudioRecord(AUDIO_SOURCE_REMOTE_SUBMIX, sampleRate, AUDIO_FORMAT_PCM_16_BIT,
                                   audio_channel_in_mask_from_count(channel), attributionSource, framecount);

    if (pAudioRecord == NULL) {
        ALOGE("%s: create AudioRecord failed", __FUNCTION__);
        return -1;
    }

    if (pAudioRecord->initCheck() != OK) {
        ALOGE("%s: init AudioRecord failed", __FUNCTION__);
        return -1;
    }

    return 0;
}

void audiostreamer_record_thread(void *arg) {
    int err = 0;
    char *pReadBuf = NULL;
    int iReadLen = 0;
    int nSock = 0;

    if (NULL != arg) {
        nSock = *((int *)arg);
        ALOGI("%s: nSock:%d", __FUNCTION__, nSock);
    } else {
        ALOGI("%s: NULL==arg, return.", __FUNCTION__);
        return;
    }

    if (pAudioRecord == NULL) {
        err = audiostreamer_init();
        if (err != 0) {
            ALOGE("%s: create AudioRecord failed", __FUNCTION__);
            return;
        }
    }

    pReadBuf = (char *) malloc(READ_AUDIO_MAX);
    if (pReadBuf == NULL) {
        ALOGE("%s: Failed to allocate memory", __FUNCTION__);
        return;
    }

    pAudioRecord->start();
    while (gRunning) {
        memset(pReadBuf, 0, READ_AUDIO_MAX);

        iReadLen = pAudioRecord->read(pReadBuf, kBufferOutputSize);
        if (iReadLen <= 0) {
            ALOGE("%s: pAudioRecord->read failed", __FUNCTION__);
            continue;
        }

        if (!sendDataSocket(nSock, pReadBuf, iReadLen, 0)) break;
    }

    if (pAudioRecord != NULL) {
        ALOGI("%s: pAudioRecord->stop", __FUNCTION__);
        pAudioRecord->stop();
        if (pAudioRecord->stopped()) {
            ALOGI("%s: pAudioRecord->stop end", __FUNCTION__);
        }
    }

    if (pReadBuf)
        free(pReadBuf);

    closeSocket(nSock);
}

void audiostreamer_create_thread() {
    int sock = 0;
    int sock_n = 0;

    if (gUsingSocketAndroid)
        sock = createAndroidSocket(DEFAULT_SOCKET_UNIX_NAME);
    else if (gUsingSocketUnix)
        sock = createUnixSocket(gSocketName);
    else
        sock = createTCPSocket(gSocketTCPPort);
    if (sock <= 0) {
        ALOGI("%s: Create socket failed!", __FUNCTION__);
        return;
    }

    // NO multitasking on PURPOSE
    while (gRunning) {
        sock_n = acceptSocket(sock);
        if (sock_n > 0) {
            audiostreamer_record_thread(&sock_n);
        }
    }
}

static int usage() {
    fprintf(stderr, "\nUsage: %s [-T <Port>] or [<-U>] or [-u <Name>]\n", gProgramName);
    fprintf(stderr,
            "\n"
            "-T: TCP socket (default port: %d)\n"
            "-U: Android control unix socket with name: %s\n"
            "-u: Unix socket (default: %s)\n",
            DEFAULT_SOCKET_TCP_PORT, DEFAULT_SOCKET_UNIX_NAME, "@" DEFAULT_SOCKET_UNIX_NAME);
    return 1;
}

int audio_main(int argc, char **argv) {
    gRunning = true;
    gProgramName = argv[0];
    int i = 1;

    if (argc < 2)
        return usage();
    while (i < argc) {
        if (strcmp(argv[i], "-T") == 0) {
            gUsingSocketAndroid = false;
            i++;
            if (i < argc) {
                gSocketTCPPort = atoi(argv[i]);
                i++;
            } else {
                gSocketTCPPort = DEFAULT_SOCKET_TCP_PORT;
            }
        } else if (strcmp(argv[i], "-U") == 0) {
            gUsingSocketAndroid = true;
            i++;
            if (i < argc)
                return usage();
        } else if (strcmp(argv[i], "-u") == 0) {
            gUsingSocketAndroid = false;
            gUsingSocketUnix = true;
            i++;
            if (i < argc) {
                gSocketName = strdup(argv[i]);
                i++;
            } else {
                gSocketName = strdup("@" DEFAULT_SOCKET_UNIX_NAME);
            }
        } else {
            return usage();
        }

        free(argv[i]);
    }

    audiostreamer_create_thread();
    return 0;
}


extern "C" jint Java_com_libremobileos_vncflinger_VncFlinger_startAudioStreamer(JNIEnv *env,
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

    return audio_main(argc, argv);
}

extern "C" void Java_com_libremobileos_vncflinger_VncFlinger_endAudioStreamer(JNIEnv *env,
                                                                             jobject thiz) {
    gRunning = false;
}