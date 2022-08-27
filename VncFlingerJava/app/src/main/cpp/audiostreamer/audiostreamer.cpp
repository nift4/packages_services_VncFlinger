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

using namespace android;

#define kBufferOutputSize (1452)
#define READ_AUDIO_MAX 2048
#define DEFAULT_SOCKET_TCP_PORT (9200)
#define DEFAULT_SOCKET_UNIX_NAME "audiostreamer"

static char *gProgramName;
static int gSocketTCPPort = DEFAULT_SOCKET_TCP_PORT;
static bool gUsingSocketUnix = false;
sp<AudioRecord> pAudioRecord = NULL;

uint32_t sampleRate = 48000;
int channel = 2;

using android::content::AttributionSourceState;

static int audiostreamer_init() {
    size_t framecount = 0;

    AudioRecord::getMinFrameCount(&framecount, sampleRate, AUDIO_FORMAT_PCM_16_BIT, audio_channel_in_mask_from_count(channel));
    ALOGI("%s: sampleRate: %d, channel: %d framecount: %d", __FUNCTION__, sampleRate, channel, (int)framecount);

    AttributionSourceState attributionSource;
    attributionSource.packageName = "audiostreamer";
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

void *audiostreamer_record_thread(void *arg) {
    int err = 0;
    char *pReadBuf = NULL;
    int iReadLen = 0;
    int nSock = 0;

    if (NULL != arg) {
        nSock = *((int *)arg);
        ALOGI("%s: nSock:%d", __FUNCTION__, nSock);
    } else {
        ALOGI("%s: NULL==arg, return.", __FUNCTION__);
        return NULL;
    }

    if (pAudioRecord == NULL) {
        err = audiostreamer_init();
        if (err != 0) {
            ALOGE("%s: create AudioRecord failed", __FUNCTION__);
            return NULL;
        }
    }

    pReadBuf = (char *) malloc(READ_AUDIO_MAX);
    if (pReadBuf == NULL) {
        ALOGE("%s: Failed to allocate memory", __FUNCTION__);
        return NULL;
    }

    pAudioRecord->start();
    while (1) {
        memset(pReadBuf, 0, READ_AUDIO_MAX);

        iReadLen = pAudioRecord->read(pReadBuf, kBufferOutputSize);
        if (iReadLen <= 0) {
            ALOGE("%s: pAudioRecord->read failed", __FUNCTION__);
            continue;
        }

        sendDataSocket(nSock, pReadBuf, iReadLen, 0);
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
    return NULL;
}

void *audiostreamer_create_thread(void *) {
    int err = 0;
    int sock = 0;
    int sock_n = 0;

    pthread_t tid_audio_record;
    memset(&tid_audio_record, 0, sizeof(pthread_t));

    pthread_detach(pthread_self());

    if (gUsingSocketUnix)
        sock = createUnixSocket(DEFAULT_SOCKET_UNIX_NAME);
    else
        sock = createTCPSocket(gSocketTCPPort);
    if (sock <= 0) {
        ALOGI("%s: Create socket failed!", __FUNCTION__);
        return NULL;
    }

    sock_n = acceptSocket(sock);
    if (sock_n > 0) {
        err = pthread_create(&tid_audio_record, NULL, audiostreamer_record_thread, &sock_n);
        if (err != 0)
            ALOGE("Failed to create recorder thread");
    }

    if (tid_audio_record)
        pthread_join(tid_audio_record, NULL);

    pthread_exit(0);

    return NULL;
}

static void usage() {
    fprintf(stderr, "\nUsage: %s [-T <Port>] or [<-U>]\n", gProgramName);
    fprintf(stderr,
            "\n"
            "-T: TCP socket (default port: %d)\n"
            "-U: Unix socket with name: %s\n",
            DEFAULT_SOCKET_TCP_PORT, DEFAULT_SOCKET_UNIX_NAME);
    exit(1);
}

int main(int argc, char **argv) {
    gProgramName = argv[0];
    pthread_t tid_audio_srv = 0;
    int i = 1;

    if (argc < 2)
        usage();
    while (i < argc) {
        if (strcmp(argv[i], "-T") == 0) {
            gUsingSocketUnix = false;
            i++;
            if (i < argc) {
                gSocketTCPPort = atoi(argv[i]);
                i++;
            } else {
                gSocketTCPPort = DEFAULT_SOCKET_TCP_PORT;
            }
        } else if (strcmp(argv[i], "-U") == 0) {
            gUsingSocketUnix = true;
            i++;
            if (i < argc)
                usage();
        } else {
            usage();
        }
    }

    int err = pthread_create(&tid_audio_srv, NULL, audiostreamer_create_thread, NULL);
    if (err != 0)
        ALOGE("Failed to create thread");

    while (1)
        sleep(5);

    return 0;
}
