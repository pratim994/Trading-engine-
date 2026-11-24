// Deterministic matching-engine correctness tests. The engine is constructed but NOT started: requests are fed straight into
// processClientRequest() and the resulting client responses are read back from the response queue.
#include <cstdio>
#include <vector>

#include "matcher/matching_engine.h"

using namespace Exchange;
using namespace Common;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static std::vector<MEClientResponse> drain(ClientResponseLFQueue &q) {
  std::vector<MEClientResponse> out;
  while (q.size()) { out.push_back(*q.getNextToRead()); q.updateReadIndex(); }
  return out;
}

static size_t count(const std::vector<MEClientResponse> &v, ClientResponseType t, ClientId c) {
  size_t n = 0;
  for (const auto &r: v) n += (r.type_ == t && r.client_id_ == c);
  return n;
}

int main() {
  ClientRequestLFQueue requests(1024);
  ClientResponseLFQueue responses(1024);
  MEMarketUpdateLFQueue updates(4096);
  MatchingEngine me(&requests, &responses, &updates);

  auto send = [&](ClientRequestType t, ClientId c, TickerId tk, OrderId o, Side s, Price p, Qty q) {
    MEClientRequest r{t, c, tk, o, s, p, q};
    me.processClientRequest(&r);
    return drain(responses);
  };
  const auto N = ClientRequestType::NEW, X = ClientRequestType::CANCEL;

  // 1. Resting order is accepted.
  auto r = send(N, 1, 0, 1, Side::BUY, 100, 10);
  CHECK(r.size() == 1 && r[0].type_ == ClientResponseType::ACCEPTED && r[0].leaves_qty_ == 10);

  // 2. Partial fill: sell 4 against the resting buy of 10 -> accepted + fill for both sides, passive order keeps 6.
  r = send(N, 2, 0, 1, Side::SELL, 100, 4);
  CHECK(count(r, ClientResponseType::ACCEPTED, 2) == 1);
  CHECK(count(r, ClientResponseType::FILLED, 2) == 1);
  CHECK(count(r, ClientResponseType::FILLED, 1) == 1);
  for (const auto &x: r) if (x.type_ == ClientResponseType::FILLED && x.client_id_ == 1) CHECK(x.exec_qty_ == 4 && x.leaves_qty_ == 6);
  for (const auto &x: r) if (x.type_ == ClientResponseType::FILLED && x.client_id_ == 2) CHECK(x.exec_qty_ == 4 && x.leaves_qty_ == 0);

  // 3. Cancel the remainder, then cancelling again is rejected (key no longer present, must not throw or insert).
  r = send(X, 1, 0, 1, Side::INVALID, Price_INVALID, Qty_INVALID);
  CHECK(r.size() == 1 && r[0].type_ == ClientResponseType::CANCELED);
  r = send(X, 1, 0, 1, Side::INVALID, Price_INVALID, Qty_INVALID);
  CHECK(r.size() == 1 && r[0].type_ == ClientResponseType::CANCEL_REJECTED);

  // 4. Regression: client-supplied ids outside the table bounds used to hit .at() (std::out_of_range inside a noexcept function).
  r = send(X, 1, 0, ME_MAX_ORDER_IDS, Side::INVALID, Price_INVALID, Qty_INVALID);
  CHECK(r.size() == 1 && r[0].type_ == ClientResponseType::CANCEL_REJECTED);
  r = send(X, 1, 0, ME_MAX_ORDER_IDS + 12345, Side::INVALID, Price_INVALID, Qty_INVALID);
  CHECK(r.size() == 1 && r[0].type_ == ClientResponseType::CANCEL_REJECTED);
  r = send(X, ME_MAX_NUM_CLIENTS + 5, 0, 1, Side::INVALID, Price_INVALID, Qty_INVALID);
  CHECK(r.size() == 1 && r[0].type_ == ClientResponseType::CANCEL_REJECTED);

  // 5. Price priority: the better-priced resting bid must be hit first, regardless of arrival order.
  send(N, 3, 1, 10, Side::BUY, 100, 5);
  send(N, 3, 1, 11, Side::BUY, 101, 5);
  r = send(N, 4, 1, 10, Side::SELL, 100, 5);
  bool hit_best = false, hit_worse = false;
  for (const auto &x: r) if (x.type_ == ClientResponseType::FILLED && x.client_id_ == 3) { hit_best |= (x.client_order_id_ == 11); hit_worse |= (x.client_order_id_ == 10); }
  CHECK(hit_best && !hit_worse);

  // 6. Time priority at one price level: first-in is filled first.
  send(N, 5, 2, 20, Side::SELL, 105, 3);
  send(N, 6, 2, 21, Side::SELL, 105, 3);
  r = send(N, 7, 2, 20, Side::BUY, 105, 3);
  CHECK(count(r, ClientResponseType::FILLED, 5) == 1 && count(r, ClientResponseType::FILLED, 6) == 0);

  // 7. Non-crossing orders rest and do not trade.
  r = send(N, 8, 3, 30, Side::BUY, 90, 5);
  CHECK(r.size() == 1 && r[0].type_ == ClientResponseType::ACCEPTED);
  r = send(N, 9, 3, 30, Side::SELL, 95, 5);
  CHECK(r.size() == 1 && r[0].type_ == ClientResponseType::ACCEPTED);

  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
