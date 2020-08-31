#!/bin/bash

readonly TOOL="./build/tools/bench-throughput"

usage() {
  echo "$0 <BINARY-PATH> <HOST-IPV4> <START-PORT> <SERVER-THREADS> <TOOL-THREADS> <TOOL-CONNS> <TOOL-REQS> <WARMUP-ROUNDS> <NORMAL-ROUNDS>"
  exit 1
}

if [[ $# -ne 9 ]]; then
  usage
fi

readonly BINARY_PATH="$1"
readonly HOST_IPV4="$2"
readonly START_PORT="$3"
readonly SERVER_THREADS="$4"
readonly TOOL_THREADS="$5"
readonly TOOL_CONNS="$6"
readonly TOOL_REQS="$7"
readonly WARMUP_ROUNDS="$8"
readonly NORMAL_ROUNDS="$9"

total=0
for ((i = 1; i <= $((WARMUP_ROUNDS + NORMAL_ROUNDS)); i++)); do
  # Workaround for io_uring bug
  PORT=$((START_PORT + i))

  # for threads the last param needs to be removed:
  # "$BINARY_PATH" "$HOST_IPV4" "$PORT" &>/dev/null &

  "$BINARY_PATH" "$HOST_IPV4" "$PORT" "$SERVER_THREADS" &>/dev/null &
  pid=$!
  echo "Created server with pid $pid"

  sleep 1

  reqs=$("$TOOL" -w "$TOOL_THREADS" -c "$TOOL_CONNS" -r "$TOOL_REQS" "$HOST_IPV4" "$PORT" | grep -Eo '[+-]?[0-9]+([.][0-9]+)?' | tail -n1)

  if [[ $i -le $WARMUP_ROUNDS ]]; then
    echo "$i/$WARMUP_ROUNDS warm up"
  else
    n=$((i - WARMUP_ROUNDS))
    total=$(echo "$total" "$reqs" | awk '{ printf "%f", $1 + $2 }')
    avg=$(echo "$total" $n | awk '{ printf "%.02f", $1 / $2 }')
    echo "$n/$NORMAL_ROUNDS cur=$reqs avg=$avg"
  fi

  echo "Killing $pid"
  if ! kill $pid; then
    echo "Server failed"
    exit 1
  fi

  wait
done
