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

#define LOG_TAG "audiostreamer-socket"

#include <arpa/inet.h>
#include <cutils/log.h>
#include <cutils/sockets.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_PAYLOAD_LENGTH 4096

static bool canSend(int sock, int timeout) {
    timeval tv;
    fd_set fds;

    if (timeout >= 1000) {
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout - tv.tv_sec * 1000) * 1000;
    } else {
        tv.tv_sec = 0;
        tv.tv_usec = timeout * 1000;
    }

    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    if (select(sock + 1, 0, &fds, NULL, &tv) > 0)
        if (FD_ISSET(sock, &fds))
            return true;

    return false;
}

int createUnixSocket(const char *name) {
    bool success = false;
    int sock = 0;
    do {
        sock = android_get_control_socket(name);
        if (sock <= 0) {
            ALOGI("createUnixSocket socket failed: %d\n", errno);
            break;
        }

        if (listen(sock, 1) != 0) {
            ALOGI("createUnixSocket socket listen: %d\n", errno);
            break;
        }

        success = true;
    } while (0);

    if (!success && sock > 0) {
        close(sock);
        sock = 0;
    }

    return sock;
}

int createTCPSocket(int port) {
    bool success = false;
    int sock = 0;
    const int flag = 1;
    struct sockaddr_in addr;

    do {
        sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock <= 0) {
            ALOGI("createTCPSocket socket failed: %d\n", errno);
            break;
        }

        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&flag, sizeof(int)) < 0) {
            ALOGI("createTCPSocket setsockopt failed: %d\n", errno);
        }

        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);

        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
            ALOGI("createTCPSocket socket bind: %d\n", errno);
            break;
        }

        if (listen(sock, 1) != 0) {
            ALOGI("createTCPSocket socket listen: %d\n", errno);
            break;
        }

        success = true;
    } while (0);

    if (!success && sock > 0) {
        close(sock);
        sock = 0;
    }

    return sock;
}

void closeSocket(int sock) {
    if (sock > 0) {
        shutdown(sock, SHUT_RDWR);
        close(sock);
        sock = 0;
    }
}

int acceptSocket(int sock) {
    struct sockaddr_in client;
    socklen_t client_length = sizeof(client);
    int s = accept(sock, (struct sockaddr *)&client, &client_length);

    return s;
}

bool sendDataSocket(int sock, void *data, int length, int timeout) {
    bool success = false;
    unsigned char *ptr = (unsigned char *)data;
    int count = length;
    int size;

    do {
        if (timeout > 0)
            if (!canSend(sock, timeout))
                break;

        size = count;

        int bytes = send(sock, ptr, size, 0);
        if (bytes > 0) {
            ptr += bytes;
            count -= bytes;

            if (0 == count) {
                success = true;
                break;
            }
        } else {
            break;
        }

    } while (1);

    return success;
}
