#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define RESPONSE "HTTP/1.1 200 OK\nContent-Length: 12\n\nHello world!"
#define LISTEN_BACKLOG 1024
#define MAX_EVENTS 64

static struct sockaddr_in server_addr;

struct socket_data {
  int fd;
  bool reading;
};

static int open_listening_socket(void) {
  int fd, ret;

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("Opening server socket failed");
    exit(1);
  }

  ret = ioctl(fd, FIONBIO, &(int){1});
  if (ret == -1) {
    perror("ioctl() on server socket failed");
    exit(1);
  }

  ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
  if (ret != 0) {
    perror("Setting SO_REUSEADDR failed");
    exit(1);
  }

  ret = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int));
  if (ret != 0) {
    perror("Setting SO_REUSEPORT failed");
    exit(1);
  }

  ret = bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
  if (ret != 0) {
    perror("Binding name to server socket failed");
    exit(1);
  }

  ret = listen(fd, LISTEN_BACKLOG);
  if (ret != 0) {
    perror("Listening failed");
    exit(1);
  }

  return fd;
}

static void handle_accept_event(int epoll_fd, int server_fd) {
  for (;;) {
    struct epoll_event event;
    struct socket_data *data;
    int client_fd, ret;

    client_fd = accept4(server_fd, NULL, NULL, SOCK_NONBLOCK);
    if (client_fd < 0) {
      if (errno == EAGAIN)
        break;
      perror("Accepting connection failed");
      exit(1);
    }

    data = malloc(sizeof(*data));
    if (data == NULL) {
      fputs("Allocating socket data failed\n", stderr);
      exit(1);
    }

    data->fd = client_fd;
    data->reading = true;

    event.events =
        EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLHUP | EPOLLET;
    event.data.ptr = data;

    ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);
    if (ret != 0) {
      perror("Adding client fd to epoll failed");
      exit(1);
    }
  }
}

static void handle_client_event(struct socket_data *data) {
  int fd = data->fd;
  bool reading = data->reading;

  if (reading)
    goto do_read;
  else
    goto do_write;

do_read : {
  uint8_t buf[1024];
  ssize_t num_read = read(fd, buf, sizeof(buf));
  if (num_read <= 0) {
    if (num_read < 0 && errno == EAGAIN)
      goto out;
    goto done;
  }
  reading = false;
  goto do_write;
}

do_write : {
  ssize_t num_written = write(fd, RESPONSE, sizeof(RESPONSE) - 1);
  if (num_written < 0 && errno == EAGAIN)
    goto out;
  if (num_written != sizeof(RESPONSE) - 1) {
    fputs("Write failed\n", stderr);
    exit(1);
  }
  reading = true;
  goto do_read;
}

out:
  data->reading = reading;
  return;

done:
  close(fd);
  free(data);
}

static void *worker(void *arg) {
  struct epoll_event event;
  struct socket_data *data;
  int server_fd, epoll_fd, ret;

  (void)arg;

  server_fd = open_listening_socket();

  data = malloc(sizeof(*data));
  if (data == NULL) {
    fputs("Allocating socket data failed\n", stderr);
    exit(1);
  }

  data->fd = server_fd;
  data->reading = true;

  /* Initialize epoll instance. */

  epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    perror("Creating epoll instance failed");
    exit(1);
  }

  event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP | EPOLLET;
  event.data.ptr = data;
  ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);
  if (ret != 0) {
    perror("Adding server fd to epoll failed");
    exit(1);
  }

  /* Loop. */

  for (;;) {
    struct epoll_event events[MAX_EVENTS];
    int n;

    /* Wait indefinitely. */
    n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    if (n < 0) {
      perror("epoll_wait() failed");
      exit(1);
    }

    for (int i = 0; i < n; ++i) {
      struct epoll_event *event = &events[i];
      struct socket_data *data = event->data.ptr;

      if ((event->events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) != 0) {
        close(data->fd);
        free(data);
        continue;
      }

      if (data->fd == server_fd) {
        handle_accept_event(epoll_fd, server_fd);
      } else {
        handle_client_event(data);
      }
    }
  }
}

int main(int argc, char **argv) {
  pthread_t *threads;
  const char *host;
  uint16_t port;
  size_t num_threads;

  /* Parse arguments. */

  if (argc != 4) {
    fprintf(stderr, "Usage: %s <HOST-IPV4> <PORT> <NUM-THREADS>\n", argv[0]);
    return 1;
  }

  host = argv[1];

  if (sscanf(argv[2], "%" SCNu16, &port) != 1) {
    fputs("Parsing port failed\n", stderr);
    return 1;
  }

  if (sscanf(argv[3], "%zu", &num_threads) != 1) {
    fputs("Parsing number of threads failed\n", stderr);
    return 1;
  }

  /* Initialize server address. */

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  if (inet_aton(host, &server_addr.sin_addr) != 1) {
    fprintf(stderr, "Converting host IPv4 '%s' failed\n", host);
    return 1;
  }

  /* Run and wait. */

  assert(num_threads <= SIZE_MAX / sizeof(*threads));
  threads = malloc(num_threads * sizeof(*threads));
  if (threads == NULL) {
    fputs("Allocating memory for threads failed\n", stderr);
    return 1;
  }

  for (size_t i = 0; i < num_threads; i++) {
    int ret = pthread_create(&threads[i], /*attr=*/NULL, &worker, /*arg=*/NULL);
    if (ret != 0) {
      fprintf(stderr, "Creating thread failed: %s\n", strerror(ret));
      return 1;
    }
  }

  for (size_t i = 0; i < num_threads; i++) {
    int ret = pthread_join(threads[i], /*ret_val=*/NULL);
    if (ret != 0) {
      fprintf(stderr, "Joining thread failed: %s\n", strerror(ret));
      return 1;
    }
  }

  return 0;
}
