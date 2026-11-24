# Low-Latency Trading Exchange (C++20)

A single-machine electronic exchange: a **matching engine** with price-time priority, a **TCP order server**, **multicast market-data publisher + snapshot synthesizer**, and **trading clients** (random order flow and a market maker) that talk to it over TCP. The code is built around lock-free queues, pre-allocated memory pools, pinned/busy-polling threads and a low-overhead asynchronous logger.

```
 trading client(s)                                   exchange
┌───────────────────┐   TCP :12345   ┌──────────────┐  LF queue  ┌─────────────────┐  LF queue  ┌──────────────────────┐
│ OrderGateway      │ ─────────────▶ │ OrderServer  │ ─────────▶ │ MatchingEngine  │ ─────────▶ │ MarketDataPublisher  │
│ TradeEngine       │ ◀───────────── │ (epoll)      │ ◀───────── │ (8 order books) │            │ + SnapshotSynthesizer│
│ MarketDataConsumer│ ◀──────────────────────────────── multicast market data ───────────────────┴──────────────────────┘
└───────────────────┘
```

## Requirements
- Linux, `g++` 11+ (C++20), `cmake` ≥ 3.5, `make` or `ninja`, `python3` (for the optional report)
- **RAM: about 3.5 GB free for the default demo.** The exchange idles at ~1.8 GB and each client at ~1.4 GB (order-id tables and memory pools are sized for 1M order ids × 8 tickers).

```bash
sudo apt install -y g++ cmake ninja-build python3     # Debian / Ubuntu
```

## Quick start (copy & paste, ~2 minutes)
```bash
git clone https://github.com/pratim994/Trading-engine-.git && cd Trading-engine-
bash scripts/build.sh                      # incremental Release build -> ./cmake-build-release
bash scripts/run_demo.sh; echo "exit=$?"   # bounded demo: 1 exchange + 1 client, about 40-60 s
```
Success looks like `Clients finished OK.` and `exit=0`. Ctrl+C at any time shuts everything down cleanly.

Check what happened:
```bash
grep -c 'type:ACCEPTED' run/trading_order_gateway_5.log     # 300 orders accepted
grep -c 'type:FILLED'   run/trading_order_gateway_5.log     # matched trades
ps -eo pid,comm | grep -E 'exchange_main|trading_main' || echo "no leftover processes"
```

## Verify correctness
```bash
ROOT=$PWD
mkdir -p /tmp/me_tests && cd /tmp/me_tests              # tests write log files into the cwd
$ROOT/cmake-build-release/tests/matching_engine_test    # expect: PASSED (0 failures)
$ROOT/cmake-build-release/tests/tcp_server_test         # expect: PASSED (0 failures)
cd $ROOT
bash scripts/test_integration.sh                        # expect: INTEGRATION: PASSED (~1 min)
```

| Test | What it checks |
|---|---|
| `matching_engine_test` | Accepts, partial fills, cancels, price priority, time priority, out-of-range ids rejected without crashing |
| `tcp_server_test` | Accepted sockets are owned by the server, peer disconnects are detected and reaped, no file-descriptor leaks over repeated connect/disconnect |
| `test_integration.sh` | Real exchange + client over TCP: all orders accepted, graceful SIGINT/SIGTERM shutdown of both processes, port released, no leftover processes |

**Memory/UB checking (optional)** builds with AddressSanitizer, UBSan and libstdc++ bounds assertions. It needs noticeably more RAM:
```bash
bash scripts/build.sh --asan
mkdir -p /tmp/me_asan && cd /tmp/me_asan
$ROOT/cmake-build-asan/tests/matching_engine_test && $ROOT/cmake-build-asan/tests/tcp_server_test
cd $ROOT
```

## Visualize a run
Pick whichever suits you.


**1. Watch it live.** In a second terminal while the demo runs:
```bash
tail -f run/trading_order_gateway_5.log | grep --line-buffered -E 'FILLED|ACCEPTED'
tail -f run/exchange_matching_engine.log
```
**2. Latency analysis notebook** (per-hop latencies through the pipeline, plotly charts):
```bash
python3 -m venv .venv && . .venv/bin/activate
pip install jupyter pandas numpy plotly hvplot session_info
jupyter lab notebooks/perf_analysis.ipynb      # run after a demo run; it reads ./run/*.log
```
Latency tags are emitted by the exchange and client logs; the notebook assumes a 2.6 GHz TSC (`CPU_FREQ` in the second cell), so adjust it to your CPU for absolute numbers.

## Demo options
Everything is bounded by default so it is safe on a laptop. Override with environment variables:

| Variable | Default (demo) | Meaning |
|---|---|---|
| `NUM_CLIENTS` | `1` | `2` also starts the market-maker client (needs ~1.5 GB more RAM) |
| `ME_NUM_ORDERS` | `300` | Iterations of the random client (each sends a NEW and a CANCEL) |
| `ME_ORDER_INTERVAL_US` | `10000` | Pause between requests |
| `ME_STARTUP_WAIT_S` | `5` | Wait for market data to sync before trading |
| `ME_SILENT_LIMIT_S` | `5` | Client exits after this many seconds without exchange activity |
| `ME_MAX_RUNTIME_S` | `60` | Hard cap on client runtime (`0` = unlimited) |
| `ME_IDLE_SLEEP_US` | `100` | Backoff in the busy-poll loops. **`0` = pure busy polling** (lowest latency, ~100% CPU per thread) |

```bash
NUM_CLIENTS=2 bash scripts/run_demo.sh
ME_NUM_ORDERS=1000 bash scripts/run_demo.sh
FULL=1 bash scripts/run_exchange_and_clients.sh   # original unbounded workload (~10 min, high CPU)
```
The launcher starts the exchange exactly once (it refuses to start if port 12345 is already in use), tracks its own PIDs (no `pkill`), and returns `0` on success, `1` on failure/timeout, `2` if it could not start.

## Benchmarks
Separate from the demo and CPU-intensive, so run them on a machine with good cooling:
```bash
bash scripts/run_benchmarks.sh
```

## Repository layout
| Path | Contents |
|---|---|
| `common/` | Lock-free queue, memory pool, TCP/multicast sockets, async logger, thread utilities |
| `exchange/` | `exchange_main`, order server, matching engine and order books, market-data publisher and snapshot synthesizer |
| `trading/` | `trading_main`, order gateway, market-data consumer, trade engine, strategies (random, market maker) |
| `tests/` | Matching-engine and TCP tests (CTest-registered) |
| `scripts/` | Build, run, demo, integration test, report generator |
| `benchmarks/`, `notebooks/` | Performance benchmarks and the latency analysis notebook |

## Notes and limitations
- Runs on loopback (`lo`) with multicast on the same host; it is a single-machine demonstration, not a production venue.
- When a client disconnects, the server resets that client's sequence numbers so a reconnecting client starts a fresh session. Requests with out-of-range ids are dropped and logged (the protocol has no reject message for new orders).
- Memory footprint is dominated by order-id tables sized by `ME_MAX_ORDER_IDS` in `common/types.h`; reduce it for smaller machines.
- Logs from a run are in `run/` (git-ignored).
