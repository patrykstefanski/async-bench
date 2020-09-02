#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uv.h>

#define RESPONSE "HTTP/1.1 200 OK\nContent-Length: 12\n\nHello world!"
#define LISTEN_BACKLOG 1024
#define BUF_SIZE 1024

static struct sockaddr_in server_addr;

static _Thread_local uv_loop_t *cur_loop;

static const uv_buf_t response_buf[] = {{
    .base = RESPONSE,
    .len = sizeof(RESPONSE) - 1,
}};

static void on_close(uv_handle_t *handle) { free(handle); }

static void on_write(uv_write_t *req, int status) {
  if (status != 0) {
    fprintf(stderr, "Writing failed: %s\n", uv_strerror(status));
  }

  free(req);
}

static void on_read(uv_stream_t *client, ssize_t num_read,
                    const uv_buf_t *buf) {
  free(buf->base);

  if (num_read > 0) {
    uv_write_t *req = malloc(sizeof(*req));
    if (req == NULL) {
      fputs("Allocating memory for write req failed\n", stderr);
      exit(1);
    }

    uv_write(req, client, response_buf, 1, on_write);
    return;
  }

  if (num_read < 0) {
    if (num_read != UV_EOF)
      fprintf(stderr, "Reading failed: %s\n", uv_strerror((int)num_read));

    uv_close((uv_handle_t *)client, on_close);
  }
}

static void alloc_buffer(uv_handle_t *handle, size_t suggested_size,
                         uv_buf_t *buf) {
  (void)handle;
  (void)suggested_size;

  buf->base = malloc(BUF_SIZE);
  buf->len = BUF_SIZE;
}

static void on_new_connection(uv_stream_t *server, int status) {
  uv_tcp_t *client;
  int ret;

  if (status < 0) {
    fprintf(stderr, "New connection error: %s\n", uv_strerror(status));
    exit(1);
  }

  client = malloc(sizeof(*client));
  if (client == NULL) {
    fputs("Allocating memory for client failed\n", stderr);
    exit(1);
  }

  ret = uv_tcp_init(cur_loop, client);
  if (ret != 0) {
    fprintf(stderr, "Initializing tcp connection failed: %s\n",
            uv_strerror(ret));
    exit(1);
  }

  ret = uv_accept(server, (uv_stream_t *)client);
  if (ret != 0) {
    fprintf(stderr, "Accepting failed: %s\n", uv_strerror(ret));
    exit(1);
  }

  ret = uv_read_start((uv_stream_t *)client, alloc_buffer, on_read);
  if (ret != 0) {
    fprintf(stderr, "Starting to read failed: %s\n", uv_strerror(ret));
    exit(1);
  }
}

static void *worker(void *arg) {
  uv_loop_t loop;
  uv_tcp_t server;
  int fd, ret;

  (void)arg;

  ret = uv_loop_init(&loop);
  if (ret != 0) {
    fprintf(stderr, "Initializing loop failed: %s\n", uv_strerror(ret));
    exit(1);
  }

  cur_loop = &loop;

  ret = uv_tcp_init_ex(&loop, &server, AF_INET);
  if (ret != 0) {
    fprintf(stderr, "Initializing tcp server failed: %s\n", uv_strerror(ret));
    exit(1);
  }

  uv_fileno((uv_handle_t *)&server, &fd);

  ret = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int));
  if (ret != 0) {
    perror("Setting SO_REUSEPORT failed");
    exit(1);
  }

  ret = uv_tcp_bind(&server, (const struct sockaddr *)&server_addr, 0);
  if (ret != 0) {
    fprintf(stderr, "Binding failed: %s\n", uv_strerror(ret));
    exit(1);
  }

  ret = uv_listen((uv_stream_t *)&server, LISTEN_BACKLOG, &on_new_connection);
  if (ret != 0) {
    fprintf(stderr, "Listening failed: %s\n", uv_strerror(ret));
    exit(1);
  }

  ret = uv_run(&loop, UV_RUN_DEFAULT);
  if (ret != 0) {
    fprintf(stderr, "Running loop failed: %s\n", uv_strerror(ret));
    exit(1);
  }

  return NULL;
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

  uv_ip4_addr(host, port, &server_addr);

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
