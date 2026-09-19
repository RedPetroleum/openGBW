#include <stdarg.h>

#include "config.hpp"
#include "grindlog.hpp"
#include "scale.hpp"

// See grindlog.hpp for the format of the lines.
//
// Two tasks are involved: the sampler task reads the load cell and is the only one that writes log lines,
// the scale status task starts and ends a recording and hands over its events. Everything the status task
// produces therefore goes through a small queue that the sampler empties, so no two tasks write into the
// same line and the serial output stays readable.

enum GrindLogState
{
    GRIND_LOG_IDLE,      // nothing is being recorded
    GRIND_LOG_REQUESTED, // a grind has started, the header is written with the next reading
    GRIND_LOG_RUNNING    // readings are being written
};

struct GrindLogMark
{
    unsigned long at;
    char text[GRIND_LOG_MARK_LENGTH];
};

static volatile GrindLogState logState = GRIND_LOG_IDLE;
static unsigned long logStartedAt = 0; // millis() of the cup detection, all times in the log refer to it
static unsigned long logStopAt = 0;    // the recording ends at this time, 0 while the grind is still running
static unsigned long logSamples = 0;
static char logReason[24] = "";
static bool logManual = false; // started by hand over the serial connection, not by a grind
static char logDetail[GRIND_LOG_MARK_LENGTH] = "";

// Single producer (scale status task), single consumer (sampler task), so the two indices need no lock
static GrindLogMark logMarks[GRIND_LOG_MARKS];
static volatile uint8_t logMarkHead = 0;
static volatile uint8_t logMarkTail = 0;

// Milliseconds since the recording started, the timestamp of every line
static unsigned long logTime(unsigned long at)
{
    return at < logStartedAt ? 0 : at - logStartedAt;
}

// Writes the events that happened up to this point, oldest first
static void writeMarks()
{
    while (logMarkTail != logMarkHead) {
        GrindLogMark &mark = logMarks[logMarkTail];
        Serial.printf("GBW>m %lu %s\n", logTime(mark.at), mark.text);
        logMarkTail = (logMarkTail + 1) % GRIND_LOG_MARKS;
    }
}

// Cup detected: a grind starts. The header is written by the sampler task with the first reading, so the
// values it contains are the ones the grind really runs with
// Reads the commands of the serial connection: "r" starts a recording without a grind, "s" ends it.
// Called by the scale status task, which is the only one that may hand markers to the sampler
void grindLogPoll()
{
    while (Serial.available()) {
        char command = Serial.read();
        if (command == 'r' && !logManual) {
            grindLogBegin();
            logManual = true; // set after the begin, which would otherwise take it for a restart
            grindLogMark("manual_start");
        } else if (command == 's' && logManual) {
            logManual = false;
            grindLogEnd("manual");
        }
    }
}

void grindLogBegin()
{
    if (logManual) {
        grindLogMark("grind_start"); // a grind during a recording by hand does not interrupt it
        return;
    }
    logMarkHead = logMarkTail; // an unfinished recording is dropped, the reader discards it
    logStartedAt = millis();
    logStopAt = 0;
    logSamples = 0;
    logReason[0] = 0;
    logDetail[0] = 0;
    logState = GRIND_LOG_REQUESTED;
}

// An event of the grind (grinder switched, target reached, dose verified), timestamped now
void grindLogMark(const char *format, ...)
{
    if (logState == GRIND_LOG_IDLE) {
        return;
    }
    uint8_t next = (logMarkHead + 1) % GRIND_LOG_MARKS;
    if (next == logMarkTail) {
        return; // the queue is full, the sampler is not writing (scale error); the log ends without this event
    }
    logMarks[logMarkHead].at = millis();
    va_list args;
    va_start(args, format);
    vsnprintf(logMarks[logMarkHead].text, GRIND_LOG_MARK_LENGTH, format, args);
    va_end(args);
    logMarkHead = next;
}

// The grind is over. Recording continues for GRIND_LOG_TAIL_MS, so the log also shows how the scale
// settles after the last grounds have landed
void grindLogEnd(const char *reason, const char *format, ...)
{
    if (logState == GRIND_LOG_IDLE || logStopAt != 0) {
        return; // nothing is being recorded, or the tail is already running
    }
    if (logManual) {
        grindLogMark("grind_end %s", reason); // the recording by hand runs on until an "s" comes
        return;
    }
    snprintf(logReason, sizeof(logReason), "%s", reason);
    if (format != 0) {
        va_list args;
        va_start(args, format);
        vsnprintf(logDetail, sizeof(logDetail), format, args);
        va_end(args);
    }
    grindLogMark("stop reason=%s", reason); // the moment the grind ended, the tail follows it
    logStopAt = millis() + GRIND_LOG_TAIL_MS;
}

// One reading of the load cell, exactly as the HX711 delivered it: no average, no Kalman estimate,
// nothing left out. Called by the sampler task for every single reading, about ten times a second
void grindLogSample(long raw, double grams)
{
    if (logState == GRIND_LOG_IDLE) {
        return;
    }
    if (logState == GRIND_LOG_REQUESTED) {
        // Which of the two cups was recognised, for the record: the nearer one
        double cupSet = ABS(cupWeightEmpty - setCupWeight) <= ABS(cupWeightEmpty - setCupWeight2) ? setCupWeight : setCupWeight2;
        // shotCount is only counted up once the dose is verified, so the grind that is starting here
        // is the next one; that is also the number it gets in the Weight History
        Serial.printf("GBW>begin v=%d kind=%s t=%lu shot=%u cup_set=%.2f cup_empty=%.2f target=%.2f dead=%.2f "
                      "sf=%.3f tare=%ld bundle=%d mode=%s grinder=%s\n",
                      GRIND_LOG_VERSION, logManual ? "manual" : "grind", logStartedAt, shotCount + 1,
                      cupSet, cupWeightEmpty, setWeight, deadTimeEnd,
                      (double)loadcell.get_scale(), loadcell.get_offset(), SCALE_READINGS_PER_UPDATE,
                      scaleMode ? "timer" : "weight", grindMode ? "continuous" : "impulse");
        Serial.println("GBW>cols t_ms raw g");
        logState = GRIND_LOG_RUNNING;
    }

    writeMarks();
    Serial.printf("GBW>d %lu %ld %.3f\n", logTime(millis()), raw, grams);
    logSamples++;

    if (logStopAt != 0 && (long)(millis() - logStopAt) >= 0) {
        writeMarks(); // events of the last moment, e.g. a cup taken off during the tail
        Serial.printf("GBW>end %lu %s n=%lu%s%s\n", logTime(millis()), logReason, logSamples,
                      logDetail[0] ? " " : "", logDetail);
        if (logState == GRIND_LOG_RUNNING) {
            logState = GRIND_LOG_IDLE; // a grind that started in the meantime keeps its own recording
        }
    }
}
