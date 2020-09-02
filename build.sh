#!/bin/bash

readonly SRC_DIR=$(dirname "$(readlink -f "$0")")
readonly BUILD_DIR=build

# tools
cmake -S "$SRC_DIR/tools" -B "$BUILD_DIR/tools" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR/tools" --config Release

# fev
pollers=(epoll io_uring)
scheds=(
  work-sharing-locking
  work-sharing-bounded-mpmc
  work-sharing-simple-mpmc
  work-stealing-locking
  work-stealing-bounded-mpmc
  work-stealing-bounded-spmc
)
for poller in "${pollers[@]}"; do
  for sched in "${scheds[@]}"; do
    cmake -S "$SRC_DIR/frameworks/fev" -B "$BUILD_DIR/fev-$poller-$sched" -DCMAKE_BUILD_TYPE=Release -DFEV_POLLER=$poller -DFEV_SCHED=$sched
    cmake --build "$BUILD_DIR/fev-$poller-$sched" --config Release
  done
done

# asio
cmake -S "$SRC_DIR/frameworks/asio" -B "$BUILD_DIR/asio" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR/asio" --config Release

# raw-epoll
cmake -S "$SRC_DIR/frameworks/raw-epoll" -B "$BUILD_DIR/raw-epoll" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR/raw-epoll" --config Release

# threads
cmake -S "$SRC_DIR/frameworks/threads" -B "$BUILD_DIR/threads" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR/threads" --config Release

# libuv
cmake -S "$SRC_DIR/frameworks/libuv" -B "$BUILD_DIR/libuv" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR/libuv" --config Release

# go
go build -o "$BUILD_DIR/go/hello" "$SRC_DIR/frameworks/go/hello.go"
go build -o "$BUILD_DIR/go/hello-timeout" "$SRC_DIR/frameworks/go/hello-timeout.go"

# async-std
cargo build --release --manifest-path "$SRC_DIR/frameworks/async-std/Cargo.toml" --target-dir "$BUILD_DIR/async-std"

# tokio
cargo build --release --manifest-path "$SRC_DIR/frameworks/tokio/Cargo.toml" --target-dir "$BUILD_DIR/tokio"
