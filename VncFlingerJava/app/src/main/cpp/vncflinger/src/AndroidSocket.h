#ifndef __ANDROID_SOCKET_H__
#define __ANDROID_SOCKET_H__

#include <network/Socket.h>

namespace vncflinger {

class AndroidListener : public network::SocketListener {
  public:
    AndroidListener(const char *name);
    virtual ~AndroidListener();

    int getMyPort();

  protected:
    virtual network::Socket* createSocket(int fd);
  };

  class AbsUnixListener : public network::SocketListener {
    public:
        AbsUnixListener(const char *name);
        virtual ~AbsUnixListener();

        int getMyPort();

    protected:
        virtual network::Socket* createSocket(int fd);
    };

}

#endif
