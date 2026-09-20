#!/usr/bin/env python3
"""Draws what the finished screen shows: the deviation of the dose from the target while it settles.

From the beginning of the verification on, the firmware averages every raw reading that has arrived
since then and shows how far that average sits from the target (see STATUS_GRINDING_FINISHED in
src/display.cpp). This draws that value over the recording, rounded to the two decimals of the screen.

    tools/plotverify.py logs/manual-20260919-210554.csv
    tools/plotverify.py                      # all logs in logs/ that have a switch-off
    tools/plotverify.py --show               # open the window instead of writing a file
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


def verification(marks):
    """When the verification begins: the moment the grinder was switched off, in seconds"""
    for mark in sorted(marks, key=lambda m: m["t_ms"]):
        if mark["text"].split()[0] == "grinder_off":
            return mark["t_ms"] / 1000.0
    return None


def target_weight(meta, marks):
    """The target including the cup, as the firmware itself noted it at the switch-off"""
    for mark in sorted(marks, key=lambda m: m["t_ms"]):
        if mark["text"].split()[0] == "grinder_off" and "target" in mark:
            return mark["target"]
    return meta.get("target", 0.0) + meta.get("cup_empty", 0.0)


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


def plot(log, theme, path, show):
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
        target = os.path.join(args.out, name + "-verify." + args.format)
        plot(log, theme, target, args.show)
        print(path if args.show else target)
    return 0


if __name__ == "__main__":
    sys.exit(main())
