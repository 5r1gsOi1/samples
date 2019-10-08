
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
#ifdef DEBUG_
      std::cerr << "asked to stop politely" << std::endl;
#endif
    } else {
      g_EchoServer.StopImmediately();
#ifdef DEBUG_
      std::cerr << "terminated server" << std::endl;
#endif
      exit(0);
    }
  }
}

int main() {
#ifdef DEBUG_
  std::cerr << "echo server start" << std::endl;
#endif
  signal(SIGSEGV, terminate);
  signal(SIGTERM, terminate);
  g_EchoServer.start();
}
