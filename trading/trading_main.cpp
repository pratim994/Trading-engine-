#include <csignal>

#include "strategy/trade_engine.h"
#include "order_gw/order_gateway.h"
#include "market_data/market_data_consumer.h"

#include "common/logging.h"

/// Main components.
Common::Logger *logger = nullptr;
Trading::TradeEngine *trade_engine = nullptr;
Trading::MarketDataConsumer *market_data_consumer = nullptr;
Trading::OrderGateway *order_gateway = nullptr;

/// Set from the signal handler; all waits below poll it so Ctrl+C / SIGTERM trigger the normal shutdown path.
volatile std::sig_atomic_t stop_requested = 0;

void signal_handler(int) {
  stop_requested = 1;
}

/// Read a non-negative integer tunable from the environment, falling back to the (original) default.
long envLong(const char *name, long default_value) {
  const char *v = std::getenv(name);
  if (!v || !*v)
    return default_value;
  char *end = nullptr;
  const long parsed = std::strtol(v, &end, 10);
  return (end != v && *end == '\0' && parsed >= 0) ? parsed : default_value;
}

/// Sleep for up to `us` microseconds in small steps, returning early once a stop was requested.
void interruptibleSleepUs(long us) {
  while (us > 0 && !stop_requested) {
    const long step = us < 100000 ? us : 100000;
    usleep(step);
    us -= step;
  }
}

/// ./trading_main CLIENT_ID ALGO_TYPE [CLIP_1 THRESH_1 MAX_ORDER_SIZE_1 MAX_POS_1 MAX_LOSS_1] [CLIP_2 THRESH_2 MAX_ORDER_SIZE_2 MAX_POS_2 MAX_LOSS_2] ...
int main(int argc, char **argv) {
  if(argc < 3) {
    FATAL("USAGE trading_main CLIENT_ID ALGO_TYPE [CLIP_1 THRESH_1 MAX_ORDER_SIZE_1 MAX_POS_1 MAX_LOSS_1] [CLIP_2 THRESH_2 MAX_ORDER_SIZE_2 MAX_POS_2 MAX_LOSS_2] ...");
  }

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  const Common::ClientId client_id = atoi(argv[1]);
  srand(client_id);

  const auto algo_type = stringToAlgoType(argv[2]);

  logger = new Common::Logger("trading_main_" + std::to_string(client_id) + ".log");

  // Workload limits. Defaults match the original behaviour; scripts/run_demo.sh overrides them with a small laptop-safe workload.
  const long sleep_time = envLong("ME_ORDER_INTERVAL_US", 20 * 1000);     // pause after each request sent by the RANDOM algo.
  const long num_orders = envLong("ME_NUM_ORDERS", 10000);                // iterations of the RANDOM algo (each sends a NEW and a CANCEL).
  const long startup_wait_s = envLong("ME_STARTUP_WAIT_S", 10);           // time to let the market data consumer sync before trading.
  const long silent_limit_s = envLong("ME_SILENT_LIMIT_S", 60);           // exit after this many seconds without exchange activity.
  const long max_runtime_s = envLong("ME_MAX_RUNTIME_S", 0);              // hard cap on total runtime (0 = unlimited).
  const auto start_time = std::chrono::steady_clock::now();
  const auto out_of_time = [&]() {
    return max_runtime_s > 0 && std::chrono::steady_clock::now() - start_time >= std::chrono::seconds(max_runtime_s);
  };

  // The lock free queues to facilitate communication between order gateway <-> trade engine and market data consumer -> trade engine.
  Exchange::ClientRequestLFQueue client_requests(ME_MAX_CLIENT_UPDATES);
  Exchange::ClientResponseLFQueue client_responses(ME_MAX_CLIENT_UPDATES);
  Exchange::MEMarketUpdateLFQueue market_updates(ME_MAX_MARKET_UPDATES);

  std::string time_str;

  TradeEngineCfgHashMap ticker_cfg;

  // Parse and initialize the TradeEngineCfgHashMap above from the command line arguments.
  // [CLIP_1 THRESH_1 MAX_ORDER_SIZE_1 MAX_POS_1 MAX_LOSS_1] [CLIP_2 THRESH_2 MAX_ORDER_SIZE_2 MAX_POS_2 MAX_LOSS_2] ...
  size_t next_ticker_id = 0;
  for (int i = 3; i < argc; i += 5, ++next_ticker_id) {
    ticker_cfg.at(next_ticker_id) = {static_cast<Qty>(std::atoi(argv[i])), std::atof(argv[i + 1]),
                                     {static_cast<Qty>(std::atoi(argv[i + 2])),
                                      static_cast<Qty>(std::atoi(argv[i + 3])),
                                      std::atof(argv[i + 4])}};
  }

  logger->log("%:% %() % Starting Trade Engine...\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
  trade_engine = new Trading::TradeEngine(client_id, algo_type,
                                          ticker_cfg,
                                          &client_requests,
                                          &client_responses,
                                          &market_updates);
  trade_engine->start();

  const std::string order_gw_ip = "127.0.0.1";
  const std::string order_gw_iface = "lo";
  const int order_gw_port = 12345;

  logger->log("%:% %() % Starting Order Gateway...\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
  order_gateway = new Trading::OrderGateway(client_id, &client_requests, &client_responses, order_gw_ip, order_gw_iface, order_gw_port);
  order_gateway->start();

  const std::string mkt_data_iface = "lo";
  const std::string snapshot_ip = "233.252.14.1";
  const int snapshot_port = 20000;
  const std::string incremental_ip = "233.252.14.3";
  const int incremental_port = 20001;

  logger->log("%:% %() % Starting Market Data Consumer...\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
  market_data_consumer = new Trading::MarketDataConsumer(client_id, &market_updates, mkt_data_iface, snapshot_ip, snapshot_port, incremental_ip, incremental_port);
  market_data_consumer->start();

  interruptibleSleepUs(startup_wait_s * 1000 * 1000);

  trade_engine->initLastEventTime();

  // For the random trading algorithm, we simply implement it here instead of creating a new trading algorithm which is another possibility.
  // Generate random orders with random attributes and randomly cancel some of them.
  if (algo_type == AlgoType::RANDOM) {
    Common::OrderId order_id = client_id * 1000;
    std::vector<Exchange::MEClientRequest> client_requests_vec;
    std::array<Price, ME_MAX_TICKERS> ticker_base_price;
    for (size_t i = 0; i < ME_MAX_TICKERS; ++i)
      ticker_base_price[i] = (rand() % 100) + 100;
    for (long i = 0; i < num_orders && !stop_requested && !out_of_time(); ++i) {
      const Common::TickerId ticker_id = rand() % Common::ME_MAX_TICKERS;
      const Price price = ticker_base_price[ticker_id] + (rand() % 10) + 1;
      const Qty qty = 1 + (rand() % 100) + 1;
      const Side side = (rand() % 2 ? Common::Side::BUY : Common::Side::SELL);

      Exchange::MEClientRequest new_request{Exchange::ClientRequestType::NEW, client_id, ticker_id, order_id++, side,
                                            price, qty};
      trade_engine->sendClientRequest(&new_request);
      interruptibleSleepUs(sleep_time);

      client_requests_vec.push_back(new_request);
      const auto cxl_index = rand() % client_requests_vec.size();
      auto cxl_request = client_requests_vec[cxl_index];
      cxl_request.type_ = Exchange::ClientRequestType::CANCEL;
      trade_engine->sendClientRequest(&cxl_request);
      interruptibleSleepUs(sleep_time);

      if (trade_engine->silentSeconds() >= static_cast<Common::Nanos>(silent_limit_s)) {
        logger->log("%:% %() % Stopping early because been silent for % seconds...\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str), trade_engine->silentSeconds());

        break;
      }
    }
  }

  while (!stop_requested && !out_of_time() && trade_engine->silentSeconds() < static_cast<Common::Nanos>(silent_limit_s)) {
    logger->log("%:% %() % Waiting till no activity, been silent for % seconds...\n", __FILE__, __LINE__, __FUNCTION__,
                Common::getCurrentTimeStr(&time_str), trade_engine->silentSeconds());

    interruptibleSleepUs(1000 * 1000);
  }

  trade_engine->stop();
  market_data_consumer->stop();
  order_gateway->stop();

  delete trade_engine;
  trade_engine = nullptr;
  delete market_data_consumer;
  market_data_consumer = nullptr;
  delete order_gateway;
  order_gateway = nullptr;
  delete logger;
  logger = nullptr;

  return EXIT_SUCCESS;
}
