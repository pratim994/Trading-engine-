#!/bin/bash
# Short, bounded, laptop-safe demo: 1 exchange + 2 clients, ~300 random orders, hard cap of 60s of trading, idle backoff on busy-poll loops.
# Tunables (env): ME_NUM_ORDERS ME_ORDER_INTERVAL_US ME_STARTUP_WAIT_S ME_SILENT_LIMIT_S ME_MAX_RUNTIME_S ME_IDLE_SLEEP_US
exec "$(dirname "$0")/run_exchange_and_clients.sh" "$@"
