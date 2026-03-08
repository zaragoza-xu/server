#pragma once

#include <strings.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

#include <memory>
#include <iostream>
#include <concepts>
#include <string>
#include <cstring>

#include "server.h"

class Channel {
protected:
    int fd;
    public:
    std::shared_ptr<MyServer> svr;
    Channel() = default;
    explicit Channel(int fd): fd(fd) {}

    virtual void handle_event();
    int getfd() { return fd; }

    virtual ~Channel() = default;
};

template <std::derived_from<Channel> T>
T* register_channel(int fd, std::shared_ptr<MyServer> svr);

class ListenChannel : public Channel {
public:
    explicit ListenChannel(int fd) : Channel(fd) {}
    void handle_event() override;
};

template <std::derived_from<Channel> T>
T* register_channel(int fd, std::shared_ptr<MyServer> svr) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    auto chl = std::make_unique<T>(fd);
    chl->svr = std::move(svr);
    T* ptr = chl.get();
    ptr->svr->channels.push_back(std::move(chl));
    return ptr;
}
