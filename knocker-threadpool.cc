#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

static constexpr int TIMEOUT_MS = 2000;

static std::atomic<int> next_port;
static std::mutex out_mu;

static void worker(const sockaddr_in base, int end_port) {
  for (;;) {
    int port = next_port.fetch_add(1, std::memory_order_relaxed);
    if (port > end_port) {
      return;
    }

    int sock = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock < 0) {
      perror("socket");
      return;
    }

    sockaddr_in addr = base;
    addr.sin_port = htons(port);

    int res = ::connect(sock, (sockaddr *)&addr, sizeof(addr));
    bool open = false;
    if (res == 0) {
      open = true;
    } else if (errno == EINPROGRESS) {
      pollfd pfd{sock, POLLOUT, 0};
      if (::poll(&pfd, 1, TIMEOUT_MS) == 1) {
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);
        open = (err == 0);
      }
    }
    ::close(sock);

    if (open) {
      std::lock_guard<std::mutex> g(out_mu);
      std::cout << "Port " << port << " answered the door!\n";
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc != 4 && argc != 5) {
    std::cerr << "Usage: " << argv[0]
              << " <target-ip> <start-port> <end-port> [threads=500]\n";
    return EXIT_FAILURE;
  }

  const char *target_ip = argv[1];
  int start_port = std::stoi(argv[2]);
  int end_port = std::stoi(argv[3]);
  int nthreads = argc == 5 ? std::stoi(argv[4]) : 500;

  if (start_port < 1 || end_port > 65535 || start_port > end_port ||
      nthreads < 1) {
    std::cerr << "Invalid range of ports or thread count\n";
    return EXIT_FAILURE;
  }

  sockaddr_in base;
  memset(&base, 0, sizeof(base));
  base.sin_family = AF_INET;

  if (::inet_pton(AF_INET, target_ip, &base.sin_addr) <= 0) {
    std::cerr << "Invalid IP address\n";
    return EXIT_FAILURE;
  }

  std::cout << "Knocking on " << target_ip << " from port " << start_port
            << " to " << end_port << "\n";

  next_port = start_port;
  std::vector<std::thread> pool;
  pool.reserve(nthreads);
  for (int i = 0; i < nthreads; i++) {
    pool.emplace_back(worker, base, end_port);
  }
  for (auto &t : pool) {
    t.join();
  }
  return EXIT_SUCCESS;
}
