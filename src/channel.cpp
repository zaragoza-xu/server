#include "channel.h"

void Channel::handle_event()  {
    char ch;
    int n;
    std::string s;
    while((n = read(fd, &ch, 1)) > 0) {
        std::cout << ch;
    }
};

void ListenChannel::handle_event() {
    struct sockaddr_in sock_addr;
    socklen_t len = sizeof(sock_addr);
    int conn_fd = accept(fd, (sockaddr *)&sock_addr, &len);
    if (conn_fd < 0) {
        // accept failed; do not proceed with an invalid file descriptor
        return;
    }
    Channel* conn_chl = register_channel<Channel>(conn_fd, svr);

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.ptr = conn_chl;
    epoll_ctl(svr->get_epfd(), EPOLL_CTL_ADD, conn_fd, &event);
}