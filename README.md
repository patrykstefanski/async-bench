# async-bench

A benchmark of approaches to writing server applications.

## Frameworks

This benchmark compares 'frameworks' for server applications that:
* can split work across multiple processors
* can handle non-uniform load (e.g. by having a shared run queue)
* provide some synchronization primitives between tasks

This includes:
* threads (one thread per connection with blocking I/O)
* [libfev](https://github.com/patrykstefanski/libfev) (Note I am the author of both this library and this benchmark)
* [Boost.Asio](https://www.boost.org/doc/libs/1_72_0/doc/html/boost_asio.html)
* [Go](https://golang.org/)
* [Tokio](https://tokio.rs/)
* [async-std](https://async.rs/)

One exception to that is **raw-epoll**. It is a multi-threaded server where each thread has its own epoll instance. A
listening socket is added to each epoll instance with SO\_REUSEPORT flag, thus the underlying kernel splits the
connections across the threads, and each connection then is pinned to one thread.

## Benchmarks

**hello** is a simple server that awaits for a request and sends a valid HTTP response. It doesn't parse requests. After
reading any data, it sends a response.

Moreover, it tries to send the HTTP response fully (in one write/send call). If it fails to do it, the process is
killed. However, it didn't happen during the benchmark and each response was fully written in one call.

These simplifications were made so that we can focus on measuring how well the frameworks handle I/O and scheduling.
HTTP was chosen so that available benchmarking tools can be used as well, such as [wrk](https://github.com/wg/wrk).

**hello-timeout**, in addition, adds 5 seconds timeouts for both reading and writing. This should show how well timers
are handled.

TODO: Add some benchmarks that use synchronization primitives.

## Throughput

Each server implementation spawns 12 workers (in the case of threads implementation, the server can use as many threads
as it wants). The benchmarking tool spawns also 12 threads and 64 connection per each thread. Each connection does 20k
requests. Wall-clock time is measured and then a number of requests per second is calculated.

Each test consists of 3 warm up rounds and 30 normal rounds, then the average of normal rounds results is calculated.
After each test, the system is rebooted.

Both servers and the benchmarking tool share the available processors on a machine with 6 cores and 12 threads.

### hello

| framework                               |   reqs/s  |
|-----------------------------------------|-----------|
| threads                                 |   930,142 |
| raw-epoll                               | 1,401,905 |
| Boost.Asio                              | 1,011,690 |
| Go                                      | 1,265,821 |
| Tokio                                   | 1,120,036 |
| async-std                               |   974,002 |
| fev-epoll-work-sharing-bounded-mpmc     | 1,289,267 |
| fev-epoll-work-sharing-locking          | 1,337,659 |
| fev-epoll-work-sharing-simple-mpmc      | 1,281,790 |
| fev-epoll-work-stealing-bounded-mpmc    | 1,348,033 |
| fev-epoll-work-stealing-bounded-spmc    | 1,347,916 |
| fev-epoll-work-stealing-locking         | 1,352,952 |
| fev-io_uring-work-sharing-bounded-mpmc  | 1,025,687 |
| fev-io_uring-work-sharing-locking       | 1,151,718 |
| fev-io_uring-work-sharing-simple-mpmc   | 1,022,070 |
| fev-io_uring-work-stealing-bounded-mpmc | 1,217,764 |
| fev-io_uring-work-stealing-bounded-spmc | 1,208,581 |
| fev-io_uring-work-stealing-locking      | 1,214,600 |

The following command for each framework was used:

```shell script
./bench-throughput.sh ./FRAMEWORK/hello 127.0.0.1 3000 12 12 64 20000 3 30
```

### hello-timeout

| framework                               |   reqs/s  |
|-----------------------------------------|-----------|
| threads                                 |   904,918 |
| Boost.Asio                              |   478,371 |
| Go                                      | 1,126,572 |
| Tokio                                   |   735,227 |
| async-std                               |   928,534 |
| fev-epoll-work-sharing-bounded-mpmc     | 1,259,515 |
| fev-epoll-work-sharing-locking          | 1,291,766 |
| fev-epoll-work-sharing-simple-mpmc      | 1,248,902 |
| fev-epoll-work-stealing-bounded-mpmc    | 1,303,468 |
| fev-epoll-work-stealing-bounded-spmc    | 1,300,814 |
| fev-epoll-work-stealing-locking         | 1,302,324 |
| fev-io_uring-work-sharing-bounded-mpmc  |   945,220 |
| fev-io_uring-work-sharing-locking       | 1,002,519 |
| fev-io_uring-work-sharing-simple-mpmc   |   928,448 |
| fev-io_uring-work-stealing-bounded-mpmc | 1,069,672 |
| fev-io_uring-work-stealing-bounded-spmc | 1,079,243 |
| fev-io_uring-work-stealing-locking      | 1,089,061 |

The following command for each framework was used:

```shell script
./bench-throughput.sh ./FRAMEWORK/hello-timeout 127.0.0.1 3000 12 12 64 20000 3 30
```

## Latency

Each server implementation spawns 6 workers (in the case of threads ```taskset -c 0-5``` is used). The benchmarking tool
spawns also 6 threads and 64 connections per each thread. Each connection does 20k requests. After receiving a response,
the benchmarking tool delays the next request for 1ms. The time between a request and its response is measured.

Each test consists of 3 warm up rounds and 30 normal rounds, then the average of normal rounds results is calculated
(e.g. an average of medians from 30 rounds) and presented in nanoseconds. After each test, the system is rebooted.

qX denotes quantiles. For example, a value for q0.9999 column means that 99.99% of all requests took just as much or
less time than that value.

### hello

| framework                               |  mean  | median |  q0.9   |  q0.99  |  q0.999   |   q0.9999  |
|-----------------------------------------|--------|--------|---------|---------|-----------|------------|
| threads                                 | 19,913 | 18,070 |  32,032 |  43,737 |    53,163 |    126,103 |
| raw-epoll                               | 17,513 | 15,558 |  27,646 |  38,822 |    47,897 |    554,005 |
| Boost.Asio                              | 21,393 | 19,318 |  32,846 |  49,131 |    86,839 |    338,921 |
| Go                                      | 18,974 | 17,092 |  29,505 |  43,911 |    59,890 |    209,802 |
| Tokio                                   | 17,930 | 16,377 |  26,108 |  38,347 |    65,201 |    562,775 |
| async-std                               | 20,544 | 18,534 |  29,326 |  43,102 |   175,335 |    807,858 |
| fev-epoll-work-sharing-bounded-mpmc     | 17,602 | 16,212 |  26,750 |  37,016 |    46,410 |    146,483 |
| fev-epoll-work-sharing-locking          | 17,435 | 16,089 |  26,321 |  36,395 |    45,412 |    155,440 |
| fev-epoll-work-sharing-simple-mpmc      | 17,699 | 16,329 |  26,821 |  37,018 |    46,070 |    152,688 |
| fev-epoll-work-stealing-bounded-mpmc    | 18,904 | 17,168 |  29,364 |  41,342 |    51,860 |    555,482 |
| fev-epoll-work-stealing-bounded-spmc    | 18,911 | 17,153 |  29,367 |  41,361 |    51,835 |    664,495 |
| fev-epoll-work-stealing-locking         | 18,725 | 17,018 |  29,018 |  40,837 |    51,203 |    505,659 |
| fev-io_uring-work-sharing-bounded-mpmc  | 69,758 | 21,094 |  47,885 | 788,073 | 9,267,102 | 14,517,913 |
| fev-io_uring-work-sharing-locking       | 26,449 | 22,329 |  43,839 |  83,165 |   159,117 |    246,950 |
| fev-io_uring-work-sharing-simple-mpmc   | 48,472 | 22,607 |  51,371 | 445,012 | 4,072,171 |  7,680,008 |
| fev-io_uring-work-stealing-bounded-mpmc | 58,697 | 39,867 | 117,507 | 286,562 |   518,514 |  2,971,076 |
| fev-io_uring-work-stealing-bounded-spmc | 58,104 | 39,753 | 117,507 | 286,530 |   505,817 |    939,609 |
| fev-io_uring-work-stealing-locking      | 54,257 | 37,978 | 108,610 | 263,357 |   465,995 |    717,205 |

The following command for each framework was used:

```shell script
./bench-latency.sh ./FRAMEWORK/hello 127.0.0.1 3000 6 6 64 20000 1000000 3 30
```

### hello-timeout

| framework                               |  mean  | median |  q0.9  |  q0.99  |   q0.999  |   q0.9999  |
|-----------------------------------------|--------|--------|--------|---------|-----------|------------|
| threads                                 | 20,405 | 18,508 | 32,896 |  44,903 |    54,687 |    140,981 |
| Boost.Asio                              | 21,459 | 19,372 | 33,178 |  49,491 |    84,077 |    180,739 |
| Go                                      | 20,110 | 18,057 | 31,519 |  47,055 |    64,470 |    165,667 |
| Tokio                                   | 23,378 | 21,526 | 35,182 |  51,618 |    75,525 |    455,931 |
| async-std                               | 23,026 | 20,626 | 32,986 |  46,471 |   229,452 |  1,204,809 |
| fev-epoll-work-sharing-bounded-mpmc     | 18,542 | 17,128 | 28,066 |  38,707 |    47,698 |    132,021 |
| fev-epoll-work-sharing-locking          | 18,465 | 17,031 | 28,014 |  38,704 |    47,466 |    136,518 |
| fev-epoll-work-sharing-simple-mpmc      | 18,674 | 17,218 | 28,396 |  39,215 |    47,925 |    147,948 |
| fev-epoll-work-stealing-bounded-mpmc    | 20,350 | 18,473 | 31,721 |  44,484 |    55,087 |    372,055 |
| fev-epoll-work-stealing-bounded-spmc    | 19,925 | 18,070 | 31,030 |  43,626 |    54,130 |    412,133 |
| fev-epoll-work-stealing-locking         | 19,874 | 18,035 | 30,932 |  43,489 |    53,939 |    377,958 |
| fev-io_uring-work-sharing-bounded-mpmc  | 29,438 | 20,990 | 38,865 |  71,960 | 1,227,937 | 10,102,328 |
| fev-io_uring-work-sharing-locking       | 22,644 | 20,145 | 35,441 |  54,069 |    88,632 |    163,720 |
| fev-io_uring-work-sharing-simple-mpmc   | 25,832 | 20,969 | 38,377 |  68,521 |   332,538 |  4,341,473 |
| fev-io_uring-work-stealing-bounded-mpmc | 42,712 | 32,270 | 78,126 | 186,425 |   332,363 |    535,485 |
| fev-io_uring-work-stealing-bounded-spmc | 42,962 | 32,455 | 78,650 | 188,493 |   336,021 |    548,401 |
| fev-io_uring-work-stealing-locking      | 41,850 | 32,067 | 75,920 | 178,941 |   316,551 |    486,942 |

The following command for each framework was used:

```shell script
./bench-latency.sh ./FRAMEWORK/hello-timeout 127.0.0.1 3000 6 6 64 20000 1000000 3 30
```

## Environment

* i7-8700k (6 cores, 12 threads)
* Linux 5.8.5-arch1-1 with mitigations=off
* GCC 10.2.0
* Boost 1.72
* Rust 1.46.0 (04488afe3 2020-08-24)
* Go 1.15
