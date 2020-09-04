#include <sys/socket.h>

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio.hpp>

#define RESPONSE "HTTP/1.1 200 OK\nContent-Length: 12\n\nHello world!"

namespace {

using boost::asio::ip::tcp;

template <typename T> T parse_arg(const char *arg) {
  T value;
  const char *last = arg + std::strlen(arg);
  if (auto [ptr, ec] = std::from_chars(arg, last, value); ec != std::errc{}) {
    std::cerr << "Failed to parse '" << arg << "'\n";
    std::exit(1);
  }
  return value;
}

class session : public std::enable_shared_from_this<session> {
public:
  explicit session(tcp::socket socket) : socket_{std::move(socket)} {}

  void start() { do_read(); }

private:
  void do_read() {
    auto self{shared_from_this()};
    socket_.async_read_some(
        boost::asio::buffer(data_, max_length),
        [this, self](boost::system::error_code ec, std::size_t /*length*/) {
          if (!ec)
            do_write();
        });
  }

  void do_write() {
    auto self{shared_from_this()};
    boost::asio::async_write(
        socket_, boost::asio::const_buffer(RESPONSE, sizeof(RESPONSE) - 1),
        [this, self](boost::system::error_code ec, std::size_t num_written) {
          if (!ec) {
            if (num_written != sizeof(RESPONSE) - 1) {
              std::cerr << "Writing to socket failed\n";
              std::exit(1);
            }

            do_read();
          }
        });
  }

  tcp::socket socket_;
  enum { max_length = 1024 };
  char data_[max_length];
};

class server {
public:
  explicit server(boost::asio::io_context &io_context,
                  const boost::asio::ip::address &address, unsigned short port)
      : acceptor_{io_context} {
    auto endpoint = tcp::endpoint{address, port};
    acceptor_.open(endpoint.protocol());

    int value{1};
    if (setsockopt(acceptor_.native_handle(), SOL_SOCKET, SO_REUSEPORT, &value,
                   sizeof(value)) != 0) {
      std::perror("Setting SO_REUSEPORT failed");
      std::exit(1);
    }

    acceptor_.bind(endpoint);
    acceptor_.listen();
    do_accept();
  }

private:
  void do_accept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
          if (!ec) {
            std::make_shared<session>(std::move(socket))->start();
          }

          do_accept();
        });
  }

  tcp::acceptor acceptor_;
};

void worker(const boost::asio::ip::address &address, unsigned short port) {
  boost::asio::io_context io_context{};
  server s{io_context, address, port};
  io_context.run();
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc != 4) {
    std::cerr << "Usage: " << argv[0] << " <HOST-IPV4> <PORT> <NUM-THREADS>\n";
    return 1;
  }

  auto host = boost::asio::ip::make_address(argv[1]);
  auto port = parse_arg<unsigned short>(argv[2]);
  auto num_threads = parse_arg<std::size_t>(argv[3]);

  std::vector<std::thread> threads;
  threads.reserve(num_threads);

  for (std::size_t i = 0; i < num_threads; i++)
    threads.push_back(std::thread{&worker, host, port});

  for (auto &thread : threads)
    thread.join();
}
