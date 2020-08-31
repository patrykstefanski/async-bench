#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <system_error>

#include <fev/fev++.hpp>

#define RESPONSE "HTTP/1.1 200 OK\nContent-Length: 12\n\nHello world!"
#define LISTEN_BACKLOG 1024
#define TIMEOUT_SECS 5

namespace {

struct sockaddr_in server_addr;

void hello(fev::socket &&socket) try {
  char buffer[1024];

  for (;;) {

#ifdef WITH_TIMEOUT
    std::size_t num_read = socket.try_read_for(
        buffer, sizeof(buffer), std::chrono::seconds(TIMEOUT_SECS));
#else
    std::size_t num_read = socket.read(buffer, sizeof(buffer));
#endif

    if (num_read == 0)
      break;

#ifdef WITH_TIMEOUT
    std::size_t num_written = socket.try_write_for(
        RESPONSE, sizeof(RESPONSE) - 1, std::chrono::seconds(TIMEOUT_SECS));
#else
    std::size_t num_written = socket.write(RESPONSE, sizeof(RESPONSE) - 1);
#endif

    if (num_written != sizeof(RESPONSE) - 1) {
      std::cerr << "Writing to socket failed\n";
      std::exit(1);
    }
  }
} catch (const std::system_error &e) {
  std::cerr << "[hello] " << e.what() << '\n';
}

void acceptor() {
  fev::socket socket;
  socket.open(AF_INET, SOCK_STREAM, 0);
  socket.set_reuse_addr();
  socket.bind(reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr));
  socket.listen(LISTEN_BACKLOG);

  for (;;) {
    auto new_socket = socket.accept();
    fev::fiber::spawn(&hello, std::move(new_socket));
  }
}

} // namespace

int main(int argc, char **argv) {
  // Parse arguments.

  if (argc != 4) {
    std::cerr << "Usage: " << argv[0] << " <HOST-IPV4> <PORT> <NUM-WORKERS>\n";
    return 1;
  }

  auto host = argv[1];

  std::uint16_t port;
  if (auto [_, ec] =
          std::from_chars(argv[2], argv[2] + std::strlen(argv[2]), port);
      ec != std::errc{}) {
    std::cerr << "Parsing port failed\n";
    return 1;
  }

  std::uint32_t num_workers;
  if (auto [_, ec] =
          std::from_chars(argv[3], argv[3] + std::strlen(argv[3]), num_workers);
      ec != std::errc{}) {
    std::cerr << "Parsing number of workers failed\n";
    return 1;
  }

  // Initialize server address.

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  if (inet_aton(host, &server_addr.sin_addr) != 1) {
    std::cerr << "Converting host IPv4 '" << host << "' failed\n";
    return 1;
  }

  // Run.

  fev::sched_attr sched_attr{};
  sched_attr.set_num_workers(num_workers);
  fev::sched sched{sched_attr};
  fev::fiber::spawn(sched, &acceptor);
  sched.run();

  return 0;
}
