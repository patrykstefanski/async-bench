/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define LIKELY(e) __builtin_expect((e), 1)
#define UNLIKELY(e) __builtin_expect((e), 0)
#define UNREACHABLE() __builtin_unreachable()

#define MAX_EVENTS 64
#define REQUEST "Hello!!!"

struct conn {
  /* Non-blocking socket file descriptor. */
  int sock_fd;

  /* Number of performed requests so far. */
  uint32_t num_reqs;
};

/* Server address we are going to connect to. */
static const char *host;
static uint16_t port;
static struct sockaddr_in server_addr;

/* Number of workers (threads). */
static uint32_t num_workers = 1;

/* Number of connections per worker. */
static uint32_t num_conns = 1;

/* Number of requests per connection. */
static uint32_t num_reqs = 1;

/* Barrier to wait until all threads are initialized, so that we can start to measure the time. */
static pthread_barrier_t start_barrier;

/* Barrier to wait until all threads finished their work. */
static pthread_barrier_t end_barrier;

static uint64_t time_diff;

static void print_help(const char *prog_name)
{
  fprintf(stderr,
          "Usage: %s [OPTIONS] <HOST-IPV4> <PORT>\n"
          "\n"
          "Options:\n"
          "  -c, --num-conns   <N>    Number of connections per worker (default 1)\n"
          "  -r, --num-reqs    <N>    Number of requests per connection (default 1)\n"
          "  -w, --num-workers <N>    Number of worker threads (default 1)\n",
          prog_name);
  exit(1);
}

static void parse_u32_option(const char *what, uint32_t *p)
{
  uint32_t value;
  if (sscanf(optarg, "%" SCNu32, &value) != 1) {
    fprintf(stderr, "Parsing %s failed\n", what);
    exit(1);
  }
  if (value < 1) {
    fprintf(stderr, "%s must be at least 1\n", what);
    exit(1);
  }
  *p = value;
}

static void parse_options(int argc, char *const *argv)
{
  const char *prog_name = argv[0];

  for (;;) {
    static const struct option long_options[] = {
        {"num-conns", required_argument, NULL, 'c'},
        {"num-reqs", required_argument, NULL, 'r'},
        {"num-workers", required_argument, NULL, 'w'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };
    int option_index;
    int c = getopt_long(argc, argv, "hc:r:w:", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    default:
    case 'h':
      print_help(prog_name);
      UNREACHABLE();
    case 'c':
      parse_u32_option("number of connections", &num_conns);
      break;
    case 'r':
      parse_u32_option("number of requests", &num_reqs);
      break;
    case 'w':
      parse_u32_option("number of workers", &num_workers);
      break;
    }
  }

  argc -= optind;
  argv += optind;

  if (argc != 2) {
    print_help(prog_name);
    UNREACHABLE();
  }

  host = argv[0];

  if (sscanf(argv[1], "%" SCNu16, &port) != 1) {
    fputs("Parsing port failed\n", stderr);
    exit(1);
  }
}

/* Outline the cold blocks of worker_run() to minimize instruction-cache in hot paths. */

#define GEN_ERR(name, msg)                                                                         \
  __attribute__((cold, noinline, noreturn)) static void name(void)                                 \
  {                                                                                                \
    fputs(msg "\n", stderr);                                                                       \
    exit(1);                                                                                       \
  }

#define GEN_PERROR(name, msg)                                                                      \
  __attribute__((cold, noinline, noreturn)) static void name(void)                                 \
  {                                                                                                \
    perror(msg);                                                                                   \
    exit(1);                                                                                       \
  }

GEN_ERR(conn_err, "Got error on a socket")
GEN_ERR(read_err, "Reading failed")
GEN_ERR(write_err, "Writing failed")

GEN_PERROR(epoll_wait_err, "Waiting for events failed")

/* Align to 64 bytes to minimize instruction-cache. */
__attribute__((aligned(64), noinline)) static void worker_run(int poller_fd)
{
  struct epoll_event events[MAX_EVENTS];
  size_t num_alive_conns = num_conns;

  while (num_alive_conns > 0) {
    int n;

    n = epoll_wait(poller_fd, events, MAX_EVENTS, -1);
    if (UNLIKELY(n < 0))
      epoll_wait_err();

    for (int i = 0; i < n; i++) {
      char buf[128];
      struct conn *conn;
      ssize_t num_read, num_written;
      uint32_t revents;

      conn = events[i].data.ptr;
      revents = events[i].events;

      if (UNLIKELY((revents & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) != 0))
        conn_err();

      num_read = read(conn->sock_fd, buf, sizeof(buf));
      if (UNLIKELY(num_read <= 0)) {
        if (UNLIKELY(errno != EAGAIN))
          read_err();
        continue;
      }

      /* Are we done? */
      if (UNLIKELY(conn->num_reqs == num_reqs)) {
        close(conn->sock_fd);
        --num_alive_conns;
        continue;
      }
      conn->num_reqs++;

      /* Send a request. */
      num_written = write(conn->sock_fd, REQUEST, sizeof(REQUEST) - 1);
      if (UNLIKELY(num_written != sizeof(REQUEST) - 1))
        write_err();
    }
  }
}

static inline uint64_t get_current_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  const uint64_t nsecs_per_sec = 1000 * 1000 * 1000;
  return (uint64_t)ts.tv_sec * nsecs_per_sec + (uint64_t)ts.tv_nsec;
}

static void *worker(void *arg)
{
  struct conn *conns;
  uint64_t start, end;
  uint32_t thread_no;
  int poller_fd;

  thread_no = (uint32_t)(uintptr_t)arg;

  poller_fd = epoll_create1(0);
  if (UNLIKELY(poller_fd < 0)) {
    perror("Creating epoll instance failed");
    exit(1);
  }

  /* Align to 64 to avoid false sharing. */
  conns = aligned_alloc(64, (size_t)num_conns * sizeof(*conns));
  if (UNLIKELY(conns == NULL)) {
    fputs("Allocating memory for connections failed\n", stderr);
    exit(1);
  }

  /* Initialize connections. */

  for (uint32_t i = 0; i < num_conns; i++) {
    struct epoll_event ev;
    struct conn *conn = &conns[i];
    int sock_fd, err;

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (UNLIKELY(sock_fd < 0)) {
      perror("Opening client socket failed");
      exit(1);
    }

    err = connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (UNLIKELY(err < 0)) {
      perror("Connecting to the server failed");
      exit(1);
    }

    err = ioctl(sock_fd, FIONBIO, &(int){1});
    if (UNLIKELY(err < 0)) {
      perror("ioctl() on client socket failed");
      exit(1);
    }

    conn->sock_fd = sock_fd;
    conn->num_reqs = 1;

    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    ev.data.ptr = conn;
    err = epoll_ctl(poller_fd, EPOLL_CTL_ADD, sock_fd, &ev);
    if (UNLIKELY(err < 0)) {
      perror("Adding client socket to poller failed");
      exit(1);
    }
  }

  /* Wait for all threads to finish the initialization. */

  pthread_barrier_wait(&start_barrier);

  /* Start counting the time. */

  if (thread_no == 0)
    start = get_current_ns();

  /* Send the first requests. */

  for (uint32_t i = 0; i < num_conns; i++) {
    struct conn *conn = &conns[i];
    ssize_t num_written;

    num_written = write(conn->sock_fd, REQUEST, sizeof(REQUEST) - 1);
    if (UNLIKELY(num_written != sizeof(REQUEST) - 1))
      write_err();
  }

  /* Start the hot loop. */

  worker_run(poller_fd);

  /* Wait for all threads to finish the work. */

  pthread_barrier_wait(&end_barrier);

  /* Calculate the taken time. */

  if (thread_no == 0) {
    end = get_current_ns();
    time_diff = end - start;
  }

  return NULL;
}

int main(int argc, char **argv)
{
  pthread_t *threads;
  uint64_t total_requests;
  double s;
  int err;

  parse_options(argc, argv);

  /* Initialize server address. */

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  if (inet_aton(host, &server_addr.sin_addr) != 1) {
    fprintf(stderr, "Converting host IPv4 '%s' failed\n", host);
    return 1;
  }

  /* Initialize the barriers. */

  err = pthread_barrier_init(&start_barrier, /*attr=*/NULL, num_workers);
  if (UNLIKELY(err != 0)) {
    fprintf(stderr, "Creating the start barrier failed: %s\n", strerror(err));
    return 1;
  }

  err = pthread_barrier_init(&end_barrier, /*attr=*/NULL, num_workers);
  if (UNLIKELY(err != 0)) {
    fprintf(stderr, "Creating the end barrier failed: %s\n", strerror(err));
    return 1;
  }

  /* Run workers. */

  threads = malloc((size_t)num_workers * sizeof(*threads));
  if (UNLIKELY(threads == NULL)) {
    fputs("Allocating memory for threads failed\n", stderr);
    return 1;
  }

  for (uint32_t i = 0; i < num_workers; i++) {
    void *arg = (void *)(uintptr_t)i;
    int err = pthread_create(&threads[i], /*attr=*/NULL, worker, arg);
    if (UNLIKELY(err != 0)) {
      fprintf(stderr, "Creating thread failed: %s\n", strerror(err));
      return 1;
    }
  }

  for (uint32_t i = 0; i < num_workers; i++) {
    int err = pthread_join(threads[i], /*retval=*/NULL);
    if (UNLIKELY(err != 0)) {
      fprintf(stderr, "Joining thread failed: %s\n", strerror(err));
      return 1;
    }
  }

  /* Calculate and print results. */

  total_requests = (uint64_t)num_reqs * (uint64_t)num_conns;
  if (UNLIKELY(total_requests > UINT64_MAX / (uint64_t)num_workers)) {
    fputs("Overflow in the calculation of total requests\n", stderr);
    return 1;
  }
  total_requests *= num_workers;

  s = (double)time_diff / 1e9;
  printf("%" PRIu64 " requests in %.2lfs, rate: %.2lf req/s\n", total_requests, s,
         (double)total_requests / s);

  return 0;
}
