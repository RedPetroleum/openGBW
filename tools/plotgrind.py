#!/usr/bin/env python3
"""Draws the grind logs recorded by tools/grindlog.py.

Every reading is one point - the readings are not connected, so what is on screen is what the load cell
delivered and nothing in between. Over the points run the filters, as thin lines. Per grind one image,
plus an overview with all grinds on top of each other when there is more than one (points only, filters
of three grinds at once cannot be told apart).

    tools/plotgrind.py                       # all logs in logs/, images into logs/plots/
    tools/plotgrind.py logs/grind-*.csv --out /tmp/plots
    tools/plotgrind.py --show                # open the windows instead of writing files
    tools/plotgrind.py --net                 # weight without the cup, so the axis starts at zero
    tools/plotgrind.py --raw                 # the counts of the HX711 instead of grams
    tools/plotgrind.py --grid 0.5            # a line every 0.5 g instead of every 0.1 g
    tools/plotgrind.py --format pdf          # vector as well, SVG is the default
"""

import argparse
import glob
import math
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from grindlog import load  # noqa: E402

# Colors of the data visualisation reference palette, slots 1 to 3, unchanged: they are validated for
# colour vision deficiency over all pairs. Light and dark are the same hues stepped for their surface
LIGHT = dict(surface="#fcfcfb", text="#0b0b0b", muted="#52514e", grid="#e8e7e3", rule="#b5b3ad",
             series=("#2a78d6", "#eb6834", "#1baf7a"), new="#4a3aa7", sigma="#e34948", grind="#008300",
             trained="#0b0b0b")
DARK = dict(surface="#1a1a19", text="#ffffff", muted="#c3c2b7", grid="#302f2d", rule="#6b6a65",
            series=("#3987e5", "#d95926", "#199e70"), new="#9085e9", sigma="#e66767", grind="#008300",
            trained="#ffffff")

REFERENCE_WINDOW = 9 # readings of the centred average that serves as the true course of the weight
STANDING_BELOW = 0.05 # g/s, a true change this small means the scale was standing
MOVING_ABOVE = 0.5    # g/s, this large means it was moving; in between is neither
STANDING_LOOK = 5     # readings to each side the true change is taken over

MOST_GRID_LINES = 400 # more lines than this in the grid are a grey area, not a grid

MARK_LABELS = {"grinder_on": "Grinder an", "grinder_off": "Grinder aus", "stop": "verifiziert"}

# The filter the firmware runs today: SCALE_READINGS_PER_UPDATE readings averaged into one weight,
# that one through the Kalman filter of main.cpp
BUNDLE = 5
KALMAN = (0.02, 0.02, 0.01) # measurement error, estimate error, process noise
AVERAGE_WINDOW = 5          # readings of the moving average

# The new filter: a step of at least JUMP_GRAMS is taken over at once, everything smaller is followed
# through the slope of the last TREND_WINDOW readings
JUMP_GRAMS = 1.0
TREND_WINDOW = 5

# The flatter the trend, and the longer it has been flat, the harder the result is smoothed on top.
# A single window cannot tell a resting scale from a running grinder: while grinding the slope is around
# 0.75 g/s, and at rest the noise alone produces up to 1.6 g/s. That is why the time carries as much
# weight as the slope - only a trend that stays flat earns the full smoothing.
# FLAT_SLOPE was picked on the recordings: below 2.5 g/s the noise at rest is barely smoothed, above it
# the filter starts to trail at the moment the grinder is switched off, which is the one that matters
FLAT_SLOPE = 2.5     # g/s, from here on the trend does not count as horizontal at all any more
FLAT_READINGS = 15   # readings of horizontal trend until the smoothing is at full strength
FLAT_ALPHA = 0.1     # weight of a new reading at full strength (small = strongly smoothed)

# The second way of recognising a flat stretch, the only thing the second new filter does differently:
# not the slope of the readings but how far they scatter. A weight that rests only carries the noise of
# the load cell, and that noise is normally distributed around the true weight - so its sigma is small.
# A weight that moves pulls the readings apart and sigma grows, whatever the shape of the movement
SIGMA_WINDOW = 8     # readings sigma is taken over, and with that the shortest stretch that can be flat
SIGMA_FLAT = 0.1     # g, at this sigma and below the flatness is 1
SIGMA_STEEP = 1.0    # g, at this sigma and above it is 0

# The trained filter: for every reading it takes the longest window over which a straight line still
# fits the readings within ADAPTIVE_TOLERANCE, and reads that line off at the newest reading. Where the
# weight rests a long window fits and the result is quiet, where it moves in clumps only a short one
# does and the result is quick - and a straight line has no lag on a rising weight however long it is.
# The values come from tools/trainfilter.py, searched on the recorded grinds
ADAPTIVE_LONGEST = 40    # readings the window may grow to
ADAPTIVE_SHORTEST = 2    # ... and never falls below
ADAPTIVE_TOLERANCE = 0.15 # g, how far the readings may sit off the line (RMS)
ADAPTIVE_JUMP = 1.0      # g, a difference this large is a step and empties the window
# ... and the Kalman filter of the firmware behind it, set soft. Only the ratio of the process noise to
# the measurement error does anything here, the two errors on their own cancel out. At a ratio of one the
# reading comes to rest most quietly; below it the filter starts to lag into the settling after the
# grinder stops and the end gets worse again, above it the middle is followed more closely
ADAPTIVE_KALMAN = (0.02, 0.02, 0.02) # measurement error, estimate error, process noise

# What the new filter finally shows: steps of DISPLAY_STEP. A step of one is only taken when the value is
# HYSTERESIS_GRAMS beyond where it would normally round, or when HYSTERESIS_READINGS readings in a row
# all want the same step
DISPLAY_STEP = 0.1
HYSTERESIS_GRAMS = 0.03
HYSTERESIS_READINGS = 3
# Where the filter has already recognised the trend as flat, both conditions are harder: there the
# weight is not going anywhere, so a step of the last digit is almost always the reading rustling.
# The gram value is added to the middle between two steps, so 0.05 puts the threshold at a full step
# above the shown value and still leaves 0.05 in which a single reading can carry the step on its own -
# from 0.10 on the difference would be two steps and this rule would not apply any more. Everything
# between 0.04 and 0.08 measured the same on the recordings, the work is done by the reading count
# The threshold is high on purpose: at 0.5 the harder conditions were also in force through two thirds
# of the grinding, where the display has to follow. At 0.9 they cover almost all of the resting and
# about a tenth of the grinding
HYSTERESIS_FLAT_FROM = 0.9 # flatness from which the harder conditions are used
HYSTERESIS_GRAMS_FLAT = 0.05
HYSTERESIS_READINGS_FLAT = 6


def kalman(values, err_measure, err_estimate, q):
    """SimpleKalmanFilter of the firmware, line by line as in the library (denyssene/SimpleKalmanFilter).

    On the scale the filter never restarts, so it does not start from zero here either: the first value
    becomes the first estimate.
    """
    out, last = [], values[0] if values else 0.0
    for measurement in values:
        gain = err_estimate / (err_estimate + err_measure)
        current = last + gain * (measurement - last)
        err_estimate = (1.0 - gain) * err_estimate + abs(last - current) * q
        last = current
        out.append(current)
    return out


def old_filter(t, values):
    """What the firmware sees today: every BUNDLE readings one weight, then the Kalman filter.

    The weight only exists once the last reading of a bundle is in, so that is where its point sits.
    """
    count = len(values) // BUNDLE * BUNDLE
    bundled = [sum(values[i:i + BUNDLE]) / BUNDLE for i in range(0, count, BUNDLE)]
    when = [t[i + BUNDLE - 1] for i in range(0, count, BUNDLE)]
    return when, kalman(bundled, *KALMAN)


def centred_average(values, window=REFERENCE_WINDOW):
    """Average around each reading, not behind it - it has no lag, so it can serve as the true weight"""
    half = window // 2
    return [statistics.fmean(values[max(0, i - half):i + half + 1]) for i in range(len(values))]


def standing_truth(t, values):
    """Whether the scale really was standing, per reading: 1.0, 0.0, or None where it is neither.

    No filter can know this, and it is not meant to: it is read off a centred average, which looks as
    far forward as backward. That makes it the yardstick the causal formulas are fitted against and
    drawn over - it is the answer, not a filter.
    """
    reference = centred_average(values)
    out = []
    for index in range(len(values)):
        low, high = index - STANDING_LOOK, index + STANDING_LOOK
        if low < 0 or high >= len(values):
            out.append(None)
            continue
        rate = abs(reference[high] - reference[low]) / (t[high] - t[low])
        out.append(1.0 if rate <= STANDING_BELOW else 0.0 if rate >= MOVING_ABOVE else None)
    return out


# The fitted flatness: how sure one can be that the weight is standing, from the spread of the readings
# over four windows at once. Short windows react at once, long ones only become small once the weight
# has been standing for a while - that is where "the longer it is straight, the surer" comes from, with
# no counter and no threshold. Fitted by tools/trainflat.py against the centred average, which knows
# the real course of the weight; a window that does not have its readings yet counts as LEARNED_UNKNOWN
LEARNED_WINDOWS = (5, 10, 20, 40)
LEARNED_WEIGHTS = (-7.6657, 1.0182, -2.9183, 1.5752)
LEARNED_BIAS = 0.4741
LEARNED_UNKNOWN = 1.0 # g, stands for "this window cannot say anything yet"


def learned_flatness(recent, slope=0.0):
    """1 means the readings are what a resting scale looks like, 0 means the weight is moving"""
    z = LEARNED_BIAS
    for window, weight in zip(LEARNED_WINDOWS, LEARNED_WEIGHTS):
        part = recent[-window:]
        z += weight * (statistics.stdev(part) if len(part) >= max(2, window) else LEARNED_UNKNOWN)
    return 1.0 / (1.0 + math.exp(-max(-30.0, min(30.0, z))))


# The three detectors of v01, all of them on the raw readings and all of them between 0 and 1
FLAT_V01_LONG = 20   # readings that have to lie within FLAT_V01_TIGHT for the full 1
FLAT_V01_SHORT = 5   # ... the fewest that are looked at at all, they give FLAT_V01_FEW
FLAT_V01_FEW = 0.8
FLAT_V01_TIGHT = 0.7 # g
FLAT_V01_WIDE = 1.2  # g, at this spread over the short window it is down to FLAT_V01_LOOSE
FLAT_V01_LOOSE = 0.5
FLAT_V01_QUIET = 0.5  # g/s, up to this rate the value is left alone
FLAT_V01_MOVING = 1.0 # g/s, from here the weight is moving and the value is 0

PLACED_V01_GRAMS = 1.5 # g, a step of more than this between two readings

GRIND_V01_CORE = (0.5, 5.5) # g/s, in here the full value is possible
GRIND_V01_WIDE = (0.4, 6.0) # g/s, outside of this it is nothing, in between it fades
GRIND_V01_LONG = 20  # readings over which the rate has to hold for the full 1
GRIND_V01_SHORT = 5  # ... the fewest, they give GRIND_V01_FEW
GRIND_V01_FEW = 0.5
GRIND_V01_QUIET = 1.0 # g/s, below this rate a value under GRIND_V01_SURE is not grinding at all
GRIND_V01_SURE = 0.6

AVERAGE_PANEL = 15  # readings the third panel averages the weight over
CHANGE_MOST = 10.0  # g/s, the third panel shows no more than this - a cup being put on is hundreds


def flat_v01(recent, change=0.0):
    """How flat it lies, from the spread of the readings.

    Twenty readings within 0.7 g are the full 1, five within 0.7 g are 0.8, and between those two it is
    interpolated over how many readings still hold. Five readings within 1.2 g are 0.5, and between
    0.7 and 1.2 g it is interpolated over the spread. Wider than that, or fewer than five readings, is 0.

    On top of that the rate has a veto: `change`, how fast the weight really moves, is taken from the
    derivative of the moving average. From FLAT_V01_MOVING on nothing is flat whatever the readings look
    like, and between FLAT_V01_QUIET and there the value is faded out towards it.
    """
    speed = abs(change)
    if speed >= FLAT_V01_MOVING:
        return 0.0
    damped = 1.0 if speed < FLAT_V01_QUIET else \
        1.0 - (speed - FLAT_V01_QUIET) / (FLAT_V01_MOVING - FLAT_V01_QUIET)
    if len(recent) < FLAT_V01_SHORT:
        return 0.0
    span = lambda count: max(recent[-count:]) - min(recent[-count:])

    short = span(FLAT_V01_SHORT)
    if short > FLAT_V01_WIDE:
        return 0.0
    if short > FLAT_V01_TIGHT: # between the two spreads, interpolated over the grams
        share = (short - FLAT_V01_TIGHT) / (FLAT_V01_WIDE - FLAT_V01_TIGHT)
        return damped * (FLAT_V01_FEW - share * (FLAT_V01_FEW - FLAT_V01_LOOSE))

    count = FLAT_V01_SHORT # how far back the readings still lie within the tight spread
    while count < min(FLAT_V01_LONG, len(recent)) and span(count + 1) <= FLAT_V01_TIGHT:
        count += 1
    share = (count - FLAT_V01_SHORT) / (FLAT_V01_LONG - FLAT_V01_SHORT)
    return damped * (FLAT_V01_FEW + share * (1.0 - FLAT_V01_FEW))


def placed_v01(previous, value):
    """Something was put on or taken off: a step of more than PLACED_V01_GRAMS from one reading to the next"""
    return 1.0 if abs(value - previous) > PLACED_V01_GRAMS else 0.0


def grinding_rate_share(rate):
    """1 inside the core range, fading to 0 at the edges of the wide one, 0 outside it"""
    low, high = GRIND_V01_WIDE
    core_low, core_high = GRIND_V01_CORE
    if rate <= low or rate >= high:
        return 0.0
    if rate < core_low:
        return (rate - low) / (core_low - low)
    if rate > core_high:
        return (high - rate) / (high - core_high)
    return 1.0


def grinding_v01(times, recent, change):
    """Coffee is falling: the weight is rising at a rate a grinder produces, and has been for a while.

    Built like flat_v01, only over the rate instead of the spread: the rate of the last `count` readings
    is taken from a straight line through them, and the longer a window still shows a rate in the range,
    the higher the value - 20 readings give 1, five give 0.5. Between the core range and the wide one the
    value fades, but it never drops below 0.5: either it is at least that, or it is nothing at all.

    `change` is how fast the weight is really moving, from the derivative of the moving average. Where
    that is below GRIND_V01_QUIET and the value has not made it past GRIND_V01_SURE, nothing is grinding
    and the answer is 0 - a rate that a few readings happen to show is not a grinder running.
    """
    best = 0.0
    for count in range(min(GRIND_V01_LONG, len(recent)), FLAT_V01_SHORT - 1, -1):
        if count < GRIND_V01_SHORT:
            break
        share = grinding_rate_share(line_at_end(times[-count:], recent[-count:])[2])
        if share > 0:
            reach = (count - GRIND_V01_SHORT) / (GRIND_V01_LONG - GRIND_V01_SHORT)
            best = max(best, GRIND_V01_FEW + reach * (1.0 - GRIND_V01_FEW) * share)
    return 0.0 if change < GRIND_V01_QUIET and best < GRIND_V01_SURE else best


def slope_flatness(recent, slope):
    """How horizontal the trend is, from the slope of the line through the window"""
    return max(0.0, 1.0 - abs(slope) / FLAT_SLOPE)


def sigma_flatness(recent, slope):
    """How horizontal the trend is, from how far the readings scatter.

    Asks what a resting scale looks like: its readings are the true weight plus the noise of the load
    cell, normally distributed around it, so they stay within a narrow sigma. As soon as the weight
    moves, the readings are no longer one normal distribution but a distribution that is being dragged
    along, and sigma grows with the distance it has travelled inside the window.

    Below SIGMA_WINDOW readings nothing is judged at all - a flat stretch has to be that long before it
    counts as one.
    """
    if len(recent) < SIGMA_WINDOW or len(recent) < 2:
        return 0.0
    sigma = statistics.stdev(recent[-SIGMA_WINDOW:])
    return min(1.0, max(0.0, (SIGMA_STEEP - sigma) / (SIGMA_STEEP - SIGMA_FLAT)))


def trend_filter(t, values, flatness=slope_flatness, jump=JUMP_GRAMS, window=TREND_WINDOW):
    """Jumps on a step, follows the slope in between.

    The flatter the trend is, and the longer it has already been flat, the harder the result is smoothed
    on top: while the weight moves the filter follows it without delay, and once it comes to rest it
    slowly turns into an average that stops the reading from rustling.

    A moving average always trails a rising weight: its value is the middle of its window, so at 0.5 g/s
    and five readings it is about a quarter of a second behind. A straight line laid through the same five
    readings and read off at the newest one has no such lag, because the slope is a part of it. What it
    cannot do is cross a step - so a difference of `jump` or more is not filtered at all but taken over,
    and the window starts again, since the readings before the step describe a different weight.

    Returns the times, the filtered values, and how strongly each reading was smoothed, from 0 (not at
    all) to 1 (fully) - that strength is what the shading in the drawing shows.
    """
    out, strengths, estimate = [], [], values[0] if values else 0.0
    keep = max(window, SIGMA_WINDOW) # one buffer, the trend and sigma each take what they need from it
    window_t, window_v = [], []
    flat = 0 # readings in a row whose trend was horizontal
    for time, value in zip(t, values):
        jumped = abs(value - estimate) >= jump
        if jumped:
            window_t, window_v = [], []
        window_t.append(time)
        window_v.append(value)
        if len(window_t) > keep:
            window_t.pop(0)
            window_v.pop(0)
        fit_t, fit_v = window_t[-window:], window_v[-window:]

        slope = 0.0
        if len(fit_t) < 3:
            fitted = sum(fit_v) / len(fit_v) # too few readings for a slope
        else:
            count = len(fit_t)
            mean_t = sum(fit_t) / count
            mean_v = sum(fit_v) / count
            spread = sum((x - mean_t) ** 2 for x in fit_t)
            if spread:
                slope = sum((x - mean_t) * (y - mean_v) for x, y in zip(fit_t, fit_v)) / spread
            fitted = mean_v + slope * (fit_t[-1] - mean_t) # the line read off at the newest reading

        # How flat this window is, from 1 to 0, times how long it has been flat. Both have to hold: a
        # steep window switches the smoothing off at once, and after a jump or a steep stretch the
        # strength has to be earned again over FLAT_READINGS readings
        level = flatness(window_v, slope)
        flat = flat + 1 if level > 0 and not jumped else 0
        strength = level * min(flat / FLAT_READINGS, 1.0)
        alpha = 1.0 - strength * (1.0 - FLAT_ALPHA)
        estimate = fitted if jumped else alpha * fitted + (1.0 - alpha) * estimate
        out.append(estimate)
        strengths.append(strength)
    return t, out, strengths


def hysteresis(values, flat=None, step=DISPLAY_STEP):
    """Rounds to `step`, but makes a single step of it harder than plain rounding would.

    Plain rounding flips the last digit as soon as the value passes the middle between two steps, so a
    weight sitting right on that middle flickers between the two. Here a step of one is only taken when
    the value is `extra` past that middle - or when `confirm` readings in a row all want the same step,
    which is the weight really moving rather than the reading rustling around the boundary. A difference
    of two steps or more is not the business of this rule and is taken over as it is.

    Where `flat` says the filter has recognised the trend as flat, both conditions are harder: a resting
    weight does not step, so a step there is the reading rustling and not the dose growing. While the
    weight moves the conditions stay soft, otherwise the display would fall behind it.

    The shown value is kept as a whole number of steps, otherwise adding 0.1 over and over drifts off.
    """
    if not values:
        return []
    out, unit = [], int(round(values[0] / step))
    pending, direction = 0, 0
    for index, value in enumerate(values):
        strict = flat is not None and flat[index] >= HYSTERESIS_FLAT_FROM
        extra = HYSTERESIS_GRAMS_FLAT if strict else HYSTERESIS_GRAMS
        confirm = HYSTERESIS_READINGS_FLAT if strict else HYSTERESIS_READINGS
        delta = int(round(value / step)) - unit
        if abs(delta) >= 2:
            unit += delta
            pending, direction = 0, 0
        elif delta == 0:
            pending, direction = 0, 0
        else:
            if delta != direction:
                direction, pending = delta, 0
            pending += 1
            # The middle between the two steps, moved by `extra` into the direction the value wants to go
            boundary = (unit + delta * 0.5) * step + delta * extra
            if pending >= confirm or (value - boundary) * delta >= 0:
                unit += delta
                pending, direction = 0, 0
        out.append(unit * step)
    return out


def line_at_end(window_t, window_v):
    """Straight line through the window: its value at the newest reading and the RMS of the residuals"""
    count = len(window_t)
    if count == 1:
        return window_v[0], 0.0, 0.0
    mean_t = sum(window_t) / count
    mean_v = sum(window_v) / count
    spread = sum((x - mean_t) ** 2 for x in window_t)
    slope = sum((x - mean_t) * (y - mean_v) for x, y in zip(window_t, window_v)) / spread if spread else 0.0
    residuals = [y - (mean_v + slope * (x - mean_t)) for x, y in zip(window_t, window_v)]
    rms = (sum(r * r for r in residuals) / count) ** 0.5
    return mean_v + slope * (window_t[-1] - mean_t), rms, slope


def adaptive_filter(t, values, longest, shortest, tolerance, jump):
    """The filter itself. `tolerance` is how far the readings may sit off the line, in grams RMS.

    A first search had the tolerance as a noise level times a factor; only their product ever mattered,
    so the two are one parameter here.
    """
    out, flat, estimate = [], [], values[0] if values else 0.0
    window_t, window_v = [], []
    for time, value in zip(t, values):
        if abs(value - estimate) >= jump:
            window_t, window_v = [], [] # a step: everything before it describes a different weight
        window_t.append(time)
        window_v.append(value)
        if len(window_t) > longest:
            window_t.pop(0)
            window_v.pop(0)
        for count in range(len(window_t), 0, -1):
            estimate, rms, slope = line_at_end(window_t[-count:], window_v[-count:])
            if count <= shortest or rms <= tolerance:
                break # the longest window a straight line still fits
        out.append(estimate)
        flat.append(slope_flatness(window_v, slope)) # how flat the line it settled on is
    return out, flat


def average_kalman(t, values):
    """The filter of the firmware, but on a moving average instead of on bundles.

    The firmware averages five readings into one weight and hands that one to the Kalman filter, which
    therefore only sees a new value twice a second. Here the same Kalman filter runs on a moving average
    of the last five readings, so it is fed ten times a second - same amount of averaging, five times
    the rate. The hysteresis behind it is the same one v01 uses, only without its harder level: this
    filter has no flatness of its own to switch it on, so the soft conditions hold everywhere.
    """
    return hysteresis(kalman(moving_average(t, values)[1], *KALMAN))


def v01(t, values):
    """The whole trained chain: the adaptive straight line, and the Kalman filter of the firmware on it.

    Returns the values and how flat the filter found the trend, which the hysteresis needs.
    """
    fitted, flat = adaptive_filter(t, values, ADAPTIVE_LONGEST, ADAPTIVE_SHORTEST,
                                   ADAPTIVE_TOLERANCE, ADAPTIVE_JUMP)
    return kalman(fitted, *ADAPTIVE_KALMAN), flat


def derivative(times, values):
    """Change per second of a curve, from the reading before to the one after each point"""
    out = []
    for index in range(len(values)):
        low = max(0, index - 1)
        high = min(len(values) - 1, index + 1)
        span = times[high] - times[low]
        out.append((values[high] - values[low]) / span if span > 0 else 0.0)
    return out


def rate(t):
    """Readings per second, for the labels"""
    return (len(t) - 1) / (t[-1] - t[0]) if len(t) > 1 and t[-1] > t[0] else 0.0


def moving_average(t, values, window=AVERAGE_WINDOW):
    """Average of the last `window` readings, one value per reading"""
    out = [sum(values[max(0, i - window + 1):i + 1]) / len(values[max(0, i - window + 1):i + 1])
           for i in range(len(values))]
    return t, out


def series(log, net, raw):
    """Time in seconds and the value of every single reading"""
    meta, marks, end, samples = log
    t = [s[0] / 1000.0 for s in samples]
    if raw:
        return t, [s[1] for s in samples]
    cup = meta.get("cup_empty", 0.0) if net else 0.0
    return t, [s[2] - cup for s in samples]


def style(theme):
    import matplotlib
    matplotlib.rcParams.update({
        "figure.facecolor": theme["surface"], "axes.facecolor": theme["surface"],
        "savefig.facecolor": theme["surface"],
        "text.color": theme["text"], "axes.labelcolor": theme["muted"],
        "xtick.color": theme["muted"], "ytick.color": theme["muted"],
        "axes.edgecolor": theme["grid"], "axes.linewidth": 0.8,
        "grid.color": theme["grid"], "grid.linewidth": 0.6, "grid.linestyle": "-",
        "axes.spines.top": False, "axes.spines.right": False,
        "font.size": 9, "axes.titlesize": 10, "legend.frameon": False,
        "figure.dpi": 130,
    })


def event_lines(axis, marks, theme, label=True):
    """The events as vertical lines. A recording by hand can carry a whole grind with its markers, so
    labels that would sit on top of each other are put on two rows"""
    previous = None
    row = 0
    for mark in sorted(marks, key=lambda m: m["t_ms"]):
        when = mark["t_ms"] / 1000.0
        axis.axvline(when, color=theme["rule"], linewidth=0.7, zorder=1)
        if not label:
            continue
        span = axis.get_xlim()[1] - axis.get_xlim()[0]
        row = 0 if previous is None or when - previous > span / 8 else 1 - row
        previous = when
        axis.annotate(MARK_LABELS.get(mark["text"].split()[0], mark["text"].split()[0]), (when, 0.0),
                      xycoords=("data", "axes fraction"), xytext=(3, 4 + row * 10),
                      textcoords="offset points", color=theme["muted"], fontsize=8)


def unit(net, raw):
    return "HX711 (counts)" if raw else ("Dose (g)" if net else "Gewicht (g)")


def grid(axis, theme, step, sf):
    """A line every `step` grams, labelled every whole gram.

    Over the whole range of a grind those lines stand close together, which is the point: the distance
    between them is the scale of the readings themselves.
    """
    from matplotlib.ticker import MultipleLocator

    fine = step * sf if sf else step # in counts when the raw values are drawn
    # A recording of a whole cup being put on spans hundreds of grams, and a line every 0.1 g would be
    # thousands of them - matplotlib refuses that, and it would be a grey area anyway. Ten times coarser
    # each time until they are far enough apart to be lines
    low, high = axis.get_ylim()
    while fine > 0 and (high - low) / fine > MOST_GRID_LINES:
        fine *= 10
    axis.yaxis.set_minor_locator(MultipleLocator(fine))
    axis.yaxis.set_major_locator(MultipleLocator(fine * 10))
    axis.grid(axis="y", which="major", color=theme["grid"], linewidth=0.8)
    axis.grid(axis="y", which="minor", color=theme["grid"], linewidth=0.4)
    axis.set_axisbelow(True)


def flat_shading(axis, t, strength, color, strongest=0.3, levels=8):
    """Marks where the filter found a flat trend; the stronger it smoothed, the deeper the colour.

    The strength is put into `levels` steps and readings of the same step become one rectangle. Drawing
    one rectangle per reading instead would leave an anti-aliased seam at every edge and turn a soft
    shading into a grid of bars - and an image would be a raster in an otherwise vector drawing.
    """
    last = t[-1] + (t[-1] - t[-2] if len(t) > 1 else 0.1)
    edge = lambda index: t[index] if index < len(t) else last
    steps = [round(value * levels) for value in strength]

    start = 0
    for index in range(1, len(steps) + 1):
        if index < len(steps) and steps[index] == steps[start]:
            continue
        if steps[start]:
            axis.axvspan(edge(start), edge(index), color=color, linewidth=0, zorder=0,
                         alpha=strongest * steps[start] / levels)
        start = index


def plot_grind(log, theme, path, show, net, raw, step):
    import matplotlib.pyplot as plt

    meta, marks, end, samples = log
    t, values = series(log, net, raw)

    figure, (axis, below, changing) = plt.subplots(3, 1, figsize=(10, 8.4), sharex=True,
                                                   gridspec_kw=dict(height_ratios=[3, 1.6, 1.6]),
                                                   constrained_layout=True)
    if meta.get("kind") == "manual":
        title = "Aufnahme von Hand  -  %.1f s, %d Messungen" % (t[-1] - t[0], len(samples))
    else:
        title = ("Shot %s  -  %s g in %s s, %s  (%d Messungen)"
                 % (meta.get("shot", "?"), end.get("dose", "?"), end.get("dur", "?"),
                    end.get("reason", "?"), len(samples)))
    figure.suptitle(title, fontsize=11, x=0.01, ha="left")
    axis.plot(t, values, linestyle="none", marker="o", markersize=2.8,
              color=theme["series"][0], markeredgewidth=0, label="Messwerte", zorder=3)
    # Both filters are drawn as the staircases they are: a filter holds its value until its next update,
    # and the old one only updates every BUNDLE readings - about twice a second. Connecting its points
    # with straight lines would make it look as if it followed the scale continuously
    for when, filtered, color, label in (
            (*old_filter(t, values), theme["series"][1],
             "alter Filter (%d gebündelt + Kalman, %.1f Hz)" % (BUNDLE, rate(t) / BUNDLE)),
            (t, average_kalman(t, values), theme["series"][2],
             "Mittel der letzten %d + Kalman + Hysterese (%.1f Hz)" % (AVERAGE_WINDOW, rate(t))),
            (t, hysteresis(*v01(t, values)), theme["trained"],
             "v01: Gerade über das längste Fenster auf %.2f g, dann Kalman (q/Fehler %.0f)"
             % (ADAPTIVE_TOLERANCE, ADAPTIVE_KALMAN[2] / ADAPTIVE_KALMAN[0]))):
        # Hidden, the code for them stays: the plain moving average and the two "neu" filters, whose
        # flatness came from the slope and from sigma - trend_filter() with slope_flatness or
        # sigma_flatness still builds them
        axis.plot(when, filtered, linewidth=0.7, color=color, label=label, zorder=4,
                  drawstyle="steps-post")
    handles, _ = axis.get_legend_handles_labels()
    axis.legend(handles=handles, loc="upper left")
    axis.set_ylabel(unit(net, raw))
    grid(axis, theme, step, meta.get("sf", 1.0) if raw else 0)
    event_lines(axis, marks, theme)
    # The same value once more as a curve: what the shading only hints at through its depth can be read
    # off here, together with the threshold from which the harder hysteresis is in force
    # The truth underneath, as a check: where the centred average says the scale really was standing or
    # moving. It is drawn as a band because it has no value in between - where it is neither, it is blank
    for label, height in ((1.0, (0.96, 1.0)), (0.0, (0.0, 0.04))):
        run = None
        for index, value in enumerate(standing_truth(t, values) + [None]):
            if value == label and run is None:
                run = index
            elif value != label and run is not None:
                below.axhspan(*height, xmin=0, xmax=1, clip_on=True, color=theme["muted"], alpha=0.0)
                below.fill_between(t[run:index], height[0], height[1], color=theme["muted"],
                                   linewidth=0, zorder=2)
                run = None
    # The three detectors of v01, each on the raw readings; grinding also needs how fast the weight
    # really moves, which is what the third panel draws
    average = moving_average(t, values, AVERAGE_PANEL)[1]
    change = derivative(t, average)
    longest = max(FLAT_V01_LONG, GRIND_V01_LONG)
    flat, placed, grinding = [], [], []
    for index in range(len(values)):
        recent = values[max(0, index - longest + 1):index + 1]
        flat.append(flat_v01(recent, change[index]))
        placed.append(placed_v01(values[index - 1] if index else values[0], values[index]))
        grinding.append(grinding_v01(t[max(0, index - longest + 1):index + 1], recent, change[index]))

    for curve, color, label in ((flat, theme["new"], "flach_v01"),
                                (placed, theme["sigma"], "aufsetzen_v01"),
                                (grinding, theme["grind"], "mahlen_v01")):
        below.plot(t, curve, linewidth=0.7, color=color, drawstyle="steps-post", zorder=3, label=label)
        # Lightly filled underneath, so three curves that share the 0 and the 1 can still be told apart
        below.fill_between(t, 0, curve, step="post", color=color, alpha=0.12, linewidth=0, zorder=2)

    # The moving average itself is not drawn, only how fast it changes
    changing.plot(t, change, linewidth=0.7, color=theme["series"][0],
                  drawstyle="steps-post", zorder=3)
    changing.axhline(0, color=theme["rule"], linewidth=0.7, zorder=1)
    # Above the panel, otherwise it sits on the curves, which spend much of their time at 0 and at 1
    below.legend(loc="lower right", bbox_to_anchor=(1, 1.0), ncol=3, borderaxespad=0.3)
    below.set_title("Erkennung v01  -  Balken: wirklich gestanden bzw. bewegt",
                    loc="left", color=theme["muted"])
    below.set_ylim(0, 1)
    below.set_yticks((0, 0.5, 1))
    below.set_ylabel("Erkennung")
    below.grid(axis="y", color=theme["grid"], linewidth=0.6)
    below.set_axisbelow(True)

    changing.set_title("Ableitung des gleitenden Durchschnitts über %d Messungen" % AVERAGE_PANEL,
                       loc="left", color=theme["muted"])
    changing.set_ylabel("Änderung (g/s)")
    # Capped: putting a cup on produces hundreds of g/s and would flatten everything else to a line
    low, high = min(change), max(change)
    changing.set_ylim(max(-CHANGE_MOST, low - 0.5), min(CHANGE_MOST, high + 0.5))
    changing.grid(axis="y", color=theme["grid"], linewidth=0.6)
    changing.set_axisbelow(True)
    for panel in (below, changing):
        event_lines(panel, marks, theme, label=False)
    changing.set_xlabel("Zeit seit Aufnahmestart (s)" if meta.get("kind") == "manual"
                        else "Zeit seit Cup-Erkennung (s)")

    if show:
        plt.show()
    else:
        figure.savefig(path)
        plt.close(figure)


def plot_overview(logs, theme, path, show, net, raw, step):
    import matplotlib.pyplot as plt

    figure, axis = plt.subplots(figsize=(11, 5.5), constrained_layout=True)
    figure.suptitle("%d Mahlvorgänge" % len(logs), fontsize=11, x=0.01, ha="left")
    for index, log in enumerate(logs):
        t, values = series(log, net, raw)
        axis.plot(t, values, linestyle="none", marker="o", markersize=2.4, markeredgewidth=0,
                  color=theme["series"][index % len(theme["series"])],
                  label="Shot %s" % log[0].get("shot", "?"))
    axis.set_xlabel("Zeit seit Cup-Erkennung (s)")
    axis.set_ylabel(unit(net, raw))
    grid(axis, theme, step, logs[0][0].get("sf", 1.0) if raw else 0)
    axis.legend(loc="upper left", markerscale=3)

    if show:
        plt.show()
    else:
        figure.savefig(path)
        plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="*", help="log files (default: logs/*.csv)")
    parser.add_argument("--out", default="logs/plots", help="folder for the images (logs/plots)")
    parser.add_argument("--show", action="store_true", help="open the windows instead of writing files")
    parser.add_argument("--dark", action="store_true", help="draw on a dark surface")
    parser.add_argument("--net", action="store_true", help="weight without the cup")
    parser.add_argument("--raw", action="store_true", help="the counts of the HX711 instead of grams")
    parser.add_argument("--grid", type=float, default=0.1, metavar="G",
                        help="grams between two lines of the grid (0.1)")
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

    logs = []
    for path in paths:
        log = load(path)
        if not log[3]:
            print("%s has no readings, skipped" % path, file=sys.stderr)
            continue
        logs.append(log)
        target = os.path.join(args.out, os.path.splitext(os.path.basename(path))[0] + "." + args.format)
        plot_grind(log, theme, target, args.show, args.net, args.raw, args.grid)
        print(path if args.show else target)

    if len(logs) > 1:
        target = os.path.join(args.out, "overview." + args.format)
        plot_overview(logs, theme, target, args.show, args.net, args.raw, args.grid)
        if not args.show:
            print(target)
    return 0


if __name__ == "__main__":
    sys.exit(main())
