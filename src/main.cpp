#include "server.h"

int main() {
    auto server = std::make_shared<MyServer>();
    server->start_server();
    return 0;
}