
#pragma once

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "jobs_pool.h"

template <size_t BufferSize>
struct EchoHandler {
  int sock{};
  struct sockaddr_in client_address {};
  std::array<unsigned char, BufferSize> buffer{};

  EchoHandler(const int sock_, const struct sockaddr_in &client_addr)
      : sock{sock_}, client_address{client_addr} {}

  void perform(int thread_number) {
    (void)thread_number;
#ifdef DEBUG_
    std::cerr << "INCOMING [" << thread_number
              << "]: port = " << client_address.sin_port;
#endif
    while (true) {
      ssize_t received{recv(sock, buffer.data(), buffer.size(), MSG_DONTWAIT)};
      if (received < 0) {
        if (errno == EAGAIN) {
          continue;
        } else {
          break;
        }
      } else if (received == 0) {
        break;
      }
#ifdef DEBUG_
      std::cerr << "\t received = " << received;
#endif
      ssize_t sent{send(sock, buffer.data(), static_cast<size_t>(received),
                        MSG_DONTWAIT)};
      if (sent <= 0) {
        break;
      }
#ifdef DEBUG_
      std::cerr << ", sent = " << sent;
#endif
    }
#ifdef DEBUG_
    std::cerr << "  END" << std::endl;
#endif
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(1ms);
    shutdown(sock, SHUT_RDWR);
    close(sock);
  }
};

template <class ConnectionHandler>
class Server {
 public:
  Server() = delete;
  Server(const int port_number, const int queue_size,
         const int number_of_handlers)
      : port_{port_number},
        queue_size_{queue_size},
        pool_{new JobsPool<ConnectionHandler>{number_of_handlers}} {
    PrepareSocket();
  }
  ~Server() {
    if (pool_) {
      pool_->FinishAvailableJobs();
    }
    StopPolitely();
    CloseSocket();
  }

  void StopImmediately() {
    if (pool_) {
      pool_->AbandonJobsAndStop();
    }
    StopPolitely();
    CloseSocket();
  }

  void StopPolitely() { shutdown(socket_, SHUT_RDWR); }

  void start() { AcceptLoop(); }

 private:
  void AcceptLoop() {
    if (socket_is_opened_) {
      struct sockaddr_in client_address {};
      socklen_t address_size = sizeof(client_address);
      while (true) {
        int new_socket{accept(
            socket_, reinterpret_cast<struct sockaddr *>(&client_address),
            &address_size)};
        if (new_socket > 0) {
          if (pool_) {
            pool_->AddJob(ConnectionHandler{new_socket, client_address});
          }
        } else {
          break;
        }
      }
    }
  }

  void PrepareSocket() {
    if (socket_is_opened_) {
      close(socket_);
      socket_ = 0;
    }
    socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ == 0) {
      throw std::runtime_error{"socket() failed"};
    }
    int optval{1};

    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optval,
                   sizeof(optval))) {
      throw std::runtime_error{"setsockopt() failed"};
    }
    address_.sin_family = AF_INET;
    address_.sin_addr.s_addr = INADDR_ANY;
    address_.sin_port = htons(port_);

    if (bind(socket_, reinterpret_cast<struct sockaddr *>(&address_),
             sizeof(address_)) < 0) {
      throw std::runtime_error{"bind() failed"};
    }

    if (listen(socket_, queue_size_) < 0) {
      throw std::runtime_error{"listen() failed"};
    }
    socket_is_opened_ = true;
  }

  void CloseSocket() {
    if (socket_is_opened_) {
      close(socket_);
      socket_ = 0;
      socket_is_opened_ = false;
    }
  }

  bool socket_is_opened_{false};
  int socket_{};
  struct sockaddr_in address_ {};
  int port_{}, queue_size_{};
  std::unique_ptr<JobsPool<ConnectionHandler>> pool_{};
};
