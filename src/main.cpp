#include <exception>
#include <iostream>

#include <httplib.h>
#include <asio/io_context.hpp>

#include "server.h"

int main() {
  try {
    asio::io_context io_context;
    Server server(io_context, 8765);

    io_context.run();

  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}