#include <AndroidSocket.h>
#include <network/UnixSocket.h>
#include <cutils/sockets.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stddef.h>

using namespace vncflinger;

AndroidListener::AndroidListener(const char *path) {

    fd = android_get_control_socket(path);
    if (fd < 0) {
		throw network::SocketException("unable to get Android control socket", EADDRNOTAVAIL);
	}

	listen(fd); 
}

AndroidListener::~AndroidListener()
{
}

network::Socket* AndroidListener::createSocket(int fd) {
  return new network::UnixSocket(fd);
}

int AndroidListener::getMyPort() {
  return 0;
}


// name example: @vncflinger
// first char (@) will be ignored!
AbsUnixListener::AbsUnixListener(const char *name)
{
	struct sockaddr_un addr;
	mode_t saved_umask;
	int result;

	if (strlen(name) >= sizeof(addr.sun_path))
		throw network::SocketException("socket path is too long", ENAMETOOLONG);

	// - Create a socket
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		throw network::SocketException("unable to create listening socket", errno);

	// - Attempt to bind to the requested path
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, name);
	addr.sun_path[0] = '\0';
	result = bind(fd, (struct sockaddr *)&addr, sizeof(sa_family_t) + strlen(name) * sizeof(char));
	if (result < 0) {
		close(fd);
		throw network::SocketException("unable to bind listening socket", result);
	}

	listen(fd);
}

AbsUnixListener::~AbsUnixListener()
{
	close(fd);
}

network::Socket* AbsUnixListener::createSocket(int fd) {
	return new network::UnixSocket(fd);
}

int AbsUnixListener::getMyPort() {
	return 0;
}