#!/bin/bash
# Build (only if needed), start the exchange exactly once, run the clients, then shut everything down gracefully.
# Default workload is the bounded laptop-safe one (see scripts/run_demo.sh). Set FULL=1 for the original unbounded workload.
# Exit codes: 0 ok | 1 a client/exchange failed or the run timed out | 2 could not start (port in use, build or exchange startup failure)
cd "$(dirname "$0")/.." || exit 2
. scripts/lib.sh
BIN=${BIN:-$PWD/cmake-build-release}
RUN_DIR=${RUN_DIR:-$PWD/run}; mkdir -p "$RUN_DIR"

if [ "${FULL:-0}" != 1 ]; then   # laptop-safe defaults, each overridable from the environment
  export ME_NUM_ORDERS=${ME_NUM_ORDERS:-300} ME_ORDER_INTERVAL_US=${ME_ORDER_INTERVAL_US:-10000}
  export ME_STARTUP_WAIT_S=${ME_STARTUP_WAIT_S:-5} ME_SILENT_LIMIT_S=${ME_SILENT_LIMIT_S:-5}
  export ME_MAX_RUNTIME_S=${ME_MAX_RUNTIME_S:-60} ME_IDLE_SLEEP_US=${ME_IDLE_SLEEP_US:-100}
fi
TOTAL_TIMEOUT_S=${TOTAL_TIMEOUT_S:-$(( ${ME_MAX_RUNTIME_S:-0} > 0 ? ME_MAX_RUNTIME_S + 90 : 0 ))}   # 0 = no overall limit

[ -x "$BIN/exchange_main" ] && [ -x "$BIN/trading_main" ] || { echo "binaries missing, building..."; bash scripts/build.sh || exit 2; }
port_open && { echo "ERROR: something is already listening on port $ORDER_PORT (another exchange running?). Not starting a second one." >&2; exit 2; }

EX_PID=""; CL_PID=""; STATUS=1
cleanup() {
  trap - INT TERM EXIT
  [ -n "$CL_PID" ] && stop_pid "$CL_PID" TERM 20
  [ -n "$EX_PID" ] && { echo "Stopping exchange (pid $EX_PID)..."; stop_pid "$EX_PID" INT "${STOP_TIMEOUT_S:-20}"; }
  exit "$STATUS"
}
trap 'STATUS=130; cleanup' INT TERM
trap cleanup EXIT

echo "Starting exchange (logs in $RUN_DIR)..."
( cd "$RUN_DIR" && exec "$BIN/exchange_main" ) > "$RUN_DIR/exchange.out" 2>&1 &
EX_PID=$!
wait_ready "$EX_PID" "${STARTUP_TIMEOUT_S:-60}"; r=$?
if [ $r -ne 0 ]; then
  [ $r -eq 1 ] && echo "ERROR: exchange exited during startup, see $RUN_DIR/exchange.out" >&2 || echo "ERROR: exchange not listening after ${STARTUP_TIMEOUT_S:-60}s" >&2
  STATUS=2; cleanup
fi
echo "Exchange is up (pid $EX_PID). Starting clients..."

bash scripts/run_clients.sh &
CL_PID=$!
SECONDS=0
while kill -0 "$CL_PID" 2>/dev/null; do
  kill -0 "$EX_PID" 2>/dev/null || { echo "ERROR: exchange died while clients were running" >&2; STATUS=1; cleanup; }
  if [ "$TOTAL_TIMEOUT_S" -gt 0 ] && [ $SECONDS -ge "$TOTAL_TIMEOUT_S" ]; then echo "ERROR: run exceeded ${TOTAL_TIMEOUT_S}s, aborting" >&2; STATUS=1; cleanup; fi
  sleep 1
done
wait "$CL_PID"; STATUS=$?; CL_PID=""
[ $STATUS -eq 0 ] && echo "Clients finished OK." || echo "Clients reported failure (status $STATUS)." >&2
cleanup
