#pragma once

#include "inspector/common.h"

#include <exception>  // For exception class
#include <filesystem> // For filesystem paths
#include <string>     // For string

using namespace std;

/**
 *   Signals a problem with the execution of a socket call.
 */
class SocketException : public exception {
public:
  /**
   *   Construct a SocketException with a explanatory message.
   *   @param message explanatory message
   *   @param incSysMsg true if system message (from strerror(errno))
   *   should be postfixed to the user provided message
   */
  SocketException(const string &message, bool inclSysMsg = false) noexcept;

  /**
   *   Provided just to guarantee that no exceptions are thrown.
   */
  ~SocketException() noexcept;

  /**
   *   Get the exception message
   *   @return exception message
   */
  const char *what() const noexcept;

private:
  string userMessage; // Exception message
};

/**
 *   Base class representing basic communication endpoint
 */
class Socket {
public:
  /**
   *   Close and deallocate this socket
   */
  ~Socket();

  /**
   *   If WinSock, unload the WinSock DLLs; otherwise do nothing.
   *   @exception SocketException thrown WinSock clean up fails
   */
  static void cleanUp() THROW(SocketException);

private:
  // Prevent the user from trying to use value semantics on this object
  Socket(const Socket &sock);
  void operator=(const Socket &sock);

protected:
  int sockDesc; // Socket descriptor
  Socket() THROW(SocketException);
  Socket(int sockDesc);
};

/**
 *   Socket which is able to connect, send, and receive
 */
class CommunicatingSocket : public Socket {
public:
  /**
   *   Write the given buffer to this socket.  Call connect() before
   *   calling send()
   *   @param buffer buffer to be written
   *   @param bufferLen number of bytes from buffer to be written
   *   @exception SocketException thrown if unable to send data
   */
  void send(const void *buffer, int bufferLen) THROW(SocketException);

  /**
   *   Read into the given buffer up to bufferLen bytes data from this
   *   socket.  Call connect() before calling recv()
   *   @param buffer buffer to receive the data
   *   @param bufferLen maximum number of bytes to read into buffer
   *   @return number of bytes read, 0 for EOF, and -1 for error
   *   @exception SocketException thrown if unable to receive data
   */
  int recv(void *buffer, int bufferLen) THROW(SocketException);

protected:
  CommunicatingSocket() THROW(SocketException);
  CommunicatingSocket(int newConnSD);
};

/**
 *   Unix domain socket for local communication
 */
class UnixSocket : public CommunicatingSocket {
public:
  /**
   *   Construct a Unix socket with no connection
   *   @exception SocketException thrown if unable to create Unix socket
   */
  UnixSocket() THROW(SocketException);

  /**
   *   Construct a Unix socket with a connection to the given socket path
   *   @param socketPath path to the Unix socket
   *   @exception SocketException thrown if unable to create Unix socket
   */
  UnixSocket(const std::filesystem::path &socketPath) THROW(SocketException);

  /**
   *   Connect to a Unix socket at the given path
   *   @param socketPath path to the Unix socket
   *   @exception SocketException thrown if unable to connect
   */
  void connectUnix(const std::filesystem::path &socketPath)
      THROW(SocketException);

private:
  UnixSocket(int newConnSD);
};
