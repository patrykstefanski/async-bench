#!/bin/bash

readonly TOOL="./build/tools/bench-latency"

usage() {
  echo "$0 <BINARY-PATH> <HOST-IPV4> <START-PORT> <SERVER-THREADS> <TOOL-THREADS> <TOOL-CONNS> <TOOL-REQS> <TOOL-DELAY> <WARMUP-ROUNDS> <NORMAL-ROUNDS>"
  exit 1
}

if [[ $# -ne 10 ]]; then
  usage
fi

readonly BINARY_PATH="$1"
readonly HOST_IPV4="$2"
readonly START_PORT="$3"
readonly SERVER_THREADS="$4"
readonly TOOL_THREADS="$5"
readonly TOOL_CONNS="$6"
readonly TOOL_REQS="$7"
readonly TOOL_DELAY="$8"
readonly WARMUP_ROUNDS="$9"
readonly NORMAL_ROUNDS="${10}"

keys=(mean median "q 0.9" "q 0.99" "q 0.999" "q 0.9999")

declare -A total
for key in "${keys[@]}"; do
  total["$key"]=0
done

for ((i = 1; i <= $((WARMUP_ROUNDS + NORMAL_ROUNDS)); i++)); do
  # Workaround for io_uring bug
  PORT=$((START_PORT + i))

  # for threads use:
  # taskset -c 0-5 "$BINARY_PATH" "$HOST_IPV4" "$PORT" &>/dev/null &

  "$BINARY_PATH" "$HOST_IPV4" "$PORT" "$SERVER_THREADS" &>/dev/null &
  pid=$!
  echo "Created server with pid $pid"

  sleep 1

  result=$("$TOOL" -w "$TOOL_THREADS" -c "$TOOL_CONNS" -r "$TOOL_REQS" -d "$TOOL_DELAY" "$HOST_IPV4" "$PORT")

  if [[ $i -le $WARMUP_ROUNDS ]]; then
    echo "$i/$WARMUP_ROUNDS warm up"
  else
    n=$((i - WARMUP_ROUNDS))
    echo "$n/$NORMAL_ROUNDS"
    for key in "${keys[@]}"; do
      value=$(echo "$result" | grep "$key:" | grep -Eo '[+-]?[0-9]+([.][0-9]+)?' | tail -n1)
      total["$key"]=$(echo "${total[$key]}" "$value" | awk '{ printf "%f", $1 + $2 }')
      avg=$(echo "${total[$key]}" $n | awk '{ printf "%.02f", $1 / $2 }')
      printf "%-12s cur=%-12.f avg=%-12.f\n" "$key:" "$value" "$avg"
    done
  fi

  echo "Killing $pid"
  if ! kill $pid; then
    echo "Server failed"
    exit 1
  fi

  wait
done
