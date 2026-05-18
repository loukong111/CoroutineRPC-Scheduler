#pragma once
#include <memory>
#include <functional>

namespace corpcron {

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    using ReadCallback = std::function<void(const std::string& data)>;

    TcpConnection(int fd);
    ~TcpConnection();

    void start();
    void send(const std::string& data);
    void setReadCallback(ReadCallback cb);

    int fd() const { return fd_; }

private:
    int fd_;
    ReadCallback read_cb_;
    std::string read_buffer_;
    void handleRead();
};

} // namespace corpcron