#pragma once
#include <functional>
#include <memory>
#include <string>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace hv {
class Buffer { public:
 std::vector<std::uint8_t> bytes;
 explicit Buffer(std::vector<std::uint8_t> value = {}): bytes(std::move(value)) {}
 void* data(){ return bytes.data(); } std::size_t size(){ return bytes.size(); }
};
class SocketChannel { public:
 bool connected=false;
 std::vector<std::vector<std::uint8_t>> writes;
 bool isConnected() const { return connected; }
 void write(const void* data,std::size_t size){ auto p=static_cast<const std::uint8_t*>(data); writes.emplace_back(p,p+size); }
 void close(bool){ connected=false; }
 std::string peeraddr(){return "mock";}
};
using SocketChannelPtr=std::shared_ptr<SocketChannel>;
class TcpClient { public:
 std::function<void(const SocketChannelPtr&)> onConnection;
 std::function<void(const SocketChannelPtr&,Buffer*)> onMessage;
 int createsocket(int,const char*){return 1;} void setConnectTimeout(int){} void start(){} void stop(){} void closesocket(){} bool isConnected(){return false;} void send(const void*,std::size_t){}
};
}
