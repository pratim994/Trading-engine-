#!/bin/bash
# End-to-end test over real TCP: exchange + RANDOM client, then graceful-shutdown checks. Bounded: finishes in about 1-2 minutes.
#   1. client trades a small workload, every NEW is ACCEPTED, client exits 0
#   2. SIGINT to the exchange exits 0 within the grace period, port closes
#   3. SIGTERM to a running client stops it gracefully (exit 0), then SIGTERM to the exchange exits 0
#   4. no exchange_main / trading_main process of ours is left behind
# Usage: scripts/test_integration.sh   (BIN=<build dir> to test another build, e.g. the sanitizer one)
cd "$(dirname "$0")/.." || exit 2
. scripts/lib.sh
BIN=${BIN:-$PWD/cmake-build-release}
RUN_DIR=$(mktemp -d /tmp/me_integration.XXXXXX); cd "$RUN_DIR" || exit 2
PIDS=(); FAILS=0
ok()   { echo "  PASS: $*"; }
bad()  { echo "  FAIL: $*"; FAILS=$((FAILS + 1)); }
check(){ local d=$1; shift; if "$@"; then ok "$d"; else bad "$d"; fi; }
trap 'for p in "${PIDS[@]}"; do kill -KILL "$p" 2>/dev/null; done' EXIT

export ME_NUM_ORDERS=50 ME_ORDER_INTERVAL_US=5000 ME_STARTUP_WAIT_S=3 ME_SILENT_LIMIT_S=3 ME_MAX_RUNTIME_S=45 ME_IDLE_SLEEP_US=100
port_open && { echo "port $ORDER_PORT already in use, refusing to run" >&2; exit 2; }

start_exchange() { "$BIN/exchange_main" > exchange.out 2>&1 & EX=$!; PIDS+=($EX); wait_ready "$EX" 60 || { echo "exchange failed to start"; cat exchange.out | tail -5; exit 2; }; }
# wait_exit PID GRACE_S -> sets WAIT_RC (exit status) ; returns 1 if the process outlived the grace period
wait_exit() { local i; for ((i = 0; i < $2 * 5; i++)); do kill -0 "$1" 2>/dev/null || { wait "$1"; WAIT_RC=$?; return 0; }; sleep 0.2; done; return 1; }

echo "[1] trading session"
start_exchange
"$BIN/trading_main" 5 RANDOM > client.out 2>&1 & CL=$!; PIDS+=($CL)
wait "$CL"; rc=$?
check "client exited 0 (got $rc)" test $rc -eq 0
accepted=$(grep -c 'type:ACCEPTED' trading_order_gateway_5.log)
check "all $ME_NUM_ORDERS NEW orders accepted (got $accepted)" test "$accepted" -ge "$ME_NUM_ORDERS"
check "exchange saw the client disconnect and released the socket" grep -q 'removing disconnected socket' exchange_order_server.log
check "exchange still alive after client left" kill -0 "$EX"

echo "[2] SIGINT shutdown of the exchange"
t0=$SECONDS; kill -INT "$EX"
wait_exit "$EX" 25; r=$?
check "exchange exited within 25s of SIGINT (took $((SECONDS - t0))s)" test $r -eq 0
check "exchange exit status 0 (got ${WAIT_RC:-none})" test "${WAIT_RC:-99}" -eq 0
check "order port closed" bash -c '! (exec 3<>/dev/tcp/127.0.0.1/'"$ORDER_PORT"') 2>/dev/null'

echo "[3] SIGTERM during a running session"
ME_NUM_ORDERS=100000 ME_MAX_RUNTIME_S=0 ME_SILENT_LIMIT_S=3600   # an effectively endless client
start_exchange
"$BIN/trading_main" 5 RANDOM > client2.out 2>&1 & CL=$!; PIDS+=($CL)
sleep 8; kill -0 "$CL" 2>/dev/null && ok "client running mid-session" || bad "client died early"
t0=$SECONDS; kill -TERM "$CL"
wait_exit "$CL" 25; r=$?
check "client stopped within 25s of SIGTERM (took $((SECONDS - t0))s)" test $r -eq 0
check "client exit status 0 (got ${WAIT_RC:-none})" test "${WAIT_RC:-99}" -eq 0
kill -TERM "$EX"; wait_exit "$EX" 25; r=$?
check "exchange exited on SIGTERM (status ${WAIT_RC:-none})" test $r -eq 0 -a "${WAIT_RC:-99}" -eq 0

echo "[4] no leftovers"
left=0; for p in "${PIDS[@]}"; do kill -0 "$p" 2>/dev/null && left=$((left + 1)); done
check "no tracked processes left running" test $left -eq 0

echo; [ $FAILS -eq 0 ] && echo "INTEGRATION: PASSED (logs: $RUN_DIR)" || echo "INTEGRATION: FAILED ($FAILS check(s)) (logs: $RUN_DIR)"
exit $((FAILS > 0))
