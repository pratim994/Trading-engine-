#!/bin/bash
# Start the trading clients and wait for them. Called by run_exchange_and_clients.sh / run_demo.sh, or run by hand against a running exchange.
#   NUM_CLIENTS=1 (default): RANDOM client 5 only.  NUM_CLIENTS=2: also the MAKER client 1 (needs ~1.5 GB more RAM; each client uses ~1.4 GB).
# Exit code: 0 if every client exited 0, otherwise 1. Ctrl+C / SIGTERM stops the clients gracefully and reaps them.
cd "$(dirname "$0")/.." || exit 2
. scripts/lib.sh
BIN=${BIN:-$PWD/cmake-build-release}
RUN_DIR=${RUN_DIR:-$PWD/run}; mkdir -p "$RUN_DIR"; cd "$RUN_DIR" || exit 2
PIDS=()

on_signal() { for p in "${PIDS[@]}"; do kill -TERM "$p" 2>/dev/null; done; for p in "${PIDS[@]}"; do stop_pid "$p" TERM 15; done; exit 130; }
trap on_signal INT TERM

if [ "${NUM_CLIENTS:-1}" -ge 2 ]; then
# ./trading_main CLIENT_ID ALGO_TYPE [CLIP THRESH MAX_ORDER_SIZE MAX_POS MAX_LOSS] x ticker
"$BIN/trading_main" 1 MAKER \
    100 0.6 150 300 -100  60 0.6 150 300 -100  150 0.5 250 600 -100  200 0.4 500 3000 -100 \
    1000 0.9 5000 4000 -100  300 0.8 1500 3000 -100  50 0.7 150 300 -100  100 0.3 250 300 -100 > client_1.out 2>&1 &
PIDS+=($!)
sleep "${CLIENT_STAGGER_S:-2}"
fi

"$BIN/trading_main" 5 RANDOM > client_5.out 2>&1 &
PIDS+=($!)

rc=0
for p in "${PIDS[@]}"; do wait "$p" || { echo "client pid $p failed (status $?)" >&2; rc=1; }; done
exit $rc
