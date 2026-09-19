#!/usr/bin/env python3
"""Trains the formula that says how sure the filter can be that the weight is standing still.

The value is a probability between 0 and 1: 1 means the readings are what a resting scale looks like,
0 means the weight is moving. It is fitted, not set by hand.

The truth it is fitted against cannot be seen by a filter: it comes from a centred average, which looks
as far into the future as into the past and therefore knows the real course of the weight. Where that
course changes by less than STANDING g/s the scale was standing, where it changes by more than MOVING
g/s it was not; everything between the two is left out of the training, because it is not clearly one
or the other.

What the formula may look at are four spreads of the readings, over the last 5, 10, 20 and 40 of them.
The short one reacts at once, the long ones only become small once the weight has been standing for a
while - which is where "the longer it is straight, the surer one can be" comes from, without a counter
and without a threshold. Where there are not yet that many readings, or a step has just emptied the
buffer, the long spreads count as UNKNOWN, so the formula starts out unsure after every step.

    tools/trainflat.py                  # fit on all logs, with leave-one-out check
    tools/trainflat.py --steps 40000    # longer fit
"""

import argparse
import glob
import math
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from grindlog import load  # noqa: E402
from plotgrind import series  # noqa: E402
from plotgrind import standing_truth  # noqa: E402

WINDOWS = (5, 10, 20, 40) # readings the spreads are taken over
UNKNOWN = 1.0  # g, the spread of a window that does not have its readings yet
RIDGE = 0.01   # pulls the weights towards zero. Kept small on purpose: more of it does raise the share
               # of readings sorted correctly, but it squeezes the answer towards the middle - at 0.5 a
               # standing scale only reached 0.59 instead of 0.87, and the value, not the yes/no, is
               # what this formula is for


def spreads(values, index):
    """The features: the spread of the readings over each window, ending at `index`"""
    out = []
    for window in WINDOWS:
        first = index - window + 1
        out.append(statistics.stdev(values[first:index + 1]) if first >= 0 and window >= 2 else UNKNOWN)
    return out


def samples(path):
    meta, marks, end, values = load(path)
    t, weights = series((meta, marks, end, values), False, False)
    labels = standing_truth(t, weights)
    rows = [(spreads(weights, i), label) for i, label in enumerate(labels) if label is not None]
    return meta.get("shot", "?"), rows, len(labels)


def logistic(z):
    return 1.0 / (1.0 + math.exp(-max(-30.0, min(30.0, z))))


def fit(rows, steps, rate=0.5, ridge=RIDGE, downhill=False):
    """Logistic regression by gradient descent on the standardised features.

    Standing readings are far rarer than moving ones, so each class carries the same total weight -
    otherwise the fit would answer "moving" to everything and still be right most of the time.
    """
    columns = list(zip(*[features for features, _ in rows]))
    middle = [statistics.fmean(column) for column in columns]
    spread = [statistics.pstdev(column) or 1.0 for column in columns]
    standing = sum(1 for _, label in rows if label >= 0.5) or 1
    moving = len(rows) - standing or 1
    data = [([(x - m) / s for x, m, s in zip(features, middle, spread)], label,
             0.5 / (standing if label >= 0.5 else moving)) for features, label in rows]

    weights = [0.0] * len(WINDOWS)
    bias = 0.0
    for _ in range(steps):
        gradient = [0.0] * len(weights)
        gradient_bias = 0.0
        for features, label, share in data:
            error = logistic(bias + sum(w * x for w, x in zip(weights, features))) - label
            for index, x in enumerate(features):
                gradient[index] += share * error * x
            gradient_bias += share * error
        weights = [w - rate * (g + ridge * w) for w, g in zip(weights, gradient)]
        if downhill:
            # Optional: more spread may then never mean more standing. It is off, because it costs
            # twenty points at recognising movement - the small positive weight on the longest window
            # does real work, it is what tells "just came to rest" from "has been moving all along"
            weights = [min(0.0, w) for w in weights]
        bias -= rate * gradient_bias

    # Fold the standardisation back in, so the formula works on the plain spreads in grams
    plain = [w / s for w, s in zip(weights, spread)]
    return plain, bias - sum(w * m / s for w, m, s in zip(weights, middle, spread))


def probability(features, weights, bias):
    return logistic(bias + sum(w * x for w, x in zip(weights, features)))


def quality(rows, weights, bias):
    """Share sorted correctly in each class on its own, and the average distance to the right answer.

    Both classes apart, because there are five times as many moving readings as standing ones and a
    single share over all of them would hide how the rare one does.
    """
    hit = {1.0: [0, 0], 0.0: [0, 0]}
    error = 0.0
    for features, label in rows:
        p = probability(features, weights, bias)
        hit[label][0] += (p >= 0.5) == (label >= 0.5)
        hit[label][1] += 1
        error += abs(p - label)
    return (hit[1.0][0] / max(1, hit[1.0][1]), hit[0.0][0] / max(1, hit[0.0][1]), error / len(rows))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="*", help="log files (default: logs/*.csv)")
    parser.add_argument("--steps", type=int, default=20000, help="gradient steps (20000)")
    args = parser.parse_args()

    paths = args.logs or sorted(glob.glob("logs/*.csv"))
    per_log = [samples(path) for path in paths]
    if not per_log:
        print("no logs found", file=sys.stderr)
        return 1

    rows = [row for _, log_rows, _ in per_log for row in log_rows]
    total = sum(count for _, _, count in per_log)
    standing = sum(1 for _, label in rows if label >= 0.5)
    print("\n%d von %d Messwerten eindeutig: %d stehend, %d bewegt (%d dazwischen, nicht trainiert)"
          % (len(rows), total, standing, len(rows) - standing, total - len(rows)))

    weights, bias = fit(rows, args.steps)
    stand_right, move_right, error = quality(rows, weights, bias)
    print("\nauf allen Logs:  stehend erkannt %.1f %%   bewegt erkannt %.1f %%   mittlerer Abstand %.3f"
          % (stand_right * 100, move_right * 100, error))
    print("\n  Streuung über   Gewicht")
    for window, weight in zip(WINDOWS, weights):
        print("  %2d Werte        %+8.3f" % (window, weight))
    print("  Konstante       %+8.3f" % bias)

    print("\nweggelassen und darauf geprüft:")
    for index, (name, held, _) in enumerate(per_log):
        rest = [row for other, (_, log_rows, _) in enumerate(per_log) if other != index for row in log_rows]
        stand_right, move_right, error = quality(held, *fit(rest, args.steps))
        print("  ohne Shot %-4s -> auf ihm stehend %.1f %%   bewegt %.1f %%   Abstand %.3f"
              % (name, stand_right * 100, move_right * 100, error))

    print("\nals Formel:\n")
    print("  z = %+.4f" % bias)
    for window, weight in zip(WINDOWS, weights):
        print("      %+.4f * Streuung über %d Werte" % (weight, window))
    print("  flach = 1 / (1 + e^-z)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
