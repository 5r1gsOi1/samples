
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <thread>
#include <vector>

constexpr int kPort{7777};
constexpr size_t kBufferSize{1024};
volatile bool g_IsRunning{false};

void SendToServer() {
  int sock{};
  sockaddr_in serv_addr{AF_INET, htons(kPort)};
  const char request[] = "test request to server";
  char buffer[kBufferSize]{};
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    throw std::runtime_error{"socket() failed"};
  }

  if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
    throw std::runtime_error{"inet_pton() failed"};
  }

  if (connect(sock, reinterpret_cast<struct sockaddr *>(&serv_addr),
              sizeof(serv_addr)) < 0) {
    throw std::runtime_error{"connect() failed"};
  }
  ssize_t sent{send(sock, request, strlen(request), 0)};

  if (sent <= 0) {
    throw std::runtime_error{"sent <= 0"};
  }

  ssize_t read_size = read(sock, buffer, kBufferSize);
  bool success{read_size + 1 == sizeof request and
               0 == strncmp(request, buffer, sizeof request)};

#ifdef DEBUG_
  std::cerr << "sent = " << sent << ", received = " << read_size << ", "
            << success << std::endl;
#endif

  if (not success) {
    throw std::runtime_error{"answer from server is different"};
  }

  shutdown(sock, SHUT_RDWR);
  close(sock);
}

void ThreadMain() {
  while (g_IsRunning) {
    SendToServer();
  }
}

int main() {
  g_IsRunning = true;
  std::vector<std::thread> threads;

  for (int i{}; i < 4; ++i) {
    threads.emplace_back(ThreadMain);
  }

  using namespace std::chrono_literals;
  std::this_thread::sleep_for(5s);

  g_IsRunning = false;

  for (auto &t : threads) {
    t.join();
  }
}
