#include <exception>
#include <memory>

#include <asio/io_context.hpp>
#include <httplib.h>

#include "logging.h"
#include "server.h"

int main() {
  try {
    asio::io_context io_context;
    auto server = std::make_shared<Server>(io_context, 8765);
    (void)server;
    logging::log("Server listening on port 8765...");

    io_context.run();

  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}