#!/usr/bin/env python3
"""Searches the parameters of the adaptive filter on the recorded grinds.

The filter is a local straight line whose length the filter chooses itself: for every reading it takes
the longest window over which a straight line still fits the readings within the noise, and reads that
line off at the newest reading. Where the weight rests, a long window fits and the result is quiet;
where the weight moves in clumps, only a short one fits and the result is quick. A straight line has no
lag on a rising weight even when it is long, which is what an average cannot do.

Two goals, measured separately:

  ruhig    how far the filter wanders while the weight really is constant - at the start before the
           first grounds land, and at the end after the grinder is off. Measured against the average of
           the readings there, which is the true weight.
  schnell  how far the filter is from the weight while it is rising. Measured against a centred moving
           average of the same readings: that one looks into the future and therefore has no lag of its
           own, so the distance to it is the lag of the filter plus the noise it lets through.

    tools/trainfilter.py                 # search on all logs, with leave-one-out check
    tools/trainfilter.py --rounds 4000   # longer random search
"""

import argparse
import glob
import itertools
import os
import random
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from grindlog import load  # noqa: E402
from plotgrind import REFERENCE_WINDOW, adaptive_filter, centred_average, line_at_end, series  # noqa: E402

def hold(t, when, values):
    """Puts a filter that only updates now and then onto the grid of the readings: it holds its value
    until the next update, and that hold is a part of what it does"""
    out, index = [], 0
    for time in t:
        while index + 1 < len(when) and when[index + 1] <= time:
            index += 1
        out.append(values[index] if values and when and when[0] <= time else (values[0] if values else 0.0))
    return out


def sections(t, values, marks):
    """Indices of the three parts: resting at the start, rising in the middle, resting at the end.

    The two resting parts are kept apart - they lie about 18 g from each other, and a common average
    over both would be a weight that never was on the scale.
    """
    off = [m["t_ms"] / 1000.0 for m in marks if m["text"].startswith("grinder_off")][0]
    start_level = statistics.fmean(values[:5])
    rising = next((x for x, value in zip(t, values) if value > start_level + 1.0), off)
    begin = [i for i, x in enumerate(t) if x < rising - 0.5]
    end = [i for i, x in enumerate(t) if x > off + 1.0]
    moving = [i for i, x in enumerate(t) if rising <= x <= off]
    return begin, end, moving


def prepare(path):
    meta, marks, end, samples = load(path)
    t, values = series((meta, marks, end, samples), False, False)
    begin, end, moving = sections(t, values, marks)
    return dict(name=meta.get("shot", "?"), t=t, values=values, begin=begin, end=end, moving=moving,
                reference=centred_average(values))


def score(log, parameters):
    """(quiet, fast) in grams - both are errors, so smaller is better"""
    return measure(log, adaptive_filter(log["t"], log["values"], *parameters)[0])


def measure(log, out):
    """(start, end, tracking) in grams for any filter output - all three are errors, smaller is better.

    The two resting parts are reported apart. The one at the start is limited by something that has
    nothing to do with the filter: the recording begins at the cup detection, so there are only about
    twenty readings before the first grounds land and no filter can have a long window yet.
    """
    def flat(part):
        if len(part) < 3:
            return 0.0
        middle = statistics.fmean([log["values"][i] for i in part])
        return (sum((out[i] - middle) ** 2 for i in part) / len(part)) ** 0.5

    moving = log["moving"]
    tracking = (sum((out[i] - log["reference"][i]) ** 2 for i in moving) / len(moving)) ** 0.5 if moving else 0.0
    return flat(log["begin"]), flat(log["end"]), tracking


def total(logs, parameters):
    """Loss and its parts. Straight at the start and at the end counts as much as quick in the middle"""
    parts = [score(log, parameters) for log in logs]
    begin, end, tracking = (statistics.fmean(column) for column in zip(*parts))
    return (begin + end) / 2 + tracking, begin, end, tracking


def search(logs, rounds, seed=1):
    """Coarse grid first, then random steps around the best of it"""
    random.seed(seed)
    grid = itertools.product((20, 30, 40, 50), (2, 3, 4), (0.10, 0.13, 0.16, 0.20, 0.25), (1.0,))
    best = min(grid, key=lambda p: total(logs, p)[0])
    best_loss = total(logs, best)[0]

    for _ in range(rounds):
        candidate = (max(4, min(60, best[0] + random.randint(-6, 6))),
                     max(2, min(8, best[1] + random.randint(-1, 1))),
                     max(0.02, min(0.6, best[2] * random.uniform(0.75, 1.33))),
                     best[3]) # the jump stays where the other filters have it, see the note in main()
        loss = total(logs, candidate)[0]
        if loss < best_loss:
            best, best_loss = candidate, loss
    return best


def show(name, parameters, logs):
    loss, begin, end, tracking = total(logs, parameters)
    print("%-22s längstes %2d  kürzestes %d  Toleranz %.3f g  Sprung %.2f g"
          % (name, parameters[0], parameters[1], parameters[2], parameters[3]))
    print("%-22s Start %.4f g   Ende %.4f g   Verfolgung %.4f g   Summe %.4f g"
          % ("", begin, end, tracking, loss))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="*", help="log files (default: logs/*.csv)")
    parser.add_argument("--rounds", type=int, default=1500, help="random steps after the grid (1500)")
    args = parser.parse_args()

    paths = args.logs or sorted(glob.glob("logs/*.csv"))
    logs = [prepare(path) for path in paths]
    if not logs:
        print("no logs found", file=sys.stderr)
        return 1

    best = search(logs, args.rounds)
    print()
    show("auf allen %d Logs:" % len(logs), best, logs)

    # Three recordings are few, so every one of them is held back once: if the parameters found without
    # it still hold on it, they describe the scale and not these three grinds
    print("\nweggelassen und darauf geprüft:")
    for index, held in enumerate(logs):
        rest = logs[:index] + logs[index + 1:]
        found = search(rest, args.rounds // 3, seed=index + 2)
        _, begin, end, tracking = total([held], found)
        _, begin_best, end_best, tracking_best = total([held], best)
        print("  ohne Shot %-4s -> längstes %2d kürzestes %d Toleranz %.3f g Sprung %.2f g"
              % (held["name"], found[0], found[1], found[2], found[3]))
        print("                    auf Shot %s: Start %.4f Ende %.4f Verfolgung %.4f"
              "  (gemeinsame Parameter: %.4f / %.4f / %.4f)"
              % (held["name"], begin, end, tracking, begin_best, end_best, tracking_best))
    print("\nzum Vergleich, dieselben zwei Maße:")
    import plotgrind as P
    others = (("alter Filter (Firmware)", lambda log: hold(log["t"], *P.old_filter(log["t"], log["values"]))),
              ("Mittel der letzten 5", lambda log: P.moving_average(log["t"], log["values"])[1]),
              ("neu, Steigung", lambda log: P.trend_filter(log["t"], log["values"], P.slope_flatness)[1]),
              ("neu, Sigma", lambda log: P.trend_filter(log["t"], log["values"], P.sigma_flatness)[1]),
              ("trainiert, ohne Kalman", lambda log: adaptive_filter(log["t"], log["values"], *best)[0]),
              ("trainiert, mit Kalman", lambda log: P.v01(log["t"], log["values"])[0]))
    for name, build in others:
        parts = [measure(log, build(log)) for log in logs]
        begin, end, tracking = (statistics.fmean(column) for column in zip(*parts))
        print("  %-24s Start %.4f   Ende %.4f   Verfolgung %.4f   Summe %.4f"
              % (name, begin, end, tracking, (begin + end) / 2 + tracking))
    return 0


if __name__ == "__main__":
    sys.exit(main())
