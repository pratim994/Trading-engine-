// TCP ownership/lifetime tests over loopback: accepted sockets are owned by the server, closed on disconnect, and nothing leaks.
#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstdio>
#include <cstring>
#include <string>

#include "common/tcp_server.h"

using namespace Common;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static int openFdCount() {
  int n = 0;
  if (DIR *d = opendir("/proc/self/fd")) { while (readdir(d)) ++n; closedir(d); }
  return n;
}

static int connectLoopback(int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port); inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
  if (connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0) { close(fd); return -1; }
  return fd;
}

int main() {
  constexpr int port = 23456;
  Logger logger("/tmp/tcp_server_test.log");
  const int baseline_fds = openFdCount();
  size_t bytes_received = 0;

  {
    TCPServer server(logger);
    size_t disconnects = 0;
    server.recv_callback_ = [&](TCPSocket *s, Nanos) { bytes_received += s->next_rcv_valid_index_; s->next_rcv_valid_index_ = 0; };
    server.recv_finished_callback_ = []() {};
    server.disconnect_callback_ = [&](TCPSocket *) { ++disconnects; };
    server.listen("lo", port);

    auto pump = [&](int rounds) { for (int i = 0; i < rounds; ++i) { server.poll(); server.sendAndRecv(); usleep(2000); } };

    // Data flows and the server reads it.
    int c = connectLoopback(port);
    CHECK(c >= 0);
    pump(20);
    CHECK(server.owned_sockets_.size() == 1);
    const char msg[] = "hello exchange";
    CHECK(send(c, msg, sizeof(msg), 0) == static_cast<ssize_t>(sizeof(msg)));
    pump(20);
    CHECK(bytes_received == sizeof(msg));

    // Orderly peer close: socket is detected, owner is notified, container + fd are released.
    close(c);
    pump(20);
    CHECK(server.owned_sockets_.empty() && server.receive_sockets_.empty());
    CHECK(disconnects == 1);

    // Repeated connect/disconnect cycles must not accumulate sockets or file descriptors.
    const int fds_before = openFdCount();
    for (int i = 0; i < 10; ++i) {
      int x = connectLoopback(port);
      CHECK(x >= 0);
      pump(10);
      close(x);
      pump(10);
    }
    CHECK(server.owned_sockets_.empty());
    CHECK(disconnects == 11);
    CHECK(openFdCount() == fds_before);

    // A client that disconnects while the server still has data to flush must not crash it (EPIPE/ECONNRESET path, MSG_NOSIGNAL).
    c = connectLoopback(port);
    pump(10);
    CHECK(server.owned_sockets_.size() == 1);
    server.owned_sockets_[0]->send(msg, sizeof(msg));
    close(c);
    pump(30);
    CHECK(server.owned_sockets_.empty());
  }

  // After the server is destroyed (listener, epoll fd, any live client sockets) every descriptor is back to the baseline.
  CHECK(openFdCount() == baseline_fds);

  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
