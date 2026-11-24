#pragma once

#include <memory>

#include "tcp_socket.h"

namespace Common {
  struct TCPServer {
    explicit TCPServer(Logger &logger)
        : listener_socket_(logger), logger_(logger) {
    }

    ~TCPServer() {
      if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
      }
    }

    TCPServer(const TCPServer &) = delete;

    TCPServer &operator=(const TCPServer &) = delete;

    /// Start listening for connections on the provided interface and port.
    auto listen(const std::string &iface, int port) -> void;

    /// Check for new connections or dead connections and update containers that track the sockets.
    auto poll() noexcept -> void;

    /// Publish outgoing data from the send buffer and read incoming data from the receive buffer.
    auto sendAndRecv() noexcept -> void;

  private:
    /// Add and remove socket file descriptors to and from the EPOLL list.
    auto addToEpollList(TCPSocket *socket);

    /// Remove sockets whose peer disconnected (or errored) from epoll and the tracking containers, notify the owner and free them.
    auto reapClosedSockets() noexcept -> void;

  public:
    /// Socket on which this server is listening for new connections on.
    int epoll_fd_ = -1;
    TCPSocket listener_socket_;

    epoll_event events_[1024];

    /// Collection of all sockets, sockets for incoming data, sockets for outgoing data and dead connections.
    std::vector<TCPSocket *> receive_sockets_, send_sockets_;

    /// Owns every accepted connection; receive_sockets_ / send_sockets_ are non-owning views into this.
    std::vector<std::unique_ptr<TCPSocket>> owned_sockets_;

    /// Called just before a disconnected socket is destroyed so the owner can drop any pointer it holds to it.
    std::function<void(TCPSocket *s)> disconnect_callback_ = nullptr;

    /// Function wrapper to call back when data is available.
    std::function<void(TCPSocket *s, Nanos rx_time)> recv_callback_ = nullptr;
    /// Function wrapper to call back when all data across all TCPSockets has been read and dispatched this round.
    std::function<void()> recv_finished_callback_ = nullptr;

    std::string time_str_;
    Logger &logger_;
  };
}
