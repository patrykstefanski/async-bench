#include <arpa/inet.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define RESPONSE "HTTP/1.1 200 OK\nContent-Length: 12\n\nHello world!"
#define LISTEN_BACKLOG 1024
#define TIMEOUT_SECS 5

static void *worker(void *arg) {
  char buffer[1024];
  int client_fd = (int)(intptr_t)arg;

#ifdef WITH_TIMEOUT
  struct timeval tv;
  int ret;

  tv.tv_sec = TIMEOUT_SECS;
  tv.tv_usec = 0;

  ret = setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  if (ret < 0) {
    perror("Setting SO_RCVTIMEO on client socket failed");
    exit(1);
  }

  ret = setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  if (ret < 0) {
    perror("Setting SO_SNDTIMEO on client socket failed");
    exit(1);
  }
#endif

  for (;;) {
    ssize_t num_read, num_written;

    num_read = read(client_fd, buffer, sizeof(buffer));
    if (num_read <= 0) {
      if (num_read < 0)
        perror("Reading from socket failed");
      break;
    }

    num_written = write(client_fd, RESPONSE, sizeof(RESPONSE) - 1);
    if (num_written != sizeof(RESPONSE) - 1) {
      fputs("Writing to socket failed\n", stderr);
      break;
    }
  }

  close(client_fd);
  return NULL;
}

int main(int argc, char **argv) {
  struct sockaddr_in server_addr;
  pthread_attr_t thread_attr;
  const char *host;
  uint16_t port;
  int server_fd, ret;

  /* Parse arguments. */

  if (argc != 3) {
    fprintf(stderr, "Usage: %s <HOST-IPV4> <PORT>\n", argv[0]);
    return 1;
  }

  host = argv[1];

  if (sscanf(argv[2], "%" SCNu16, &port) != 1) {
    fputs("Parsing port failed\n", stderr);
    return 1;
  }

  /* Initialize server address. */

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  if (inet_aton(host, &server_addr.sin_addr) != 1) {
    fprintf(stderr, "Converting host IPv4 '%s' failed\n", host);
    return 1;
  }

  /* Initialize server socket. */

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("Opening server socket failed");
    return 1;
  }

  ret = setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
  if (ret < 0) {
    perror("Setting SO_REUSEADDR on server socket failed");
    return 1;
  }

  ret = bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
  if (ret < 0) {
    perror("Binding name to server socket failed");
    return 1;
  }

  ret = listen(server_fd, LISTEN_BACKLOG);
  if (ret < 0) {
    perror("Listening failed");
    return 1;
  }

  /* Initialize thread attributes. */

  ret = pthread_attr_init(&thread_attr);
  if (ret != 0) {
    fprintf(stderr, "Creating thread attributes failed: %s\n", strerror(ret));
    return 1;
  }

  ret = pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
  if (ret != 0) {
    fprintf(stderr, "Setting detached attribute on thread failed: %s\n",
            strerror(ret));
    return 1;
  }

  /* Accept connections. */

  for (;;) {
    pthread_t thread;
    int client_fd, ret;
    void *arg;

    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
      perror("Accepting new connection failed");
      return 1;
    }

    arg = (void *)(intptr_t)client_fd;
    ret = pthread_create(&thread, &thread_attr, worker, arg);
    if (ret != 0) {
      fprintf(stderr, "Creating thread failed: %s\n", strerror(ret));
      return 1;
    }
  }

  return 0;
}
