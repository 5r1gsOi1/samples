
#include <signal.h>

#include <iostream>

#include "server.h"

Server<EchoHandler<1024>> g_EchoServer{7777, 1000, 4};

void terminate(int signal) {
  if (signal == SIGTERM) {
    static int polite_ask{};
    if (polite_ask < 3) {
      ++polite_ask;
      g_EchoServer.StopPolitely();
      std::cerr << "asked to stop politely" << std::endl;
    } else {
      g_EchoServer.StopImmediately();
      std::cerr << "terminated server" << std::endl;
      exit(0);
    }
  }
}

int main() {
  std::cerr << "echo server start" << std::endl;
  signal(SIGSEGV, terminate);
  signal(SIGTERM, terminate);
  g_EchoServer.start();
}
