#pragma once
#include "TcpClient.h"
namespace hv {
class TcpServer;
inline TcpServer* lastStartedServer=nullptr;
class TcpServer { public:
 std::function<void(const SocketChannelPtr&)> onConnection;
 std::function<void(const SocketChannelPtr&,Buffer*)> onMessage;
 int max_connections=0; std::string host;
 int createsocket(int,const char*){return 1;} void setThreadNum(int){} void start(){lastStartedServer=this;} void stop(){if(lastStartedServer==this)lastStartedServer=nullptr;}
}; }
