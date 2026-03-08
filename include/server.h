#pragma once
#include <memory>
#include <vector>

class Channel;

class MyServer : public std::enable_shared_from_this<MyServer> {
private: 
    #define FDSIZE 1024
    #define SVR_PORT 7777

    int epfd{-1};    
public:
    MyServer();
    ~MyServer();
    std::vector<std::unique_ptr<Channel> > channels;
    void start_server();
    int get_epfd() { return epfd; }
};