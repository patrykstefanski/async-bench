/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>

#define LIKELY(e) __builtin_expect((e), 1)
#define UNLIKELY(e) __builtin_expect((e), 0)
#define UNREACHABLE() __builtin_unreachable()

#define MAX_EVENTS 64
#define REQUEST "Hello!!!"

struct conn {
  /* Non-blocking socket file descriptor. */
  int sock_fd;

  /* Non-blocking timer file descriptor. */
  int timer_fd;

  /* Subarray of the global latencies array of size num_reqs assigned to this connection. */
  uint64_t *latencies;

  /* The last time a write() operation was performed. */
  uint64_t last_write_ns;

  /* Number of performed requests so far. */
  uint32_t num_reqs;

  /* Reading flag. Set to true iff we are expecting a read event. */
  bool reading;
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

/* Delay between requests of a single connection. */
static struct itimerspec delay = {
    .it_interval = {.tv_sec = 0, .tv_nsec = 0},
    .it_value = {.tv_sec = 0, .tv_nsec = 1000 * 1000}, /* 1ms */
};

/* Latencies array. The connections will put here measured latencies. */
static uint64_t *latencies;

/* A barrier to start worker_run() loop at the same time. */
static pthread_barrier_t start_barrier;

static void print_help(const char *prog_name)
{
  fprintf(
      stderr,
      "Usage: %s [OPTIONS] <HOST-IPV4> <PORT>\n"
      "\n"
      "Options:\n"
      "  -c, --num-conns   <N>    Number of connections per worker (default 1)\n"
      "  -d, --delay       <N>    Delay in nanoseconds before sending request (default 1000000)\n"
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
        {"delay", required_argument, NULL, 'd'},
        {"num-reqs", required_argument, NULL, 'r'},
        {"num-workers", required_argument, NULL, 'w'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };
    int option_index;
    int c = getopt_long(argc, argv, "hc:d:r:w:", long_options, &option_index);
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
    case 'd': {
      long ns;
      if (sscanf(optarg, "%li", &ns) != 1) {
        fputs("Parsing delay failed\n", stderr);
        exit(1);
      }
      if (ns < 0) {
        fputs("Delay cannot be negative\n", stderr);
        exit(1);
      }
      delay.it_value.tv_sec = ns / (1000 * 1000 * 1000);
      delay.it_value.tv_nsec = ns % (1000 * 1000 * 1000);
      break;
    }
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
GEN_ERR(unexpected_read_event_err, "Unexpected read event")
GEN_ERR(read_err, "Reading failed")
GEN_ERR(write_err, "Writing failed")

GEN_PERROR(epoll_wait_err, "Waiting for events failed")
GEN_PERROR(timerfd_settime_err, "Setting timer fd failed");

__attribute__((always_inline)) static inline uint64_t get_current_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  const uint64_t nsecs_per_sec = 1000 * 1000 * 1000;
  return (uint64_t)ts.tv_sec * nsecs_per_sec + (uint64_t)ts.tv_nsec;
}

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
      void *ptr = events[i].data.ptr;
      uint32_t revents = events[i].events;

      if (UNLIKELY((revents & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) != 0))
        conn_err();

      if (((uintptr_t)ptr & 1) == 0) {
        char buf[128];
        struct conn *conn = ptr;
        ssize_t num_read;
        uint64_t cur_ns, last_write_ns;
        int err;

        num_read = read(conn->sock_fd, buf, sizeof(buf));
        if (num_read <= 0) {
          if (UNLIKELY(errno != EAGAIN))
            read_err();
          continue;
        }

        cur_ns = get_current_ns();

        /* We shouldn't get two read events after sending one request. */
        if (UNLIKELY(!conn->reading))
          unexpected_read_event_err();
        conn->reading = false;

        /*
         * If last_write_ns is 0, this is the response to the request that was made in worker(),
         * which we will ignore in the latencies.
         */
        last_write_ns = conn->last_write_ns;
        if (LIKELY(last_write_ns != 0)) {
          assert(conn->num_reqs < num_reqs);
          uint64_t latency = cur_ns - last_write_ns;
          conn->latencies[conn->num_reqs] = latency;
          conn->num_reqs++;

          /* Are we done? */
          if (UNLIKELY(conn->num_reqs == num_reqs)) {
            close(conn->sock_fd);
            close(conn->timer_fd);
            --num_alive_conns;
            continue;
          }
        }

        err = timerfd_settime(conn->timer_fd, 0, &delay, NULL);
        if (UNLIKELY(err < 0))
          timerfd_settime_err();
      } else {
        struct conn *conn = (void *)((uintptr_t)ptr & ~(uintptr_t)1u);
        ssize_t num_written;

        conn->last_write_ns = get_current_ns();
        conn->reading = true;

        /* Send a request. */
        num_written = write(conn->sock_fd, REQUEST, sizeof(REQUEST) - 1);
        if (UNLIKELY(num_written != sizeof(REQUEST) - 1))
          write_err();
      }
    }
  }
}

static void *worker(void *arg)
{
  struct conn *conns;
  uint64_t *lat;
  uint32_t thread_no;
  int poller_fd;

  thread_no = (uint32_t)(uintptr_t)arg;
  lat = latencies + (size_t)thread_no * (size_t)num_conns * (size_t)num_reqs;

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

  for (uint32_t i = 0; i < num_conns; i++, lat += (size_t)num_reqs) {
    struct epoll_event ev;
    struct conn *conn = &conns[i];
    int sock_fd, timer_fd, err;

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

    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (UNLIKELY(timer_fd < 0)) {
      perror("Creating timer failed");
      exit(1);
    }

    conn->sock_fd = sock_fd;
    conn->timer_fd = timer_fd;
    conn->latencies = lat;
    conn->last_write_ns = 0;
    conn->num_reqs = 0;
    conn->reading = true;

    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    ev.data.ptr = conn;
    err = epoll_ctl(poller_fd, EPOLL_CTL_ADD, sock_fd, &ev);
    if (UNLIKELY(err < 0)) {
      perror("Adding client socket to poller failed");
      exit(1);
    }

    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = (void *)((uintptr_t)conn | 1u);
    err = epoll_ctl(poller_fd, EPOLL_CTL_ADD, timer_fd, &ev);
    if (UNLIKELY(err < 0)) {
      perror("Adding client timer to poller failed");
      exit(1);
    }
  }

  /* Send first requests, which are later ignored. */

  for (uint32_t i = 0; i < num_conns; i++) {
    struct conn *conn = &conns[i];
    ssize_t num_written;

    num_written = write(conn->sock_fd, REQUEST, sizeof(REQUEST) - 1);
    if (UNLIKELY(num_written != sizeof(REQUEST) - 1))
      write_err();
  }

  /* Wait for all threads to finish the initialization. */

  pthread_barrier_wait(&start_barrier);

  /* Start the hot loop. */

  worker_run(poller_fd);

  return NULL;
}

static int cmp_u64(const void *a, const void *b)
{
  uint64_t lhs = *(uint64_t *)a;
  uint64_t rhs = *(uint64_t *)b;
  if (lhs > rhs)
    return 1;
  if (lhs < rhs)
    return -1;
  return 0;
}

int main(int argc, char **argv)
{
  pthread_t *threads;
  size_t num_latencies, n;
  uint64_t sum, mean, min, max, median, q09, q095, q099, q0995, q0999, q09995, q09999;
  int err;

  parse_options(argc, argv);

  /* Initialize server address. */

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  if (inet_aton(host, &server_addr.sin_addr) != 1) {
    fprintf(stderr, "Converting host IPv4 '%s' failed\n", host);
    return 1;
  }

  /* Prepare latencies. */

  num_latencies = (size_t)num_workers * (size_t)num_conns;
  if (UNLIKELY((size_t)num_reqs > (SIZE_MAX / sizeof(*latencies)) / num_latencies)) {
    fputs("num_workers * num_conns * num_reqs * sizeof(uint64_t) overflows size_t\n", stderr);
    return 1;
  }
  num_latencies *= num_reqs;

  latencies = calloc(num_latencies, sizeof(*latencies));
  if (UNLIKELY(latencies == NULL)) {
    fputs("Allocating memory for latencies failed\n", stderr);
    return 1;
  }

  /* Initialize the barrier. */

  err = pthread_barrier_init(&start_barrier, /*attr=*/NULL, num_workers);
  if (UNLIKELY(err != 0)) {
    fprintf(stderr, "Creating barrier failed: %s\n", strerror(err));
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

  sum = 0;
  for (size_t i = 0; i < num_latencies; i++) {
    uint64_t latency = latencies[i];
    if (sum > UINT64_MAX - latency) {
      fputs("Overflow in the calculation of mean\n", stderr);
      return 1;
    }
    sum += latencies[i];
  }
  mean = sum / num_latencies;

  qsort(latencies, num_latencies, sizeof(*latencies), cmp_u64);

#if SIZE_MAX >= UINT64_MAX
  if (num_latencies > UINT64_MAX / 9999) {
    fputs("Overflow in the calculation of quantiles\n", stderr);
    return 1;
  }
#endif

  min = latencies[0];
  max = latencies[num_latencies - 1];
  median = latencies[num_latencies / 2];
  q09 = latencies[num_latencies * 9 / 10];
  q095 = latencies[num_latencies * 95 / 100];
  q099 = latencies[num_latencies * 99 / 100];
  q0995 = latencies[num_latencies * 995 / 1000];
  q0999 = latencies[num_latencies * 999 / 1000];
  q09995 = latencies[num_latencies * 9995 / 10000];
  q09999 = latencies[num_latencies * 9999 / 10000];

  printf("Latency [ns]:\n"
         "  mean:     %" PRIu64 "\n"
         "  min:      %" PRIu64 "\n"
         "  max:      %" PRIu64 "\n"
         "  median:   %" PRIu64 "\n"
         "  q 0.9:    %" PRIu64 "\n"
         "  q 0.95:   %" PRIu64 "\n"
         "  q 0.99:   %" PRIu64 "\n"
         "  q 0.995:  %" PRIu64 "\n"
         "  q 0.999:  %" PRIu64 "\n"
         "  q 0.9995: %" PRIu64 "\n"
         "  q 0.9999: %" PRIu64 "\n\n",
         mean, min, max, median, q09, q095, q099, q0995, q0999, q09995, q09999);

  n = 10;
  if (n > num_latencies)
    n = num_latencies;
  printf("Best %zu:\n", n);
  for (size_t i = 0; i < n; i++)
    printf("  %2zu. %" PRIu64 "\n", i + 1, latencies[i]);
  printf("\nWorst %zu:\n", n);
  for (size_t i = 0; i < n; i++)
    printf("  %2zu. %" PRIu64 "\n", i + 1, latencies[num_latencies - i - 1]);

  return 0;
}
