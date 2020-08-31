#include <arpa/inet.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#include <fev/fev.h>

#define RESPONSE "HTTP/1.1 200 OK\nContent-Length: 12\n\nHello world!"
#define LISTEN_BACKLOG 1024
#define TIMEOUT_SECS 5

static struct sockaddr_in server_addr;

static void *hello(void *arg) {
  char buffer[1024];
  struct fev_socket *socket = arg;

#ifdef WITH_TIMEOUT
  const struct timespec ts = {
      .tv_sec = TIMEOUT_SECS,
      .tv_nsec = 0,
  };
#endif

  for (;;) {
    ssize_t num_read, num_written;

#ifdef WITH_TIMEOUT
    num_read = fev_socket_try_read_for(socket, buffer, sizeof(buffer), &ts);
#else
    num_read = fev_socket_read(socket, buffer, sizeof(buffer));
#endif

    if (num_read <= 0) {
      if (num_read < 0) {
        int err = (int)(-num_read);
        fprintf(stderr, "Reading from socket failed: %s\n", strerror(err));
      }
      break;
    }

#ifdef WITH_TIMEOUT
    num_written =
        fev_socket_try_write_for(socket, RESPONSE, sizeof(RESPONSE) - 1, &ts);
#else
    num_written = fev_socket_write(socket, RESPONSE, sizeof(RESPONSE) - 1);
#endif

    if (num_written != sizeof(RESPONSE) - 1) {
      fputs("Writing to socket failed\n", stderr);
      exit(1);
    }
  }

  fev_socket_close(socket);
  fev_socket_destroy(socket);

  return NULL;
}

static void *acceptor(void *arg) {
  struct fev_socket *socket;
  int ret;

  (void)arg;

  ret = fev_socket_create(&socket);
  if (ret != 0) {
    fprintf(stderr, "Creating socket failed: %s\n", strerror(-ret));
    goto out;
  }

  ret = fev_socket_open(socket, AF_INET, SOCK_STREAM, 0);
  if (ret != 0) {
    fprintf(stderr, "Opening socket failed: %s\n", strerror(-ret));
    goto out_destroy;
  }

  ret = fev_socket_set_opt(socket, SOL_SOCKET, SO_REUSEADDR, &(int){1},
                           sizeof(int));
  if (ret != 0) {
    fprintf(stderr, "Setting SO_REUSEADDR failed: %s\n", strerror(-ret));
    goto out_close;
  }

  ret = fev_socket_bind(socket, (struct sockaddr *)&server_addr,
                        sizeof(server_addr));
  if (ret != 0) {
    fprintf(stderr, "Binding socket failed: %s\n", strerror(-ret));
    goto out_close;
  }

  ret = fev_socket_listen(socket, LISTEN_BACKLOG);
  if (ret != 0) {
    fprintf(stderr, "Listening on socket failed: %s\n", strerror(-ret));
    goto out_close;
  }

  for (;;) {
    struct fev_socket *new_socket;

    ret = fev_socket_create(&new_socket);
    if (ret != 0) {
      fprintf(stderr, "Creating new socket failed: %s\n", strerror(-ret));
      goto out_close;
    }

    ret = fev_socket_accept(socket, new_socket, /*address=*/NULL,
                            /*address_len=*/NULL);
    if (ret != 0) {
      fprintf(stderr, "Accepting socket failed: %s\n", strerror(-ret));
      fev_socket_destroy(new_socket);
      goto out_close;
    }

    ret = fev_fiber_spawn(/*sched=*/NULL, &hello, new_socket);
    if (ret != 0) {
      fprintf(stderr, "Spawning echo fiber failed: %s\n", strerror(-ret));
      fev_socket_destroy(new_socket);
      goto out_close;
    }
  }

out_close:
  fev_socket_close(socket);

out_destroy:
  fev_socket_destroy(socket);

out:
  return NULL;
}

int main(int argc, char **argv) {
  struct fev_sched_attr *attr;
  struct fev_sched *sched;
  const char *host;
  uint32_t num_workers;
  uint16_t port;
  int err, ret = 1;

  /* Parse arguments. */

  if (argc != 4) {
    fprintf(stderr, "Usage: %s <HOST-IPV4> <PORT> <NUM-WORKERS>\n", argv[0]);
    return 1;
  }

  host = argv[1];

  if (sscanf(argv[2], "%" SCNu16, &port) != 1) {
    fputs("Parsing port failed\n", stderr);
    return 1;
  }

  if (sscanf(argv[3], "%" SCNu32, &num_workers) != 1) {
    fputs("Parsing number of workers failed\n", stderr);
    return 1;
  }

  /* Initialize server address. */

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  if (inet_aton(host, &server_addr.sin_addr) != 1) {
    fprintf(stderr, "Converting host IPv4 '%s' failed\n", host);
    return 1;
  }

  /* Initialize scheduler. */

  err = fev_sched_attr_create(&attr);
  if (err != 0) {
    fprintf(stderr, "Creating scheduler attributes failed: %s\n",
            strerror(-err));
    return 1;
  }

  fev_sched_attr_set_num_workers(attr, num_workers);

  err = fev_sched_create(&sched, attr);
  if (err != 0) {
    fprintf(stderr, "Creating scheduler failed: %s\n", strerror(-err));
    goto out_sched_attr;
  }

  /* Schedule acceptor. */

  err = fev_fiber_spawn(sched, &acceptor, sched);
  if (err != 0) {
    fprintf(stderr, "Spawning acceptor fiber failed: %s\n", strerror(-err));
    goto out_sched;
  }

  /* Run. */

  err = fev_sched_run(sched);
  if (err != 0) {
    fprintf(stderr, "Running scheduler failed: %s\n", strerror(-err));
    goto out_sched;
  }

  ret = 0;

out_sched:
  fev_sched_destroy(sched);

out_sched_attr:
  fev_sched_attr_destroy(attr);

  return ret;
}
