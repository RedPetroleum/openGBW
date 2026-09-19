#pragma once

// Log of a single grind: every load cell reading of the HX711, unfiltered and undivided, sent over the
// serial USB connection. It is the raw material for reworking the filter, so nothing in here smooths,
// averages or drops readings: what the chip delivers at its 10 Hz is what goes out, one line per reading.
//
// Recording starts when a cup has been detected and ends GRIND_LOG_TAIL_MS after the grind is over
// (a verified dose, an abort or a cup taken away), so the settling of the scale is part of the log.
//
// It can also be recorded without a grind: a "r" on the serial connection starts a recording that runs
// until an "s" comes, whatever the scale is doing meanwhile - for the noise of a resting scale, for
// weights put on by hand, for anything a filter has to cope with. A grind that starts during such a
// recording does not interrupt it, it only leaves its markers in it.
//
// Format of the lines, all of them prefixed with "GBW>" so they can be picked out of the other serial
// output; t_ms is milliseconds since the recording started:
//
//   GBW>begin v=1 kind=grind|manual t=<millis> shot=.. cup_empty=.. target=.. sf=.. tare=.. ...
//   GBW>cols t_ms raw g
//   GBW>m <t_ms> <event> [key=value ...]
//   GBW>d <t_ms> <raw> <g>
//   GBW>end <t_ms> <reason> [key=value ...]
//
// "raw" is the 24 bit value of the HX711, "g" the same reading as grams: (raw - tare) / sf. Both are in
// every line, so a log can be read without knowing the tare point and still be recalculated from the
// counts. tools/grindlog.py writes one file per grind from this.

#define GRIND_LOG_VERSION 1
#define GRIND_LOG_TAIL_MS 1000   // keep recording this long after the grind ended
#define GRIND_LOG_MARKS 12       // events the scale status task can hand over before the sampler prints them
#define GRIND_LOG_MARK_LENGTH 48 // length of one event text

// Called by the scale status task
void grindLogPoll();                                             // reads the commands from the serial connection
void grindLogBegin();                                            // a cup was detected, a grind starts
void grindLogMark(const char *format, ...);                      // an event, timestamped now
void grindLogEnd(const char *reason, const char *format = 0, ...); // the grind is over, the tail is still recorded

// Called by the sampler task for every single reading of the load cell
void grindLogSample(long raw, double grams);
