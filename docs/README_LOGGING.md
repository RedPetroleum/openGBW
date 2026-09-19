Recording grinds over the serial connection and working on the filters with the recordings.
Back to the [main README](../README.md).

# Grind log over USB

While a grind is running, the firmware sends every single reading of the load cell over the serial USB connection: unfiltered, unaveraged and at the full 10 Hz of the HX711, the raw material for working on the filter. Recording starts as soon as a cup has been detected and ends one second after the dose has been verified (or after an abort), so the settling of the scale is part of the log.

`tools/grindlog.py` listens on the port and writes one file per grind:

```sh
pip install pyserial
tools/grindlog.py                       # finds the port itself, the logs land in logs/
tools/grindlog.py --port /dev/cu.usbserial-0001 --dir logs --echo
tools/grindlog.py --replay captured.txt # take the lines from a captured serial output instead
```

The files are CSV with the description of the grind in the leading comment lines, so they read with `pandas.read_csv(path, comment="#")`:

```
# openGBW grind log v1
# rate 10.00
# meta {"shot": 301, "cup_empty": 396.05, "target": 17.5, "offset": -1.67, "sf": 1760.0, "tare": 8391200, "bundle": 5, ...}
# mark {"t_ms": 0, "text": "grinder_on"}
# mark {"t_ms": 3000, "text": "grinder_off w=412.95 target=411.88", "w": 412.95, "target": 411.88}
# mark {"t_ms": 4500, "text": "stop reason=finished", "reason": "finished"}
# end {"t_ms": 5500, "reason": "finished", "dose": 17.42, "dur": 3.1, "n": 56}
t_ms,raw,g
0,9088248,396.050
100,9088875,396.406
```

`raw` is the 24 bit value of the HX711, `g` the same reading as grams, `(raw - tare) / sf`. `bundle` in the header is `SCALE_READINGS_PER_UPDATE` in [include/config.hpp](../include/config.hpp): how many of these readings the firmware averages into one weight for the grinding itself (five, so the weight is updated twice a second). The log always contains every single reading, no matter what that value is, so a different bundling can be tried out on a recorded grind before it is built into the firmware.

It also records without a grind, for the noise of a resting scale or for weights put on by hand. While the script is listening, `r` starts such a recording, `s` ends it and `q` quits - the script passes those on to the scale over the same connection. A grind that starts meanwhile does not interrupt the recording, it only leaves its markers in it. The files land in `logs/manual-<time>.csv` and carry `kind: manual` in their header. For a script there is `--record <seconds>` instead, which needs no keyboard.

The format of the lines is described in [include/grindlog.hpp](../include/grindlog.hpp).

`tools/plotgrind.py` draws the recorded grinds (needs `matplotlib`):

```sh
tools/plotgrind.py                       # all logs in logs/, images into logs/plots/
tools/plotgrind.py --show                # windows instead of files
tools/plotgrind.py --net                 # weight without the cup, so the axis starts at zero
tools/plotgrind.py --raw                 # the counts of the HX711 instead of grams
tools/plotgrind.py --grid 0.5            # a line every 0.5 g instead of every 0.1 g
tools/plotgrind.py --format pdf          # vector as well, SVG is the default
```

Every reading is one point and the points are not connected, so what is on screen is what the load cell delivered and nothing in between. The grid has a line every 0.1 g (coarser where that would be thousands of lines) and a label every whole gram. The images are SVG and can be zoomed into without the points turning into blocks.

Three panels per grind, on a common time axis:

1. **The weight** with the filters over it, drawn as the staircases they are - a filter holds its value until its next update. The one of the firmware bundles five readings into one and updates about twice a second; next to it the same Kalman filter on a *moving* average of five readings, ten times a second; v01, which lays a straight line through the longest window that still fits the readings within 0.15 g and reads it off at the newest reading. Where the weight rests a long window fits and v01 is quiet, where grounds land in clumps only a short one does and it is quick - and unlike an average a straight line does not trail a rising weight however long its window is. And v03, which takes its value from v01 and steps like any other filter while the weight is moving - but where `flach_v02` - the answer of the third panel - is at 1, a single 0.1 g step also needs v02 to land on that same step, and the value then comes from v02. Lying flat and with v02 still on the old step, the display stays where it is. A shown value within 0.2 g of zero for ten readings in a row is shown as a plain zero, which changes the display only and tares nothing.
2. **The three detectors of v01**, all between 0 and 1: `flach_v01` from the spread of the readings, faded out towards 0 as soon as the weight really moves by 0.5 g/s or more and 0 from 1 g/s on, `aufsetzen_v01` for a step of more than 1.5 g from one reading to the next, and `mahlen_v01` from the rate of a straight line over the last readings - and 0 wherever the weight is really moving by less than 1 g/s and the value has not made it past 0.6. Two dark bars say what really happened - a centred average, which looks as far forward as backward and therefore knows the real course of the weight, marks where the scale was standing and where it was moving.
3. **The answer**: `flach_v02`, `aufsetzen_v02` and `mahlen_v02`, of which at most one is 1 and the others are 0. A detector has to be at 1 to win, or at 0.8 when the other two are at zero; grinding beats something being put on when both are at 1, and any other tie is no answer at all.
4. **How fast the weight changes**: the derivative of a moving average over the last 15 readings, in grams per second. The average itself is not drawn.

Plus an overview with all grinds on top of each other, points only.

Two tools search parameters on the recordings. [tools/trainfilter.py](../tools/trainfilter.py) fitted v01, measuring three things separately: how straight the filter is while the weight rests at the start and at the end, and how far it is from that centred, lag-free average while the weight rises. [tools/trainflat.py](../tools/trainflat.py) fitted a flatness as a logistic regression on the spread over the last 5, 10, 20 and 40 readings - the short window reacts at once, the long ones only become small once the weight has been standing for a while. Both of those, and the two filters whose smoothing is gated by a flatness measure, are still in the script but not drawn.

All the numbers sit as constants at the top of [tools/plotgrind.py](../tools/plotgrind.py).
