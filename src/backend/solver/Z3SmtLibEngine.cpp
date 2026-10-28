#include "z3_smtlib_engine.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace depvec {
namespace {

[[noreturn]] void throwSystemError(const char *operation) {
  throw std::runtime_error(std::string(operation) + ": " +
                           std::strerror(errno));
}

void closeIfOpen(int descriptor) {
  if (descriptor >= 0)
    close(descriptor);
}

void sendAll(int socket, std::string_view data) {
  std::size_t written = 0;
  while (written < data.size()) {
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    const ssize_t count =
        send(socket, data.data() + written, data.size() - written, flags);
    if (count < 0) {
      if (errno == EINTR)
        continue;
      throwSystemError("writing SMT-LIB command to Z3");
    }
    if (count == 0)
      throw std::runtime_error("Z3 closed its input unexpectedly");
    written += static_cast<std::size_t>(count);
  }
}

bool isMarkerCharacter(char character) {
  return (character >= 'a' && character <= 'z') ||
         (character >= 'A' && character <= 'Z') ||
         (character >= '0' && character <= '9') || character == '_' ||
         character == '-';
}

} // namespace

Z3SmtLibEngine::Z3SmtLibEngine(const std::string &executable) {
  int sockets[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0)
    throwSystemError("creating Z3 communication socket");

#ifndef MSG_NOSIGNAL
  int enabled = 1;
  if (setsockopt(sockets[0], SOL_SOCKET, SO_NOSIGPIPE, &enabled,
                 sizeof(enabled)) < 0) {
    const int savedError = errno;
    closeIfOpen(sockets[0]);
    closeIfOpen(sockets[1]);
    errno = savedError;
    throwSystemError("configuring Z3 communication socket");
  }
#endif

  const pid_t child = fork();
  if (child < 0) {
    const int savedError = errno;
    closeIfOpen(sockets[0]);
    closeIfOpen(sockets[1]);
    errno = savedError;
    throwSystemError("starting Z3");
  }

  if (child == 0) {
    closeIfOpen(sockets[0]);
    if (dup2(sockets[1], STDIN_FILENO) < 0 ||
        dup2(sockets[1], STDOUT_FILENO) < 0)
      _exit(126);
    closeIfOpen(sockets[1]);

    std::vector<std::string> arguments{executable, "-in", "-smt2"};
    std::vector<char *> argumentPointers;
    argumentPointers.reserve(arguments.size() + 1);
    for (std::string &argument : arguments)
      argumentPointers.push_back(argument.data());
    argumentPointers.push_back(nullptr);
    execvp(executable.c_str(), argumentPointers.data());
    _exit(127);
  }

  closeIfOpen(sockets[1]);
  socket_ = sockets[0];
  processId_ = static_cast<int>(child);
}

Z3SmtLibEngine::~Z3SmtLibEngine() {
  closeIfOpen(socket_);
  if (processId_ >= 0) {
    int status = 0;
    while (waitpid(processId_, &status, 0) < 0 && errno == EINTR) {
    }
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): fixed by SmtLibEngine.
std::string Z3SmtLibEngine::execute(std::string_view commands,
                                    std::string_view marker) {
  if (socket_ < 0)
    throw std::runtime_error("Z3 process is not available");
  if (marker.empty())
    throw std::invalid_argument("SMT-LIB response marker cannot be empty");
  for (char character : marker) {
    if (!isMarkerCharacter(character))
      throw std::invalid_argument("SMT-LIB response marker contains an "
                                  "unsupported character");
  }

  std::string request(commands);
  if (request.empty() || request.back() != '\n')
    request.push_back('\n');
  request += "(echo \"";
  request.append(marker);
  request += "\")\n";
  sendAll(socket_, request);

  for (;;) {
    const std::size_t markerPosition = outputBuffer_.find(marker);
    if (markerPosition != std::string::npos) {
      std::string response = outputBuffer_.substr(0, markerPosition);
      std::size_t afterMarker = markerPosition + marker.size();
      while (afterMarker < outputBuffer_.size() &&
             (outputBuffer_[afterMarker] == '\r' ||
              outputBuffer_[afterMarker] == '\n'))
        ++afterMarker;
      outputBuffer_.erase(0, afterMarker);
      return response;
    }

    char buffer[4096];
    const ssize_t count = read(socket_, buffer, sizeof(buffer));
    if (count < 0) {
      if (errno == EINTR)
        continue;
      throwSystemError("reading SMT-LIB response from Z3");
    }
    if (count == 0)
      throw std::runtime_error(
          "Z3 closed stdout before the SMT-LIB response marker; "
          "check that the executable is available and accepts -in -smt2");
    outputBuffer_.append(buffer, static_cast<std::size_t>(count));
  }
}

} // namespace depvec
