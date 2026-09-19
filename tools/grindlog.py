#!/usr/bin/env python3
"""Records the grind logs of the scale over the serial USB connection.

The firmware sends every single reading of the load cell while a grind is running, unfiltered and at the
10 Hz of the HX711 (see include/grindlog.hpp). This script listens on the serial port, collects the lines
of one grind and writes them into their own file, one per grind:

    tools/grindlog.py                       # finds the port itself, writes into logs/
    tools/grindlog.py --port /dev/cu.usbserial-0001 --dir logs
    tools/grindlog.py --echo                # also show the other serial output of the firmware
    tools/grindlog.py --replay captured.txt # take the lines from a file instead of the port

It also records without a grind, for the noise of a resting scale or for weights put on by hand. While
it is listening, the keys do that:

    r   start a recording, whatever the scale is doing
    s   end it
    q   quit

A grind that starts during such a recording does not interrupt it, it only leaves its markers in it.
For a script there is --record <seconds> instead, which needs no keyboard.

The files are CSV with the description of the grind in the leading comment lines, so they can be read with
pandas.read_csv(path, comment="#") and the comments with json.loads of everything after "# meta " etc.
"""

import argparse
import datetime
import glob
import json
import os
import re
import select
import sys
import time

try:
    import termios
    import tty
except ImportError: # not a terminal that can do this, the keys are then unavailable
    termios = None

PREFIX = "GBW>"
FIELD = re.compile(r'([A-Za-z_][A-Za-z_0-9]*)=("[^"]*"|\S*)')


def number(text):
    """The value of a field as int or float where it is one, otherwise as text"""
    if text.startswith('"'):
        return text[1:-1]
    for convert in (int, float):
        try:
            return convert(text)
        except ValueError:
            pass
    return text


def fields(text):
    return {key: number(value) for key, value in FIELD.findall(text)}


class Grind:
    """The lines of one grind, from GBW>begin to GBW>end"""

    def __init__(self, meta):
        self.meta = meta
        self.marks = []
        self.samples = []
        self.end = None
        self.recorded = datetime.datetime.now()

    def name(self):
        stamp = self.recorded.strftime("%Y%m%d-%H%M%S")
        if self.meta.get("kind") == "manual":
            return f"manual-{stamp}.csv"
        shot = self.meta.get("shot")
        return f"grind-{stamp}-shot{shot}.csv" if shot is not None else f"grind-{stamp}.csv"

    def rate(self):
        """Readings per second actually received, the check that nothing was averaged or lost"""
        if len(self.samples) < 2:
            return 0.0
        span = self.samples[-1][0] - self.samples[0][0]
        return (len(self.samples) - 1) * 1000.0 / span if span > 0 else 0.0

    def write(self, directory):
        path = os.path.join(directory, self.name())
        # Two grinds within the same second, or a repeated shot number, must not overwrite each other
        base, extension = os.path.splitext(path)
        attempt = 2
        while os.path.exists(path):
            path = "%s-%d%s" % (base, attempt, extension)
            attempt += 1
        end = self.end if self.end is not None else {"reason": "incomplete"}
        with open(path, "w") as file:
            file.write("# openGBW grind log v%s\n" % self.meta.get("v", "?"))
            file.write("# recorded %s\n" % self.recorded.isoformat(timespec="seconds"))
            file.write("# rate %.2f\n" % self.rate())
            file.write("# meta %s\n" % json.dumps(self.meta, sort_keys=True))
            for mark in self.marks:
                file.write("# mark %s\n" % json.dumps(mark, sort_keys=True))
            file.write("# end %s\n" % json.dumps(end, sort_keys=True))
            file.write("t_ms,raw,g\n")
            for t_ms, raw, grams in self.samples:
                file.write("%d,%d,%.3f\n" % (t_ms, raw, grams))
        return path

    def summary(self):
        end = self.end or {}
        parts = [
            "by hand" if self.meta.get("kind") == "manual" else "shot %s" % self.meta.get("shot", "?"),
            "%s" % end.get("reason", "incomplete"),
            "%d readings" % len(self.samples),
            "%.1f Hz" % self.rate(),
        ]
        if "dose" in end:
            parts.insert(2, "%.2f g in %.2f s" % (end["dose"], end.get("dur", 0)))
        return ", ".join(parts)


class Reader:
    """Turns the lines of the serial port into finished grinds"""

    def __init__(self, directory, echo=False):
        self.directory = directory
        self.echo = echo
        self.grind = None
        self.count = 0

    def line(self, text):
        text = text.strip()
        if not text.startswith(PREFIX):
            if self.echo and text:
                print("   %s" % text, file=sys.stderr)
            return
        body = text[len(PREFIX):]
        kind, _, rest = body.partition(" ")

        if kind == "begin":
            if self.grind is not None:
                self.finish("the previous grind was never finished")
            self.grind = Grind(fields(rest))
            if self.grind.meta.get("kind") == "manual":
                print("recording by hand")
            else:
                print("recording: shot %s, cup %s g" % (self.grind.meta.get("shot", "?"),
                                                        self.grind.meta.get("cup_empty", "?")))
        elif self.grind is None or kind == "cols":
            return  # started in the middle of a grind, or the column names we already know
        elif kind == "d":
            parts = rest.split()
            if len(parts) == 3:
                try:
                    self.grind.samples.append((int(parts[0]), int(parts[1]), float(parts[2])))
                except ValueError:
                    pass  # a line garbled by other serial output, the next one carries on
        elif kind == "m":
            when, _, what = rest.partition(" ")
            try:
                self.grind.marks.append(dict(t_ms=int(when), text=what, **fields(what)))
            except ValueError:
                pass
        elif kind == "end":
            when, _, what = rest.partition(" ")
            reason, _, extra = what.partition(" ")
            try:
                self.grind.end = dict(t_ms=int(when), reason=reason, **fields(extra))
            except ValueError:
                self.grind.end = dict(reason=reason)
            self.finish()

    def finish(self, note=None):
        if self.grind is None:
            return
        if note:
            print("warning: %s" % note, file=sys.stderr)
        if self.grind.samples:
            path = self.grind.write(self.directory)
            self.count += 1
            print("%s  (%s)" % (path, self.grind.summary()))
        self.grind = None


def load(path):
    """Reads a log file written by this script back in: (meta, marks, end, samples).

    samples is a list of (t_ms, raw, g). tools/plotgrind.py uses it, and it is the short way to get at a
    recorded grind from a notebook without pandas.
    """
    meta, marks, end, samples = {}, [], {}, []
    with open(path) as file:
        for line in file:
            line = line.strip()
            if line.startswith("# meta "):
                meta = json.loads(line[7:])
            elif line.startswith("# mark "):
                marks.append(json.loads(line[7:]))
            elif line.startswith("# end "):
                end = json.loads(line[6:])
            elif line and not line.startswith("#") and not line.startswith("t_ms"):
                t_ms, raw, grams = line.split(",")
                samples.append((int(t_ms), int(raw), float(grams)))
    return meta, marks, end, samples


class Keyboard:
    """Single keypresses, without waiting for Enter. Does nothing where that is not possible"""

    def __init__(self):
        self.live = termios is not None and sys.stdin.isatty()
        self.saved = None

    def __enter__(self):
        if self.live:
            self.saved = termios.tcgetattr(sys.stdin)
            tty.setcbreak(sys.stdin.fileno())
        return self

    def __exit__(self, *ignored):
        if self.saved is not None:
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, self.saved)

    def watched(self):
        return [sys.stdin] if self.live else []


def find_port():
    """The serial port of the ESP32, as it is called on macOS and on Linux"""
    for pattern in ("/dev/cu.usbserial*", "/dev/cu.wchusbserial*", "/dev/cu.SLAB_USBtoUART*",
                    "/dev/cu.usbmodem*", "/dev/ttyUSB*", "/dev/ttyACM*"):
        found = sorted(glob.glob(pattern))
        if found:
            return found[0]
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial port of the scale (found automatically by default)")
    parser.add_argument("--baud", type=int, default=115200, help="baud rate, as in platformio.ini (115200)")
    parser.add_argument("--dir", default="logs", help="folder for the log files (logs)")
    parser.add_argument("--echo", action="store_true", help="also show the other serial output")
    parser.add_argument("--replay", help="read the lines from this file instead of the serial port")
    parser.add_argument("--record", type=float, metavar="SECONDS", nargs="?", const=0.0,
                        help="for scripts: start a recording at once and run it this long "
                             "(0 or no number: until Ctrl-C), instead of using the keys")
    args = parser.parse_args()

    os.makedirs(args.dir, exist_ok=True)
    reader = Reader(args.dir, args.echo)

    if args.replay:
        with open(args.replay, errors="replace") as file:
            for line in file:
                reader.line(line)
        reader.finish("the file ends in the middle of a grind")
        return 0

    try:
        import serial  # pyserial, install with: pip install pyserial
    except ImportError:
        print("pyserial is missing, install it with: pip install pyserial", file=sys.stderr)
        return 1

    port = args.port or find_port()
    if port is None:
        print("no serial port found, name it with --port", file=sys.stderr)
        return 1

    connection = serial.Serial()
    connection.port = port
    connection.baudrate = args.baud
    connection.timeout = 0.2
    connection.dtr = False  # opening the port must not reset the ESP32 and interrupt a grind
    connection.rts = False
    connection.open()
    print("listening on %s at %d baud, the logs land in %s/" % (port, args.baud, args.dir))

    def command(letter):
        connection.write(letter)
        connection.flush()

    with Keyboard() as keyboard:
        if keyboard.live:
            print("keys: r record without a grind, s end it, q quit")
        elif args.record is None:
            print("no terminal for the keys, use --record <seconds> to record without a grind")

        recording = False
        until = None
        if args.record is not None:
            command(b"r")
            recording = True
            until = time.monotonic() + args.record if args.record else None
            print("recording%s" % (", %g seconds" % args.record if args.record else ", stop with Ctrl-C"))

        rest = b""
        quitting = False
        try:
            while not quitting:
                ready = select.select([connection] + keyboard.watched(), [], [], 0.2)[0]

                if connection in ready:
                    rest += connection.read(max(1, connection.in_waiting))
                    while b"\n" in rest:
                        line, _, rest = rest.partition(b"\n")
                        reader.line(line.decode("utf-8", errors="replace"))

                if sys.stdin in ready:
                    key = sys.stdin.read(1)
                    if key == "r" and not recording:
                        command(b"r")
                        recording = True
                    elif key == "s" and recording:
                        command(b"s")
                        recording = False
                    elif key in ("q", "\x03", "\x04"):
                        quitting = True

                if until is not None and time.monotonic() >= until:
                    break
        except KeyboardInterrupt:
            pass

        # The scale sends its last second after the "s", so the end of the file is waited for
        if recording:
            command(b"s")
            deadline = time.monotonic() + 3
            while reader.grind is not None and time.monotonic() < deadline:
                rest += connection.read(max(1, connection.in_waiting or 1))
                while b"\n" in rest:
                    line, _, rest = rest.partition(b"\n")
                    reader.line(line.decode("utf-8", errors="replace"))

    reader.finish("stopped in the middle of a recording")
    print("%d recordings written" % reader.count)
    connection.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
