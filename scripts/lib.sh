#!/bin/bash
# Shared helpers for the run scripts. Source this file; do not execute it.

ORDER_PORT=${ORDER_PORT:-12345}

# True if something accepts TCP connections on the exchange order port (the probe connects and closes immediately).
port_open() { (exec 3<>"/dev/tcp/127.0.0.1/$ORDER_PORT") 2>/dev/null; }

# wait_ready PID TIMEOUT_S : wait until the exchange listens, failing fast if PID dies first.
wait_ready() {
  local pid=$1 t=$2 i
  for ((i = 0; i < t * 5; i++)); do
    kill -0 "$pid" 2>/dev/null || return 1
    port_open && return 0
    sleep 0.2
  done
  return 2
}

# stop_pid PID SIGNAL GRACE_S : send SIGNAL, wait up to GRACE_S, escalate to SIGTERM then SIGKILL, then reap. Only touches our own PID.
stop_pid() {
  local pid=$1 sig=$2 grace=$3 i
  kill -0 "$pid" 2>/dev/null || { wait "$pid" 2>/dev/null; return 0; }
  kill "-$sig" "$pid" 2>/dev/null
  for ((i = 0; i < grace * 5; i++)); do kill -0 "$pid" 2>/dev/null || break; sleep 0.2; done
  if kill -0 "$pid" 2>/dev/null; then echo "pid $pid ignored SIG$sig, sending SIGTERM" >&2; kill -TERM "$pid" 2>/dev/null; sleep 2; fi
  if kill -0 "$pid" 2>/dev/null; then echo "pid $pid still alive, sending SIGKILL" >&2; kill -KILL "$pid" 2>/dev/null; fi
  wait "$pid" 2>/dev/null
}
