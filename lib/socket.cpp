#include "inspector/socket.h"

#ifdef WIN32
#include <afunix.h>  // For Unix domain sockets on Windows
#include <winsock.h> // For socket(), connect(), send(), and recv()
typedef int socklen_t;
typedef char raw_type; // Type used for raw data on this platform
#else
#include <sys/socket.h> // For socket(), connect(), send(), and recv()
#include <sys/types.h>  // For data types
#include <sys/un.h>     // For Unix domain sockets
#include <unistd.h>     // For close()
typedef void raw_type; // Type used for raw data on this platform
#endif

#include <cstring>
#include <errno.h> // For errno
#include <filesystem>

using namespace std;

#ifdef WIN32
static bool initialized = false;
#endif

// SocketException Code

SocketException::SocketException(const string &message,
                                 bool inclSysMsg) noexcept
    : userMessage(message) {
  if (inclSysMsg) {
    userMessage.append(": ");
    userMessage.append(strerror(errno));
  }
}

SocketException::~SocketException() noexcept {}

const char *SocketException::what() const noexcept {
  return userMessage.c_str();
}

// Socket Code

Socket::Socket() THROW(SocketException) {
#ifdef WIN32
  if (!initialized) {
    WORD wVersionRequested;
    WSADATA wsaData;

    wVersionRequested = MAKEWORD(2, 0);                 // Request WinSock v2.0
    if (WSAStartup(wVersionRequested, &wsaData) != 0) { // Load WinSock DLL
      throw SocketException("Unable to load WinSock DLL");
    }
    initialized = true;
  }
#endif
  sockDesc = -1; // Will be set by derived classes
}

Socket::Socket(int sockDesc) { this->sockDesc = sockDesc; }

Socket::~Socket() {
#ifdef WIN32
  ::closesocket(sockDesc);
#else
  ::close(sockDesc);
#endif
  sockDesc = -1;
}

void Socket::cleanUp() THROW(SocketException) {
#ifdef WIN32
  if (WSACleanup() != 0) {
    throw SocketException("WSACleanup() failed");
  }
#endif
}

// CommunicatingSocket Code

CommunicatingSocket::CommunicatingSocket() THROW(SocketException) : Socket() {}

CommunicatingSocket::CommunicatingSocket(int newConnSD) : Socket(newConnSD) {}

void CommunicatingSocket::send(const void *buffer, int bufferLen)
    THROW(SocketException) {
  if (::send(sockDesc, (raw_type *)buffer, bufferLen, 0) < 0) {
    throw SocketException("Send failed (send())", true);
  }
}

int CommunicatingSocket::recv(void *buffer, int bufferLen)
    THROW(SocketException) {
  int rtn;
  if ((rtn = ::recv(sockDesc, (raw_type *)buffer, bufferLen, 0)) < 0) {
    throw SocketException("Received failed (recv())", true);
  }

  return rtn;
}

// UnixSocket Code

UnixSocket::UnixSocket() THROW(SocketException) : CommunicatingSocket() {
  // Create socket with Unix domain
  if ((sockDesc = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    throw SocketException("Unix socket creation failed (socket())", true);
  }
}

UnixSocket::UnixSocket(const std::filesystem::path &socketPath)
    THROW(SocketException)
    : CommunicatingSocket() {
  // Create socket with Unix domain
  if ((sockDesc = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    throw SocketException("Unix socket creation failed (socket())", true);
  }
  connectUnix(socketPath);
}

UnixSocket::UnixSocket(int newConnSD) : CommunicatingSocket(newConnSD) {}

void UnixSocket::connectUnix(const std::filesystem::path &socketPath)
    THROW(SocketException) {
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;

  std::string pathStr = socketPath.string();
  if (pathStr.length() >= sizeof(addr.sun_path)) {
    throw SocketException("Socket path too long");
  }

  strncpy(addr.sun_path, pathStr.c_str(), sizeof(addr.sun_path) - 1);

  if (::connect(sockDesc, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    throw SocketException("Connect to Unix socket failed (connect())", true);
  }
}
