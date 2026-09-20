#!/usr/bin/env python3
"""Draws the end of a grind: how the dose settles after the switch-off, in two images per log.

"-verify" is what the finished screen shows: from the beginning of the verification on, the firmware
averages every raw reading that has arrived since then and shows how far that average sits from the
target (see STATUS_GRINDING_FINISHED in src/display.cpp), rounded to the two decimals of the screen.

"-delay" is what the delay is calculated from: the line through the last FLOW_WINDOW seconds before
the switch-off, read off at that moment and carried on as the grinder was expected to deliver, and the
weight the dose was verified with. Between the two the grounds that arrived after the switch-off, and
the time they stand for is the measured delay (see calibrateDelay in src/scale.cpp).

    tools/plotverify.py logs/manual-20260920-120523.csv
    tools/plotverify.py                      # all logs in logs/ that have a switch-off
    tools/plotverify.py --show               # open the windows instead of writing files
    tools/plotverify.py --format png
"""

import argparse
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from grindlog import load  # noqa: E402
from plotgrind import DARK, LIGHT, style  # noqa: E402

DECIMALS = 2 # the screen shows the deviation with two decimals

# The firmware constants this redraws, see include/config.hpp
FLOW_WINDOW = 4.0          # s of readings the line the switch-off is calculated from runs through
VERIFY_WINDOW_MS = 1000    # ms of readings the dose is verified with ...
VERIFY_WEIGHT_OLDEST = 0.33 # ... the oldest two of them counted less
VERIFY_WEIGHT_SECOND = 0.67
DELAY_LOOK_BEFORE = 2.0    # s of the plot before the window of the line ...
DELAY_LOOK_AFTER = 3.0     # ... and after the dose was confirmed
DELAY_MARGIN = 0.3         # g of air above and below the readings


def verification(marks):
    """When the verification begins: the moment the grinder was switched off, in seconds"""
    for mark in sorted(marks, key=lambda m: m["t_ms"]):
        if mark["text"].split()[0] == "grinder_off":
            return mark["t_ms"] / 1000.0
    return None


def cup_weight(meta, marks):
    """The empty cup the dose counts from, as the micro-tare set it at the start of the grind"""
    for mark in sorted(marks, key=lambda m: m["t_ms"]):
        if mark["text"].split()[0] == "microtare" and "cup" in mark:
            return mark["cup"]
    return meta.get("cup_empty", 0.0)


def target_weight(meta, marks):
    """The target including the cup: older firmware noted it at the switch-off, otherwise it is the
    target of the recording on top of the cup the micro-tare set when the grind started"""
    for mark in sorted(marks, key=lambda m: m["t_ms"]):
        if mark["text"].split()[0] == "grinder_off" and "target" in mark:
            return mark["target"]
    return meta.get("target", 0.0) + cup_weight(meta, marks)


def confirmed(marks):
    """When the dose was confirmed and the delay calculated from it, in seconds"""
    for mark in sorted(marks, key=lambda m: m["t_ms"]):
        if mark["text"].split()[0] == "grind_end":
            return mark["t_ms"] / 1000.0
    return None


def firmware_line(marks):
    """The line the firmware itself stopped the grinder on, as it noted it at the switch-off:
    (mass flow in g/s, dose the line stood at). Both are missing in older logs"""
    for mark in sorted(marks, key=lambda m: m["t_ms"]):
        if mark["text"].split()[0] == "grinder_off" and "flow" in mark and "dose" in mark:
            return mark["flow"], mark["dose"]
    return None, None


def window_line(samples, at, window):
    """The least squares line through the readings of the last `window` seconds before `at`.

    The firmware fits it to its filtered weights, which arrive twice a second; here it runs on the raw
    readings of the same stretch. Returns (slope in g/s, value of the line at `at`)
    """
    points = [(t_ms / 1000.0 - at, grams) for t_ms, _raw, grams in samples
              if at - window <= t_ms / 1000.0 <= at]
    if len(points) < 2:
        return None, None
    count = len(points)
    meanTime = sum(t for t, _ in points) / count
    meanValue = sum(v for _, v in points) / count
    spread = sum((t - meanTime) ** 2 for t, _ in points)
    if spread <= 0:
        return None, None
    slope = sum((t - meanTime) * (v - meanValue) for t, v in points) / spread
    return slope, meanValue - slope * meanTime


def verified_weight(samples, at):
    """The weight the dose was verified with: the average over the last second before `at`, with the
    oldest two readings counted less, the way the firmware weighs that window"""
    window = [grams for t_ms, _raw, grams in samples
              if at - VERIFY_WINDOW_MS / 1000.0 <= t_ms / 1000.0 <= at]
    if not window:
        return None
    weights = [1.0] * len(window)
    weights[0] = VERIFY_WEIGHT_OLDEST
    if len(weights) > 1:
        weights[1] = VERIFY_WEIGHT_SECOND
    return sum(w * v for w, v in zip(weights, window)) / sum(weights)


def running_deviation(samples, since, target):
    """The average of the readings from `since` on, minus the target, reading by reading"""
    t, deviation = [], []
    total, count = 0.0, 0
    for t_ms, _raw, grams in samples:
        if t_ms / 1000.0 < since:
            continue
        total += grams
        count += 1
        t.append(t_ms / 1000.0 - since)
        deviation.append(round(total / count - target, DECIMALS))
    return t, deviation


def plot_deviation(log, theme, path, show):
    import matplotlib.pyplot as plt

    meta, marks, _end, samples = log
    since = verification(marks)
    target = target_weight(meta, marks)
    t, deviation = running_deviation(samples, since, target)

    figure, axis = plt.subplots(figsize=(9, 4), constrained_layout=True)
    axis.axhline(0, color=theme["rule"], linewidth=0.8, zorder=1)
    axis.step(t, deviation, where="post", color=theme["series"][0], linewidth=1.4, zorder=3)
    axis.set_xlabel("Zeit seit Beginn der Verifizierung (s)")
    axis.set_ylabel("Abweichung vom Ziel (g)")
    axis.set_title("%s - gleitender Durchschnitt der Abweichung, Ziel %.2f g" % (meta.get("name", ""), target))
    axis.grid(axis="y", color=theme["grid"], linewidth=0.6)
    axis.set_axisbelow(True)

    if show:
        plt.show()
    else:
        figure.savefig(path)
        plt.close(figure)


def plot_delay(log, theme, path, show):
    """The readings around the switch-off with the line the firmware stopped the grinder on and the
    weight the dose was verified with"""
    import matplotlib.pyplot as plt

    meta, marks, _end, samples = log
    switchOff = verification(marks)
    confirmedAt = confirmed(marks)
    cup = cup_weight(meta, marks)
    slope, atSwitchOff = window_line(samples, switchOff, FLOW_WINDOW)
    verified = verified_weight(samples, confirmedAt) if confirmedAt else None

    first = switchOff - FLOW_WINDOW - DELAY_LOOK_BEFORE
    # The recording runs on long after the dose is confirmed and that stretch says nothing more here
    last = min(samples[-1][0] / 1000.0, (confirmedAt or switchOff) + DELAY_LOOK_AFTER)
    t = [s[0] / 1000.0 for s in samples if first <= s[0] / 1000.0 <= last]
    dose = [s[2] - cup for s in samples if first <= s[0] / 1000.0 <= last]

    figure, axis = plt.subplots(figsize=(9, 4.5), constrained_layout=True)
    axis.plot(t, dose, linestyle="none", marker=".", markersize=3, color=theme["series"][0],
              label="Rohmessungen", zorder=3)

    def line(atDose, rate, color, label):
        """A line over the window it was fitted to, carried on dashed from the switch-off up to the
        weight the dose was verified with - that is where the firmware reads the delay off"""
        axis.plot([switchOff - FLOW_WINDOW, switchOff], [atDose - rate * FLOW_WINDOW, atDose],
                  color=color, linewidth=1.4, zorder=4, label=label)
        carried = (verifiedDose - atDose) / rate if verifiedDose is not None and rate > 0 \
            and verifiedDose > atDose else 1.0
        until = min(switchOff + carried, last)
        axis.plot([switchOff, until], [atDose, atDose + rate * (until - switchOff)],
                  color=color, linewidth=1.0, linestyle="--", zorder=4)

    verifiedDose = None if verified is None else verified - cup
    firmwareFlow, firmwareDose = firmware_line(marks)
    if firmwareDose is not None:
        line(firmwareDose, firmwareFlow, theme["series"][1],
             "Gerade der Firmware, gefilterte Gewichte (%.2f g/s)" % firmwareFlow)
    if slope is not None:
        line(atSwitchOff - cup, slope, theme["new"],
             "dieselbe Gerade auf den Rohmessungen (%.2f g/s)" % slope)
    if verified is not None:
        axis.axhline(verifiedDose, color=theme["series"][2], linewidth=1.2, zorder=2,
                     label="verifiziertes Gewicht %.2f g" % verifiedDose)
    # The two moments, their labels on two rows so they do not sit on top of each other
    for row, (when, text) in enumerate(((switchOff, "Grinder aus"), (confirmedAt, "verifiziert"))):
        if when is None:
            continue
        axis.axvline(when, color=theme["rule"], linewidth=0.7, zorder=1)
        axis.annotate(text, (when, 0.0), xycoords=("data", "axes fraction"), xytext=(3, 4 + row * 10),
                      textcoords="offset points", color=theme["muted"], fontsize=8)

    axis.set_xlim(first, last)
    axis.set_ylim(min(dose) - DELAY_MARGIN, max(dose) + DELAY_MARGIN)
    axis.set_xlabel("Zeit der Aufnahme (s)")
    axis.set_ylabel("Dose (g)")
    axis.set_title("%s - Abschaltrechnung und verifiziertes Gewicht" % meta.get("name", ""))
    axis.grid(axis="y", color=theme["grid"], linewidth=0.6)
    axis.set_axisbelow(True)
    axis.legend(loc="upper left", fontsize=8)

    if show:
        plt.show()
    else:
        figure.savefig(path)
        plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="*", help="log files (default: logs/*.csv)")
    parser.add_argument("--out", default="logs/plots", help="folder for the images (logs/plots)")
    parser.add_argument("--show", action="store_true", help="open the window instead of writing a file")
    parser.add_argument("--dark", action="store_true", help="draw on a dark surface")
    parser.add_argument("--format", default="svg", choices=("svg", "pdf", "png"),
                        help="file format, vector by default (svg)")
    args = parser.parse_args()

    paths = args.logs or sorted(glob.glob("logs/*.csv"))
    if not paths:
        print("no logs found, record some with tools/grindlog.py", file=sys.stderr)
        return 1

    theme = DARK if args.dark else LIGHT
    style(theme)
    if not args.show:
        os.makedirs(args.out, exist_ok=True)

    for path in paths:
        log = load(path)
        name = os.path.splitext(os.path.basename(path))[0]
        log[0].setdefault("name", name)
        if verification(log[1]) is None:
            print("%s has no switch-off, skipped" % path, file=sys.stderr)
            continue
        for suffix, draw in (("-verify", plot_deviation), ("-delay", plot_delay)):
            target = os.path.join(args.out, name + suffix + "." + args.format)
            draw(log, theme, target, args.show)
            print(path if args.show else target)
    return 0


if __name__ == "__main__":
    sys.exit(main())
