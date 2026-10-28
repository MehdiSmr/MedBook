#!/usr/bin/env python3
"""crisis_feed.py: mock order feeds for the MedBook matching engine.

Writes, into --out:
  crisis_2008.csv         "2008 crisis, accelerated": Sep 2008 to Mar 2009 squeezed
                          into --seconds of feed time (default 120 s, so one calendar
                          day = 0.566 s), at about --rate messages per feed second.
  crisis_2008_trades.csv  the trades this script's own reference matcher expects for
                          that feed: ts_us,symbol,qty,price,buy_id,sell_id
  scenarios.csv           small hand-written test cases, expected results in comments

FEED FORMAT (comma-separated, no header; '#' lines are comments, blank lines ignored)
  <ts_us>,ADD,<id>,<symbol>,<BUY|SELL>,<qty>,<price>
  <ts_us>,MODIFY,<id>,<new_qty>,<new_price>
  <ts_us>,CANCEL,<id>
  ts_us   microseconds since the start of the file; never decreases. Lines with the
          same ts_us are still ordered by their position in the file.
  id      client order id chosen by the feed on ADD: a positive integer, unique within
          the file and never reused. When your engine accepts an ADD it should map
          id -> its own internal id; MODIFY/CANCEL find the order through that map.
          MODIFY/CANCEL of an id that was never accepted, or is already filled or
          cancelled, is rejected. An ADD reusing a live id is rejected.
  price   integer ticks; real price = ticks * tick size (see SYMBOLS below).

MODIFY PRIORITY CONVENTION (used by the expected results and the reference matcher):
  same price and lower qty -> keeps its place in the queue
  higher qty or new price  -> goes to the back of the queue, as if new; may trade

EVENTS USED (calendar day -> feed seconds that day occupies, at the default 120 s)
  Sep 7      3.4-4.0    Fannie Mae and Freddie Mac taken over; Lehman -45% on Sep 9 (4.5 s)
  Sep 15     7.9-8.5    Lehman files Chapter 11 (LEH -95%); BofA buys Merrill; AIG -60%
  Sep 16     8.5-9.1    AIG gets an $85B Fed loan
  Sep 17     9.1-9.6    Gold +$70 to $850 (record one-day gain); NYSE suspends LEH:
                        at 9.6 s every resting LEH order is cancelled, LEH never trades again
  Sep 18-19  9.6-10.8   TARP proposed + short-selling ban: rally
  Sep 29     15.8-16.4  House rejects TARP: Dow -778, S&P -8.8%
  Sep 30     16.4-17.0  Rebound
  Oct 6-10   19.8-22.6  Worst week of the crisis: S&P -18%
  Oct 13     23.8-24.3  S&P +11.6%, its biggest one-day gain
  Oct 15     24.9-25.5  S&P -9%
  Oct 24     30.0-30.6  Euro near 1.25, gold near its crisis low (~$712)
  Oct 28     32.3-32.8  S&P +10.8%
  Nov 20     45.3-45.8  S&P closes at 752, lowest since 1997
  Nov 24     47.5-48.1  Citigroup rescue: C +58%
  Dec 1      51.5-52.1  S&P -8.9% (recession officially declared)
  Dec 18     61.1-61.7  Calmer holiday period; euro rallies to ~1.47
  Jan 20     79.8-80.4  Inauguration-day bank selloff: BAC -29%
  Feb 17-20  95.7-97.9  S&P -4.6% on Feb 17; gold reaches $1,000 (Feb 20)
  Mar 2-9    103.0-107.5  AIG reports a $61.7B loss; C closes at $1.02 (Mar 5);
                        S&P closes at 676.53 on Mar 9, the bottom; AIG ~$0.35
  Mar 10     107.5-108.1  Citi says it is profitable: S&P +6.4%, banks +30-40%
  Mar 23     114.9-115.5  Treasury's toxic-asset plan: S&P +7.1%
  Prices are approximate daily levels (pre-split for AIG and C), shaped from reported
  closes; between keyframes the path is log-linear plus regime-scaled noise.

Usage:
  python crisis_feed.py [--rate 50000] [--seconds 120] [--seed 42] [--out .]
Standard library only. At the defaults: 6M lines, ~190 MB feed + ~23 MB trades, ~40 s.
Size and run time scale linearly with --rate x --seconds.
"""
import argparse, bisect, heapq, math, os, random, time
from collections import namedtuple
from datetime import date

# symbol: (tick size, share of message flow, volatility multiplier)
SYMBOLS = {
    "SPX":     (0.01,    3.0, 1.0),
    "LEH":     (0.01,    2.0, 4.0),
    "AIG":     (0.01,    2.0, 4.0),
    "C":       (0.01,    2.0, 3.0),
    "BAC":     (0.01,    2.0, 3.0),
    "GS":      (0.01,    2.0, 2.5),
    "EUR/USD": (0.00001, 2.0, 0.4),
    "GOLD":    (0.10,    1.5, 0.8),
}
HALTS = {"LEH": "2008-09-17"}  # stops trading at the end of this day

# Approximate closing prices ("date": price). Each value is the level at the END of that day.
KEYFRAMES = {
    "SPX": {"2008-08-31": 1282.8, "2008-09-12": 1251.7, "2008-09-15": 1192.7, "2008-09-17": 1156.4,
            "2008-09-19": 1255.1, "2008-09-26": 1213.3, "2008-09-29": 1106.4, "2008-09-30": 1166.4,
            "2008-10-03": 1099.2, "2008-10-10": 899.2, "2008-10-13": 1003.4, "2008-10-15": 907.8,
            "2008-10-27": 848.9, "2008-10-28": 940.5, "2008-11-04": 1005.8, "2008-11-20": 752.4,
            "2008-11-24": 851.8, "2008-11-28": 896.2, "2008-12-01": 816.2, "2008-12-31": 903.3,
            "2009-01-06": 934.7, "2009-01-20": 805.2, "2009-02-09": 869.9, "2009-02-17": 789.2,
            "2009-02-27": 735.1, "2009-03-09": 676.5, "2009-03-10": 719.6, "2009-03-20": 768.5,
            "2009-03-23": 822.9, "2009-03-31": 797.9},
    "LEH": {"2008-08-31": 16.09, "2008-09-08": 14.15, "2008-09-09": 7.79, "2008-09-10": 7.25,
            "2008-09-11": 4.22, "2008-09-14": 3.65, "2008-09-15": 0.19, "2008-09-16": 0.30,
            "2008-09-17": 0.13},
    "AIG": {"2008-08-31": 21.49, "2008-09-12": 12.14, "2008-09-15": 4.76, "2008-09-16": 3.75,
            "2008-09-17": 2.05, "2008-09-18": 2.69, "2008-09-19": 3.85, "2008-10-08": 1.44,
            "2008-10-31": 1.92, "2008-11-20": 1.45, "2008-12-31": 1.57, "2009-01-31": 1.25,
            "2009-02-27": 0.42, "2009-03-05": 0.35, "2009-03-09": 0.35, "2009-03-10": 0.45,
            "2009-03-31": 1.00},
    "C":   {"2008-08-31": 18.99, "2008-09-15": 15.50, "2008-09-19": 22.00, "2008-09-29": 17.00,
            "2008-10-10": 14.00, "2008-10-13": 17.60, "2008-11-04": 14.00, "2008-11-21": 3.77,
            "2008-11-24": 5.95, "2008-12-31": 6.71, "2009-01-16": 3.50, "2009-01-20": 2.80,
            "2009-02-20": 1.95, "2009-02-27": 1.50, "2009-03-05": 1.02, "2009-03-09": 1.05,
            "2009-03-10": 1.45, "2009-03-31": 2.53},
    "BAC": {"2008-08-31": 31.13, "2008-09-15": 26.55, "2008-09-19": 37.48, "2008-09-29": 32.50,
            "2008-10-10": 22.00, "2008-10-13": 26.00, "2008-11-20": 11.25, "2008-11-24": 16.00,
            "2008-12-31": 14.08, "2009-01-16": 7.18, "2009-01-20": 5.10, "2009-02-20": 3.79,
            "2009-03-06": 3.14, "2009-03-09": 3.40, "2009-03-10": 4.84, "2009-03-31": 6.82},
    "GS":  {"2008-08-31": 163.97, "2008-09-15": 135.5, "2008-09-17": 108.0, "2008-09-19": 133.0,
            "2008-09-23": 125.0, "2008-10-10": 88.0, "2008-10-13": 105.0, "2008-10-31": 92.5,
            "2008-11-20": 52.0, "2008-11-24": 70.0, "2008-11-28": 79.0, "2008-12-31": 84.4,
            "2009-01-20": 64.0, "2009-02-27": 91.1, "2009-03-09": 79.0, "2009-03-31": 106.0},
    "EUR/USD": {"2008-08-31": 1.4673, "2008-09-10": 1.3947, "2008-09-16": 1.4120,
            "2008-09-17": 1.4311, "2008-09-22": 1.4740, "2008-09-30": 1.4100, "2008-10-10": 1.3420,
            "2008-10-24": 1.2530, "2008-10-27": 1.2490, "2008-11-04": 1.2970, "2008-11-20": 1.2440,
            "2008-12-01": 1.2590, "2008-12-18": 1.4700, "2008-12-31": 1.3970, "2009-01-20": 1.2880,
            "2009-02-18": 1.2550, "2009-03-04": 1.2550, "2009-03-18": 1.3600, "2009-03-31": 1.3250},
    "GOLD": {"2008-08-31": 833.0, "2008-09-11": 740.0, "2008-09-16": 780.5, "2008-09-17": 850.5,
            "2008-09-19": 860.0, "2008-09-29": 895.0, "2008-10-10": 910.0, "2008-10-24": 712.0,
            "2008-10-31": 730.0, "2008-11-28": 815.0, "2008-12-31": 869.8, "2009-01-15": 805.0,
            "2009-02-20": 1001.0, "2009-03-09": 940.0, "2009-03-18": 900.0, "2009-03-31": 922.0},
}

# Market regimes. Spread, offset and sweep are in basis points of the mid price.
Regime = namedtuple("Regime", [
    "rate",       # message-rate multiplier (normalised so the total matches --rate)
    "burst",      # how bursty the arrival rate is
    "spread",     # quoted spread new passive orders are placed around
    "offset",     # mean extra distance of passive orders behind the touch (book depth)
    "mix",        # base (ADD, CANCEL, MODIFY) weights before depth feedback
    "aggr",       # share of ADDs that cross the spread
    "imbalance",  # how strongly order flow follows the price direction (0 = 50/50)
    "depth",      # target resting orders per symbol; cancels rise above it, fall below it
    "pull",       # share of CANCELs that pull quotes near the touch (vs stale far orders)
    "vol",        # noise volatility around the historical path
    "sweep",      # how far aggressive orders reach past the touch
    "aqty",       # size multiplier for aggressive orders
])
REGIMES = {
    #               rate burst spr off  (add, cancel, modify)  aggr  imb  depth pull vol  sweep aqty
    "calm":    Regime(0.6, 0.02,  2,  8, (0.58, 0.27, 0.15),   0.05, 0.10, 4000, 0.10, 0.25,  2, 1.0),
    "stress":  Regime(1.0, 0.04,  6, 14, (0.50, 0.32, 0.18),   0.12, 0.30,  400, 0.30, 0.60,  8, 1.5),
    "panic":   Regime(2.0, 0.10, 20, 30, (0.44, 0.48, 0.08),   0.35, 0.45,   20, 0.60, 1.50, 40, 3.0),
    "rebound": Regime(1.6, 0.07, 10, 18, (0.48, 0.36, 0.16),   0.30, 0.45,  150, 0.30, 1.00, 25, 2.5),
}

# (first day, regime): a regime lasts until the next row. See EVENTS USED above.
TIMELINE = [
    ("2008-09-01", "calm"), ("2008-09-08", "stress"), ("2008-09-15", "panic"),
    ("2008-09-18", "rebound"), ("2008-09-22", "stress"), ("2008-09-29", "panic"),
    ("2008-09-30", "rebound"), ("2008-10-01", "stress"), ("2008-10-06", "panic"),
    ("2008-10-13", "rebound"), ("2008-10-14", "stress"), ("2008-10-15", "panic"),
    ("2008-10-16", "stress"), ("2008-10-28", "rebound"), ("2008-10-29", "stress"),
    ("2008-11-19", "panic"), ("2008-11-21", "rebound"), ("2008-11-26", "stress"),
    ("2008-12-01", "panic"), ("2008-12-02", "stress"), ("2008-12-15", "calm"),
    ("2009-01-07", "stress"), ("2009-01-20", "panic"), ("2009-01-21", "stress"),
    ("2009-02-17", "panic"), ("2009-02-18", "stress"), ("2009-03-02", "panic"),
    ("2009-03-10", "rebound"), ("2009-03-14", "stress"), ("2009-03-23", "rebound"),
    ("2009-03-24", "stress"),
]

START, END = date(2008, 8, 31), date(2009, 3, 31)   # t = 0 is the close of Aug 31
DAYS = (END - START).days
SLOT_US = 1000          # simulation step: 1 ms of feed time
NOISE_TAU = 0.25        # noise mean-reversion time, feed seconds
BASE_SD = 0.01          # typical noise size (1%) before regime/symbol multipliers
DRIFT_SLOTS = 200       # order flow looks at the path's direction over the next 0.2 s
MEAN_QTY = 20


def day_index(iso):
    return (date.fromisoformat(iso) - START).days


def build_paths(n_slots):
    """Per-slot regime index, target log price in ticks, and short-term drift."""
    names = list(REGIMES)
    starts = [day_index(d) - 1 for d, _ in TIMELINE]   # a day d spans [d-1, d) in day units
    days = [(s + 0.5) * DAYS / n_slots for s in range(n_slots)]
    regime = [names.index(TIMELINE[max(0, bisect.bisect_right(starts, x) - 1)][1]) for x in days]
    target, drift = {}, {}
    for sym, kf in KEYFRAMES.items():
        tick = SYMBOLS[sym][0]
        xs = [day_index(d) for d in kf]
        ys = [math.log(p / tick) for p in kf.values()]
        path, j = [], 0
        for x in days:
            while j < len(xs) - 1 and xs[j + 1] <= x:
                j += 1
            if j == len(xs) - 1 or x <= xs[0]:
                path.append(ys[j] if x > xs[0] else ys[0])
            else:
                f = (x - xs[j]) / (xs[j + 1] - xs[j])
                path.append(ys[j] + f * (ys[j + 1] - ys[j]))
        target[sym] = path
        drift[sym] = [path[min(s + DRIFT_SLOTS, n_slots - 1)] - path[s] for s in range(n_slots)]
    halt = {sym: min(n_slots, math.ceil(day_index(d) * n_slots / DAYS)) for sym, d in HALTS.items()}
    return regime, target, drift, halt


def slot_counts(rng, regime, total):
    """Bursty message counts per 1 ms slot, summing to `total`."""
    regs, b, w = list(REGIMES.values()), 0.0, []
    for ri in regime:
        b = 0.99 * b + regs[ri].burst * rng.gauss(0, 1)
        w.append(regs[ri].rate * math.exp(b))
    scale, acc, counts = total / sum(w), 0.0, []
    for x in w:
        acc += x * scale
        counts.append(int(acc))
        acc -= int(acc)
    return counts


class Book:
    """Small reference matcher: price-time priority, trades at the resting price."""

    def __init__(self):
        self.orders = {}   # id -> [symbol, is_buy, qty, price, seq, touched]
        self.heaps = {}    # (symbol, is_buy) -> heap of (price key, seq, id)
        self.count = {}    # symbol -> number of resting orders
        self.seq = 0
        self.trades = []   # (symbol, qty, price, buy_id, sell_id); drained by the caller

    def _push(self, oid):
        o = self.orders[oid]
        self.seq += 1
        o[4] = self.seq
        heapq.heappush(self.heaps.setdefault((o[0], o[1]), []),
                       (-o[3] if o[1] else o[3], self.seq, oid))

    def best(self, sym, is_buy):
        h = self.heaps.get((sym, is_buy), [])
        while h:   # drop entries of cancelled, filled or re-queued orders
            _, seq, oid = h[0]
            o = self.orders.get(oid)
            if o is not None and o[4] == seq:
                return oid
            heapq.heappop(h)
        return None

    def _match(self, oid):
        o = self.orders[oid]
        sym, is_buy = o[0], o[1]
        while o[2] > 0:
            rid = self.best(sym, not is_buy)
            if rid is None:
                break
            r = self.orders[rid]
            if (r[3] > o[3]) if is_buy else (r[3] < o[3]):
                break
            q = min(o[2], r[2])
            o[2] -= q
            r[2] -= q
            o[5] = r[5] = True
            self.trades.append((sym, q, r[3], oid, rid) if is_buy else (sym, q, r[3], rid, oid))
            if r[2] == 0:
                del self.orders[rid]
                self.count[sym] -= 1
        if o[2] == 0:
            del self.orders[oid]
            self.count[sym] -= 1
        else:
            self._push(oid)

    def add(self, oid, sym, is_buy, qty, price):
        self.orders[oid] = [sym, is_buy, qty, price, 0, False]
        self.count[sym] = self.count.get(sym, 0) + 1
        self._match(oid)

    def cancel(self, oid):
        self.count[self.orders.pop(oid)[0]] -= 1

    def modify(self, oid, qty, price):   # caller guarantees the order is not partially filled
        o = self.orders[oid]
        if price == o[3] and qty <= o[2]:
            o[2] = qty                          # keeps its place
        else:
            o[2], o[3], o[4] = qty, price, -1   # old queue entry becomes stale
            self._match(oid)


def generate(feed_path, trades_path, rate, seconds, seed):
    rng = random.Random(seed)
    n_slots = round(seconds * 1e6 / SLOT_US)
    regime, target, drift, halt = build_paths(n_slots)
    counts = slot_counts(rng, regime, rate * seconds)
    regs = list(REGIMES.values())
    book, live = Book(), {s: [] for s in SYMBOLS}
    active = list(SYMBOLS)
    pool = [s for s in active for _ in range(int(SYMBOLS[s][1] * 2))]
    noise = dict.fromkeys(SYMBOLS, 0.0)
    mid, sell_p = {}, {}
    decay = math.exp(-SLOT_US / 1e6 / NOISE_TAU)
    kick = math.sqrt(1 - decay * decay) * BASE_SD
    next_id = 1
    # per regime: messages, ADD, CANCEL, MODIFY, aggressive ADD, depth samples, depth sum,
    #             spread samples, spread sum (bps)
    stats = [[0] * 9 for _ in regs]

    def candidates(sym, k, ok):
        """Up to k random live orders of sym passing ok(); dead ids are swept out lazily."""
        lst, out = live[sym], []
        for _ in range(3 * k):
            if not lst or len(out) == k:
                break
            i = rng.randrange(len(lst))
            o = book.orders.get(lst[i])
            if o is None:
                lst[i] = lst[-1]
                lst.pop()
            elif ok(o):
                out.append(lst[i])
        return out

    def passive_price(sym, is_buy, price):
        """Pull a passive price back so it rests instead of trading."""
        opp = book.best(sym, not is_buy)
        if opp is None:
            return price
        p = book.orders[opp][3]
        return min(price, p - 1) if is_buy else max(price, p + 1)

    feed = open(feed_path, "w")
    trades_f = open(trades_path, "w")
    ticks = " ".join(f"{s}={SYMBOLS[s][0]:g}" for s in SYMBOLS)
    feed.write(f'# MedBook feed "2008 crisis, accelerated": seed={seed} rate={rate}/s seconds={seconds}\n'
               "# ts_us,ADD,id,symbol,BUY|SELL,qty,price | ts_us,MODIFY,id,qty,price | ts_us,CANCEL,id\n"
               f"# tick sizes (real price = ticks * tick): {ticks}\n")
    trades_f.write("# ts_us,symbol,qty,price,buy_id,sell_id\n")
    out, tout, n_lines = [], [], 0

    for s in range(n_slots):
        R = regs[regime[s]]
        st = stats[regime[s]]
        ts0 = s * SLOT_US
        for sym in [x for x in active if halt.get(x) == s]:   # delisting: pull everything
            for oid in list(live[sym]):
                if oid in book.orders:
                    book.cancel(oid)
                    out.append(f"{ts0},CANCEL,{oid}")
            active.remove(sym)
            pool = [x for x in pool if x != sym]
        for sym in active:   # move each mid: historical path + mean-reverting noise
            noise[sym] = noise[sym] * decay + kick * R.vol * SYMBOLS[sym][2] * rng.gauss(0, 1)
            mid[sym] = max(1, round(math.exp(target[sym][s] + noise[sym])))
            sell_p[sym] = 0.5 - R.imbalance * math.tanh(drift[sym][s] / 0.01)
        if s % 50 == 0:      # sample book depth (all symbols) and SPX spread for the summary
            for sym in active:
                st[5] += 1
                st[6] += book.count.get(sym, 0)
            b, a = book.best("SPX", True), book.best("SPX", False)
            if b is not None and a is not None:
                st[7] += 1
                st[8] += (book.orders[a][3] - book.orders[b][3]) / mid["SPX"] * 1e4
        n = counts[s]
        for i in range(n):
            ts = ts0 + i * SLOT_US // n
            sym = rng.choice(pool)
            m = mid[sym]
            half = max(1, int(m * R.spread / 2e4))
            off_mean = max(0.3, m * R.offset / 1e4)
            pa, pc, pm = R.mix
            pc *= min(3.0, max(0.5, book.count.get(sym, 0) / R.depth))   # steer toward target depth
            r = rng.random() * (pa + pc + pm)
            line = None
            if r >= pa + pc:                                         # MODIFY
                c = candidates(sym, 1, lambda o: not o[5])
                if c:
                    oid = c[0]
                    o = book.orders[oid]
                    sign = 1 if o[1] else -1
                    u = rng.random()
                    if u < R.aggr / 2:      # reprice through the spread
                        price = m + sign * (half + rng.randint(0, int(m * R.sweep / 1e4)))
                        qty = o[2]
                    elif u < 0.55:          # change size, same price
                        price, qty = o[3], max(1, int(o[2] * rng.uniform(0.3, 1.7)))
                    else:                   # requote near the current mid, post-only
                        price = passive_price(sym, o[1], m - sign * (half + int(rng.expovariate(1 / off_mean))))
                        qty = o[2]
                    price = max(1, price)
                    book.modify(oid, qty, price)
                    line = f"{ts},MODIFY,{oid},{qty},{price}"
                    st[3] += 1
            elif r >= pa:                                            # CANCEL
                c = candidates(sym, 4, lambda o: True)
                if c:
                    if rng.random() < R.pull:   # pull quotes at the touch on the side under pressure
                        hit_bids = sell_p[sym] > 0.5
                        oid = min(c, key=lambda x: (book.orders[x][1] != hit_bids,
                                                    abs(book.orders[x][3] - m)))
                    else:                       # clean up stale orders far from the mid
                        oid = max(c, key=lambda x: abs(book.orders[x][3] - m))
                    book.cancel(oid)
                    line = f"{ts},CANCEL,{oid}"
                    st[2] += 1
            if line is None:                                         # ADD
                is_buy = rng.random() >= sell_p[sym]
                sign = 1 if is_buy else -1
                qty = 1 + int(rng.expovariate(1 / MEAN_QTY))
                if rng.random() < R.aggr:
                    price = m + sign * (half + rng.randint(0, int(m * R.sweep / 1e4)))
                    qty = max(1, int(qty * R.aqty))
                    st[4] += 1
                else:   # passive: never crosses the opposite best (post-only)
                    price = passive_price(sym, is_buy, m - sign * (half + int(rng.expovariate(1 / off_mean))))
                price = max(1, price)
                oid, next_id = next_id, next_id + 1
                book.add(oid, sym, is_buy, qty, price)
                live[sym].append(oid)
                line = f"{ts},ADD,{oid},{sym},{'BUY' if is_buy else 'SELL'},{qty},{price}"
                st[1] += 1
            out.append(line)
            st[0] += 1
            if book.trades:
                tout.extend(f"{ts},{t[0]},{t[1]},{t[2]},{t[3]},{t[4]}" for t in book.trades)
                book.trades.clear()
        if len(out) > 50_000 or s == n_slots - 1:
            feed.write("\n".join(out) + "\n")
            n_lines += len(out)
            out = []
            if tout:
                trades_f.write("\n".join(tout) + "\n")
                tout = []
    feed.close()
    trades_f.close()
    return n_lines, stats


SCENARIOS = """\
# MedBook test scenarios. Format as in crisis_feed.py: ts_us,COMMAND,...; prices in ticks.
# Trades are written TRADE <qty>@<price> buy=<id> sell=<id>. Each scenario ends with
# cleanup CANCELs so every book is empty before the next one starts.

# === S1 adds that don't cross: book just builds up ===
# expect: no trades; C bids 10@100(#1) 5@99(#2), asks 7@101(#3) 4@102(#4)
ADD,1,C,BUY,10,100
ADD,2,C,BUY,5,99
ADD,3,C,SELL,7,101
ADD,4,C,SELL,4,102
CANCEL,1
CANCEL,2
CANCEL,3
CANCEL,4

# === S2 exact full fill ===
# expect: TRADE 10@100 buy=6 sell=5; book empty
ADD,5,C,SELL,10,100
ADD,6,C,BUY,10,100

# === S3 partial fill of the resting order (and trade at the resting price) ===
# expect: TRADE 4@100 buy=8 sell=7 (not 101); #7 rests SELL 6@100
ADD,7,C,SELL,10,100
ADD,8,C,BUY,4,101
CANCEL,7

# === S4 aggressor remainder rests ===
# expect: TRADE 5@100 buy=10 sell=9; #10 rests BUY 3@100
ADD,9,C,SELL,5,100
ADD,10,C,BUY,8,100
CANCEL,10

# === S5 one aggressive order sweeps several levels and stops at its limit ===
# expect: TRADE 5@200 buy=15 sell=11, TRADE 5@201 buy=15 sell=12,
#         TRADE 5@202 buy=15 sell=13; #15 rests BUY 3@202; #14 untouched 5@204
ADD,11,GS,SELL,5,200
ADD,12,GS,SELL,5,201
ADD,13,GS,SELL,5,202
ADD,14,GS,SELL,5,204
ADD,15,GS,BUY,18,202
CANCEL,14
CANCEL,15

# === S6 several orders at the same price are filled oldest first ===
# expect: TRADE 5@500 buy=16 sell=19, TRADE 5@500 buy=17 sell=19,
#         TRADE 2@500 buy=18 sell=19; #18 left BUY 3@500
ADD,16,SPX,BUY,5,500
ADD,17,SPX,BUY,5,500
ADD,18,SPX,BUY,5,500
ADD,19,SPX,SELL,12,500
CANCEL,18

# === S7 MODIFY quantity down, same price (keeps priority) ===
# expect: MODIFY accepted, no trade; then TRADE 4@300 buy=22 sell=20,
#         TRADE 2@300 buy=22 sell=21; #21 left SELL 8@300
ADD,20,EUR/USD,SELL,10,300
ADD,21,EUR/USD,SELL,10,300
MODIFY,20,4,300
ADD,22,EUR/USD,BUY,6,300
CANCEL,21

# === S8 MODIFY quantity up, same price (loses priority) ===
# expect: MODIFY accepted, #23 moves behind #24; then TRADE 5@300 buy=25 sell=24,
#         TRADE 1@300 buy=25 sell=23; #23 left SELL 7@300
#         (if your engine keeps priority on qty-up: TRADE 6@300 buy=25 sell=23)
ADD,23,EUR/USD,SELL,5,300
ADD,24,EUR/USD,SELL,5,300
MODIFY,23,8,300
ADD,25,EUR/USD,BUY,6,300
CANCEL,23

# === S9 MODIFY price change that crosses ===
# expect: MODIFY trades: TRADE 5@410 buy=27 sell=26 (resting price, not 412);
#         #27 rests BUY 3@412
ADD,26,BAC,SELL,5,410
ADD,27,BAC,BUY,8,405
MODIFY,27,8,412
CANCEL,27

# === S10 MODIFY price change that doesn't cross (and joins the back of the new level) ===
# expect: MODIFY accepted, no trade, #29 queued behind #30 at 407; then
#         TRADE 3@407 buy=30 sell=31, TRADE 1@407 buy=29 sell=31; #29 left BUY 7@407
ADD,28,BAC,SELL,5,410
ADD,29,BAC,BUY,8,405
ADD,30,BAC,BUY,3,407
MODIFY,29,8,407
ADD,31,BAC,SELL,4,407
CANCEL,28
CANCEL,29

# === S11 MODIFY of a partially filled order is rejected ===
# expect: TRADE 4@2000 buy=33 sell=32; MODIFY REJECTED; #32 still SELL 6@2000
ADD,32,GOLD,SELL,10,2000
ADD,33,GOLD,BUY,4,2000
MODIFY,32,10,2000
CANCEL,32

# === S12 CANCEL of a live order ===
# expect: CANCEL accepted; the SELL then rests with no trade (#34 is gone)
ADD,34,AIG,BUY,5,150
CANCEL,34
ADD,35,AIG,SELL,5,150
CANCEL,35

# === S13 CANCEL/MODIFY of orders that are no longer live ===
# expect: all 4 REJECTED: #34 cancelled (S12), #5 filled (S2), #34 again, #999 never existed
CANCEL,34
CANCEL,5
MODIFY,34,5,150
CANCEL,999

# === S14 bad input ===
# expect: every line REJECTED except ADD 42 and its final CANCEL; no trades; books empty.
# In order: unknown symbol, qty 0, qty < 0, price 0, price < 0, bad side, (valid ADD 42),
# MODIFY qty 0, MODIFY price < 0, duplicate live id, cancel of a rejected id, too few
# fields, non-numeric qty, price > int32, empty id, unknown command, missing id,
# non-numeric timestamp, missing timestamp, (valid cleanup).
ADD,36,WAMU,BUY,5,100
ADD,37,C,BUY,0,100
ADD,38,C,BUY,-5,100
ADD,39,C,BUY,5,0
ADD,40,C,SELL,5,-100
ADD,41,C,HOLD,5,100
ADD,42,C,BUY,5,100
MODIFY,42,0,100
MODIFY,42,5,-1
ADD,42,C,SELL,1,200
CANCEL,37
ADD,43,C,BUY,5
ADD,44,C,BUY,five,100
ADD,45,C,BUY,5,3000000000
ADD,,C,BUY,5,100
FOO,46,C,BUY,5,100
CANCEL
!abc,ADD,47,C,BUY,5,100
!ADD,48,C,BUY,5,100
CANCEL,42
"""


def write_scenarios(path):
    """Prefix each command with a timestamp 1 ms apart; '!' lines are written as-is."""
    out, n = [], 0
    for line in SCENARIOS.splitlines():
        if not line or line.startswith("#"):
            out.append(line)
        elif line.startswith("!"):
            out.append(line[1:])
        else:
            n += 1
            out.append(f"{n * 1000},{line}")
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")


def main():
    ap = argparse.ArgumentParser(description="Generate MedBook mock order feeds.")
    ap.add_argument("--rate", type=int, default=50_000, help="messages per second of feed time")
    ap.add_argument("--seconds", type=float, default=120, help="feed duration (history is compressed into it)")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--out", default=".", help="output directory")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    paths = [os.path.join(a.out, f) for f in ("crisis_2008.csv", "crisis_2008_trades.csv", "scenarios.csv")]
    t0 = time.time()
    write_scenarios(paths[2])
    n_lines, stats = generate(paths[0], paths[1], a.rate, a.seconds, a.seed)
    print(f"done in {time.time() - t0:.0f} s; {n_lines:,} feed lines")
    for p in paths:
        print(f"  {p}: {os.path.getsize(p) / 1e6:,.1f} MB")
    print("regime    msgs     ADD  CANCEL  MODIFY  aggr-ADD  resting/sym  SPX spread(bps)")
    tot = [0] * 4
    for name, st in zip(REGIMES, stats):
        if st[0]:
            tot = [x + y for x, y in zip(tot, st[:4])]
            print(f"{name:8} {st[0]:>9,} {st[1]/st[0]:5.0%} {st[2]/st[0]:6.0%} {st[3]/st[0]:6.0%}"
                  f" {st[4]/max(1, st[1]):8.0%} {st[6]/max(1, st[5]):11.0f} {st[8]/max(1, st[7]):11.1f}")
    print(f"overall  {tot[0]:>9,} {tot[1]/tot[0]:5.0%} {tot[2]/tot[0]:6.0%} {tot[3]/tot[0]:6.0%}")


if __name__ == "__main__":
    main()
