#!/usr/bin/env python3
"""Draws several grinds under each other, each with the same three fitted lines.

One panel per log, from BEFORE_ON seconds before the grinder starts to the end of the recording, the
time counted from the start of the grinder. Into every panel go three straight lines, all of them
drawn across the whole width and without ends:

    - a horizontal through the last LEVEL_BEFORE readings before the grinder started
    - a horizontal through the last LEVEL_AFTER readings before the dose was verified
    - the least squares line through the readings from FIT_AFTER_ON after the start to
      FIT_BEFORE_OFF before the grinder stopped

The maths comes from plotgrind.py, so both tools fit their lines the same way.

    tools/plotfits.py --show                 # the three newest logs with a whole grind
    tools/plotfits.py logs/manual-2026*.csv  # these logs instead
    tools/plotfits.py --out /tmp/fits.svg    # write a file instead of opening a window
"""

import argparse
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from grindlog import load  # noqa: E402
import plotgrind as pg  # noqa: E402

BEFORE_ON = 2.0      # s of the panel before the grinder starts
LEVEL_BEFORE = 5     # readings before the start for the first horizontal ...
LEVEL_AFTER = 10     # ... and readings before the verified dose for the second one
FIT_AFTER_ON = 2.0   # the sloped line runs from this many s after the grinder started ...
FIT_BEFORE_OFF = 1.0 # ... to this many s before it stopped

# The marks the panels are built from; a recording by hand ends a grind with grind_end, a recording
# that ran on its own with stop
STARTED = "grinder_on"
STOPPED = "grinder_off"
VERIFIED = ("grind_end", "stop")


def marks_of(log):
    """The time of every mark in seconds, the first one of a name counts"""
    out = {}
    for mark in log[1]:
        out.setdefault(mark["text"].split()[0], mark["t_ms"] / 1000.0)
    return out


def verified_at(marks):
    for name in VERIFIED:
        if name in marks:
            return marks[name]
    return None


def level_of_last(t, values, before, count):
    """The average of the last `count` readings taken before `before` seconds, None if there are fewer"""
    taken = [y for x, y in zip(t, values) if x < before][-count:]
    if len(taken) < count:
        return None
    return sum(taken) / len(taken)


def draw(axis, theme, log, step):
    """One panel: the readings around the grind and the three lines through them"""
    marks = marks_of(log)
    started, stopped, verified = marks.get(STARTED), marks.get(STOPPED), verified_at(marks)
    if started is None or stopped is None or verified is None:
        return "%s: no whole grind in it" % log[0].get("shot", "?")

    t, values = pg.series(log, net=False, raw=False)
    before = level_of_last(t, values, started, LEVEL_BEFORE)
    after = level_of_last(t, values, verified, LEVEL_AFTER)
    fitted = pg.fit_line(t, values, started + FIT_AFTER_ON, stopped - FIT_BEFORE_OFF)
    if before is None or after is None or fitted is None:
        return "%s: not enough readings for the lines" % log[0].get("shot", "?")
    middle, average, slope = fitted

    # Everything against the start of the grinder, so the panels can be compared with each other
    shown = [(x - started, y) for x, y in zip(t, values) if x >= started - BEFORE_ON]
    axis.plot([x for x, _ in shown], [y for _, y in shown], linestyle="none", marker="o",
              markersize=2.8, color=theme["series"][0], markeredgewidth=0, label="Messwerte", zorder=3)

    # Two heights for the labels, otherwise "Grinder aus" and "verifiziert" sit on top of each other
    # when the dose is confirmed a second after the grinder stopped
    for index, (when, name) in enumerate(((0.0, "Grinder an"), (stopped - started, "Grinder aus"),
                                          (verified - started, "verifiziert"))):
        axis.axvline(when, color=theme["rule"], linewidth=0.8, zorder=2)
        axis.annotate(name, (when, 0.02 + 0.06 * (index % 2)), xycoords=("data", "axes fraction"),
                      color=theme["muted"], fontsize=8, ha="left", va="bottom", xytext=(3, 0),
                      textcoords="offset points")

    for point, rise, color, label in (
            ((0.0, before), 0.0, theme["new"],
             "letzte %d vor Grinder an: %.2f g" % (LEVEL_BEFORE, before)),
            ((0.0, after), 0.0, theme["sigma"],
             "letzte %d vor verifiziert: %.2f g" % (LEVEL_AFTER, after)),
            ((middle - started, average), slope, theme["grind"],
             "%.0f s nach an bis %.0f s vor aus: %+.3f g/s" % (FIT_AFTER_ON, FIT_BEFORE_OFF, slope))):
        axis.axline(point, slope=rise, color=color, linewidth=1.0, linestyle=(0, (6, 3)), zorder=5,
                    label=label)

    axis.set_xlim(-BEFORE_ON, max(x for x, _ in shown))
    # The height is taken from the grind itself and from the level before it. A cup put down within the
    # two seconds before the start would otherwise stretch the panel over its whole placing swing
    grinding = [y for x, y in zip(t, values) if x >= started] + [before]
    room = 0.08 * (max(grinding) - min(grinding))
    axis.set_ylim(min(grinding) - room, max(grinding) + room)
    axis.set_ylabel("Gewicht (g)")
    axis.legend(loc="upper left", fontsize=8)
    pg.grid(axis, theme, step, 0)
    meta = log[0]
    axis.set_title("Schuss %s  -  %s, Dosis %.2f g in %.1f s" %
                   (meta.get("shot", "?"), meta.get("kind", "?"), after - before, stopped - started),
                   loc="left", fontsize=9)
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="*", help="log files (default: the three newest in logs/)")
    parser.add_argument("--show", action="store_true", help="open a window instead of writing a file")
    parser.add_argument("--out", default="logs/plots/fits.svg", help="file to write (logs/plots/fits.svg)")
    parser.add_argument("--dark", action="store_true", help="draw on a dark surface")
    parser.add_argument("--grid", type=float, default=1.0, metavar="G",
                        help="grams between two lines of the grid (1.0)")
    args = parser.parse_args()

    paths = args.logs or sorted(glob.glob("logs/*.csv"), key=os.path.getmtime, reverse=True)[:3]
    if not paths:
        print("no logs found, record some with tools/grindlog.py", file=sys.stderr)
        return 1

    theme = pg.DARK if args.dark else pg.LIGHT
    pg.style(theme)
    import matplotlib.pyplot as plt

    logs = [load(path) for path in paths]
    figure, axes = plt.subplots(len(logs), 1, figsize=(11, 3.6 * len(logs)), constrained_layout=True)
    for axis, log, path in zip(axes if len(logs) > 1 else [axes], logs, paths):
        missing = draw(axis, theme, log, args.grid)
        if missing:
            print("%s %s" % (path, missing), file=sys.stderr)
    axes[-1].set_xlabel("Zeit seit Grinder an (s)") if len(logs) > 1 else axes.set_xlabel("Zeit seit Grinder an (s)")

    if args.show:
        plt.show()
    else:
        os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
        figure.savefig(args.out)
        print(args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
