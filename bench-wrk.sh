#!/bin/bash

usage() {
  echo "$0 <BINARY-PATH> <HOST-IPV4> <START-PORT> <SERVER-THREADS> <WRK-CONNS> <WRK-DURATION> <WRK-THREADS> <WARMUP-ROUNDS> <NORMAL-ROUNDS>"
  exit 1
}

if [[ $# -ne 9 ]]; then
  usage
fi

readonly BINARY_PATH="$1"
readonly HOST_IPV4="$2"
readonly START_PORT="$3"
readonly SERVER_THREADS="$4"
readonly WRK_CONNS="$5"
readonly WRK_DURATION="$6"
readonly WRK_THREADS="$7"
readonly WARMUP_ROUNDS="$8"
readonly NORMAL_ROUNDS="$9"

total=0
for ((i = 1; i <= $((WARMUP_ROUNDS + NORMAL_ROUNDS)); i++)); do
  # Workaround for io_uring bug
  PORT=$((START_PORT + i))

  "$BINARY_PATH" "$HOST_IPV4" "$PORT" "$SERVER_THREADS" &>/dev/null &
  pid=$!
  echo "Created server with pid $pid"

  sleep 1

  reqs=$(wrk -c "$WRK_CONNS" -d "$WRK_DURATION" -t "$WRK_THREADS" "http://$HOST_IPV4:$PORT" | grep 'Requests/sec' | grep -Eo '[+-]?[0-9]+([.][0-9]+)?')

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
