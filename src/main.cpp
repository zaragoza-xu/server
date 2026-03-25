#include "server.h"
#include <asio/io_context.hpp>
#include <exception>
#include <httplib.h>
#include <iostream>

int main() {
  try {
    asio::io_context io_context;
    Server server(io_context);

    io_context.run();

  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}