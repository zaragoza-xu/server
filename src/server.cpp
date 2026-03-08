#include "server.h"

#include <cerrno>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <err.h>

#include <memory>

#include "channel.h" 

MyServer::MyServer() = default;
void MyServer::start_server() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        return;
    }
    Channel* lsn_chl = register_channel<ListenChannel>(listen_fd, shared_from_this());

    epfd = epoll_create(FDSIZE);
    if(epfd == -1)
        err(errno, "epoll create");
    struct epoll_event ep_event;
    ep_event.events = EPOLLET | EPOLLIN;
    ep_event.data.ptr = lsn_chl;
    epoll_ctl(epfd, EPOLL_CTL_ADD, lsn_chl->getfd(), &ep_event);

    struct sockaddr_in sock_addr;
    sock_addr.sin_port = htons(SVR_PORT);
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(lsn_chl->getfd(), (sockaddr *)&sock_addr, sizeof(sock_addr)) == -1)
        err(errno, "bind");
    listen(lsn_chl->getfd(), SOMAXCONN);

    while(1) {
        struct epoll_event events[20];
        int nfds = epoll_wait(epfd, events, 20, -1);
        if(nfds == -1)
            break;
        for(int i = 0; i < nfds; i ++) {
            static_cast<Channel*>(events[i].data.ptr)->handle_event();
        }
    }

}