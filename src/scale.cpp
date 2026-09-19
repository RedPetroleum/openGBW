#include "config.hpp"
#include "grindlog.hpp"
#include "rotary.hpp"
#include "scale.hpp"

// Variables for scale functionality
double scaleWeight = 0;       // Current weight measured by the scale
double shownWeight = 0;       // ... in steps of DISPLAY_STEP with the hysteresis, what the display shows
double previousScaleWeight = 0; // Weight of the reading before the current one
double setWeight = 0;         // Target weight set by the user
double setCupWeight = 0;      // Weight of the cup set by the user
double setCupWeight2 = 0;     // Weight of the second cup set by the user
double offset = 0;            // Offset for stopping grinding prior to reaching set weight
double scaleFactor = LOADCELL_SCALE_FACTOR; // Load cell calibration factor
bool scaleMode = false;       // Indicates if the scale is used in timer mode
bool grindMode = true;        // Grinder mode: impulse (false) or continuous (true, default)
bool grinderActive = false;   // Grinder state (on/off)
unsigned int shotCount;  

// Buffer for storing recent weight history
MathBuffer<double, WEIGHT_HISTORY_SIZE> weightHistory;

// Last finished grinds, newest first
GrindRecord grindHistory[GRIND_HISTORY_SIZE];
int grindHistoryCount = 0;

// Timing and status variables
unsigned long scaleLastUpdatedAt = 0;  // Timestamp of the last scale update
unsigned long lastActivityAt = 0; // Timestamp of the last scale change or knob use (display sleep timer)
unsigned long lastTareAt = 0; // Timestamp of the last tare operation
bool scaleReady = false;      // Indicates if the scale is ready to measure
bool scaleTared = false;      // True once a tare has succeeded, until then taring is retried immediately
int scaleStatus = STATUS_EMPTY; // Current status of the scale
double cupWeightEmpty = 0;    // Measured weight of the empty cup
unsigned long startedGrindingAt = 0;  // Timestamp of when grinding started
unsigned long finishedGrindingAt = 0; // Timestamp of when grinding finished
bool greset = false;          // Flag for reset operation
bool newOffset = false;       // Indicates if a new offset value is pending
const char *grindFailReason = ""; // Why the last grind was aborted, shown on the display

// Tares the scale (sets the current weight to zero). The readings are taken one by one instead of
// through loadcell.tare(), so that a weight arriving while taring can be noticed: taring takes about
// two seconds, and a cup placed during them would end up in the new zero point with a part of its weight.
void tareScale() {
    Serial.println("Taring scale");
    long sum = 0, lowest = 0, highest = 0;
    for (int measure = 0; measure < TARE_MEASURES; measure++) {
        if (!loadcell.wait_ready_timeout(300)) {
            Serial.println("Tare aborted, scale does not answer");
            scaleReady = false;
            return; // lastTareAt stays untouched, so this is tried again right away
        }
        long reading = loadcell.read();
        sum += reading;
        if (measure == 0 || reading < lowest) {
            lowest = reading;
        }
        if (measure == 0 || reading > highest) {
            highest = reading;
        }
        delay(0); // feeds the watchdog on the ESP32
    }

    double spread = ABS((highest - lowest) / scaleFactor);
    if (spread > TARE_MAX_SPREAD) {
        Serial.printf("Tare discarded, the readings are %.1fg apart\n", spread);
        // Once the scale has a zero point, the next quiet moment is awaited instead of taring right away,
        // which would zero away whatever was just placed on the scale
        lastTareAt = scaleTared ? millis() : 0;
        return;
    }

    loadcell.set_offset(sum / TARE_MEASURES);
    scaleTared = true;
    lastTareAt = millis();
}

// Filter v01, see config.hpp. The readings of the window and their time in seconds, oldest first;
// a step empties the window, because the readings before it describe a different weight
static double v01Values[FILTER_V01_LONGEST];
static double v01Seconds[FILTER_V01_LONGEST];
static int v01Count = 0;
static double v01Fitted = 0; // value of the straight line, and what a step is measured against
static double v01Slope = 0;  // its slope, which tells the display hysteresis how flat the weight lies
static bool v01Ready = false;

// Straight line through the newest `count` readings: returns its value at the newest one and writes
// its slope and how far the readings sit off it (RMS)
static double v01Line(int count, double *slope, double *rms) {
    int first = v01Count - count;
    if (count < 2) {
        *slope = 0;
        *rms = 0;
        return v01Values[first];
    }
    double meanTime = 0, meanValue = 0;
    for (int i = first; i < v01Count; i++) {
        meanTime += v01Seconds[i];
        meanValue += v01Values[i];
    }
    meanTime /= count;
    meanValue /= count;

    double spread = 0, mixed = 0;
    for (int i = first; i < v01Count; i++) {
        spread += (v01Seconds[i] - meanTime) * (v01Seconds[i] - meanTime);
        mixed += (v01Seconds[i] - meanTime) * (v01Values[i] - meanValue);
    }
    *slope = spread > 0 ? mixed / spread : 0;

    double squares = 0;
    for (int i = first; i < v01Count; i++) {
        double off = v01Values[i] - (meanValue + *slope * (v01Seconds[i] - meanTime));
        squares += off * off;
    }
    *rms = sqrt(squares / count);
    return meanValue + *slope * (v01Seconds[v01Count - 1] - meanTime);
}

// One raw reading in, the filtered weight out
static double v01Filter(double grams, unsigned long at) {
    if (!v01Ready) {
        v01Fitted = grams;
        v01Ready = true;
    }
    if (ABS(grams - v01Fitted) >= FILTER_V01_JUMP) {
        v01Count = 0;
    }
    if (v01Count == FILTER_V01_LONGEST) {
        for (int i = 1; i < FILTER_V01_LONGEST; i++) {
            v01Values[i - 1] = v01Values[i];
            v01Seconds[i - 1] = v01Seconds[i];
        }
        v01Count--;
    }
    v01Values[v01Count] = grams;
    v01Seconds[v01Count] = at / 1000.0;
    v01Count++;

    double rms;
    for (int count = v01Count; count >= 1; count--) {
        v01Fitted = v01Line(count, &v01Slope, &rms);
        if (count <= FILTER_V01_SHORTEST || rms <= FILTER_V01_TOLERANCE) {
            break; // the longest window a straight line still fits
        }
    }
    return kalmanV01.updateEstimate(v01Fitted);
}

// Filter v02: the average of the last FILTER_V02_WINDOW readings, through the Kalman filter of the old
// filter. The same amount of averaging as the old one and the same Kalman values, but as a moving
// average - so a weight comes out for every reading and not only for every fifth
static double v02Values[FILTER_V02_WINDOW];
static int v02Count = 0;

static double v02Filter(double grams) {
    if (v02Count == FILTER_V02_WINDOW) {
        for (int i = 1; i < FILTER_V02_WINDOW; i++) {
            v02Values[i - 1] = v02Values[i];
        }
        v02Count--;
    }
    v02Values[v02Count++] = grams;

    double sum = 0;
    for (int i = 0; i < v02Count; i++) {
        sum += v02Values[i];
    }
    return kalmanFilter.updateEstimate(sum / v02Count);
}

// ---------------------------------------------------------------------------------------------------
// The three detectors of v03 and its display, developed in tools/plotgrind.py. One ring of the last
// readings feeds all of them

static double detValues[FILTER_V03_WINDOW];
static double detSeconds[FILTER_V03_WINDOW];
static int detCount = 0;
static double detAverages[3] = {0, 0, 0}; // the newest three moving averages, newest first ...
static double detAverageAt[3] = {0, 0, 0}; // ... and when they were taken
static int detAverageCount = 0;

// Difference between the highest and the lowest of the newest `count` readings
static double detSpan(int count) {
    double lowest = detValues[detCount - count], highest = lowest;
    for (int i = detCount - count; i < detCount; i++) {
        if (detValues[i] < lowest) {
            lowest = detValues[i];
        }
        if (detValues[i] > highest) {
            highest = detValues[i];
        }
    }
    return highest - lowest;
}

// Slope of a straight line through the newest `count` readings, in grams per second
static double detSlope(int count) {
    if (count < 2) {
        return 0;
    }
    int first = detCount - count;
    double meanTime = 0, meanValue = 0;
    for (int i = first; i < detCount; i++) {
        meanTime += detSeconds[i];
        meanValue += detValues[i];
    }
    meanTime /= count;
    meanValue /= count;

    double spread = 0, mixed = 0;
    for (int i = first; i < detCount; i++) {
        spread += (detSeconds[i] - meanTime) * (detSeconds[i] - meanTime);
        mixed += (detSeconds[i] - meanTime) * (detValues[i] - meanValue);
    }
    return spread > 0 ? mixed / spread : 0;
}

// How flat it lies: from the spread of the readings, with the rate as a veto over it
static double flatV01(double change) {
    if (detCount < FLAT_V01_SHORT) {
        return 0;
    }
    double speed = ABS(change);
    if (speed >= FLAT_V01_MOVING) {
        return 0;
    }
    double damped = speed < FLAT_V01_QUIET ? 1.0
                    : 1.0 - (speed - FLAT_V01_QUIET) / (FLAT_V01_MOVING - FLAT_V01_QUIET);

    double short_ = detSpan(FLAT_V01_SHORT);
    if (short_ > FLAT_V01_WIDE) {
        return 0;
    }
    if (short_ > FLAT_V01_TIGHT) { // between the two spreads, interpolated over the grams
        double share = (short_ - FLAT_V01_TIGHT) / (FLAT_V01_WIDE - FLAT_V01_TIGHT);
        return damped * (FLAT_V01_FEW - share * (FLAT_V01_FEW - FLAT_V01_LOOSE));
    }

    int count = FLAT_V01_SHORT; // how far back the readings still lie within the tight spread
    int most = detCount < FLAT_V01_LONG ? detCount : FLAT_V01_LONG;
    while (count < most && detSpan(count + 1) <= FLAT_V01_TIGHT) {
        count++;
    }
    double share = (double)(count - FLAT_V01_SHORT) / (FLAT_V01_LONG - FLAT_V01_SHORT);
    return damped * (FLAT_V01_FEW + share * (1.0 - FLAT_V01_FEW));
}

// 1 inside the core range, fading to 0 at the edges of the wide one, 0 outside it
static double grindShare(double rate) {
    if (rate <= GRIND_V01_WIDE_LOW || rate >= GRIND_V01_WIDE_HIGH) {
        return 0;
    }
    if (rate < GRIND_V01_CORE_LOW) {
        return (rate - GRIND_V01_WIDE_LOW) / (GRIND_V01_CORE_LOW - GRIND_V01_WIDE_LOW);
    }
    if (rate > GRIND_V01_CORE_HIGH) {
        return (GRIND_V01_WIDE_HIGH - rate) / (GRIND_V01_WIDE_HIGH - GRIND_V01_CORE_HIGH);
    }
    return 1.0;
}

// Coffee is falling: the weight rises at a rate a grinder makes, and has been for a while
static double grindingV01(double change) {
    double best = 0;
    int most = detCount < GRIND_V01_LONG ? detCount : GRIND_V01_LONG;
    for (int count = most; count >= GRIND_V01_SHORT; count--) {
        double share = grindShare(detSlope(count));
        if (share > 0) {
            double reach = (double)(count - GRIND_V01_SHORT) / (GRIND_V01_LONG - GRIND_V01_SHORT);
            double value = GRIND_V01_FEW + reach * (1.0 - GRIND_V01_FEW) * share;
            if (value > best) {
                best = value;
            }
        }
    }
    return change < GRIND_V01_QUIET && best < GRIND_V01_SURE ? 0 : best;
}

// True where the three detectors agree that the weight is lying flat and nothing else is going on.
// A detector has to be at 1 to win, or at DECIDED_V01_ALONE when the other two are at zero; grinding
// beats something being put on when both are at 1, and any other tie is no answer at all
static bool flatDecided(double flat, double placed, double grinding) {
    int full = (flat >= 1.0) + (placed >= 1.0) + (grinding >= 1.0);
    if (full == 1) {
        return flat >= 1.0;
    }
    if (full > 1) {
        return false; // several at once: either grinding wins, or nobody - never flat
    }
    return flat >= DECIDED_V01_ALONE && placed == 0 && grinding == 0;
}

// One reading into the detectors; returns whether they say the weight is lying flat
static bool v03Detect(double grams, unsigned long at, double previous) {
    if (detCount == FILTER_V03_WINDOW) {
        for (int i = 1; i < FILTER_V03_WINDOW; i++) {
            detValues[i - 1] = detValues[i];
            detSeconds[i - 1] = detSeconds[i];
        }
        detCount--;
    }
    detValues[detCount] = grams;
    detSeconds[detCount] = at / 1000.0;
    detCount++;

    // The moving average and how fast it changes - backwards over two readings, see config.hpp
    int span = detCount < FILTER_V03_AVERAGE ? detCount : FILTER_V03_AVERAGE;
    double sum = 0;
    for (int i = detCount - span; i < detCount; i++) {
        sum += detValues[i];
    }
    for (int i = 2; i > 0; i--) {
        detAverages[i] = detAverages[i - 1];
        detAverageAt[i] = detAverageAt[i - 1];
    }
    detAverages[0] = sum / span;
    detAverageAt[0] = at / 1000.0;
    if (detAverageCount < 3) {
        detAverageCount++;
    }
    double change = 0;
    if (detAverageCount == 3 && detAverageAt[0] > detAverageAt[2]) {
        change = (detAverages[0] - detAverages[2]) / (detAverageAt[0] - detAverageAt[2]);
    }

    return flatDecided(flatV01(change),
                       ABS(grams - previous) > PLACED_V01_GRAMS ? 1.0 : 0.0,
                       grindingV01(change));
}

// Holds a small negative shown value at zero, see config.hpp. The counting runs on the real stepped
// value, so the display jumps to it the moment the hold ends
static double holdNegative(double shown) {
    static int negatives = 0;
    static bool released = false;
    if (shown > -DISPLAY_STEP / 2) {
        negatives = 0;
        released = false;
        return shown; // zero or above, nothing to hold and the hold is armed again
    }
    negatives++;
    // Deep enough, or negative long enough to be a real weight - and once it is out, it stays out
    // until the weight is back at zero, otherwise the display flickers between the value and zero
    if (shown <= -NEGATIVE_V03_GRAMS + DISPLAY_STEP / 2 || negatives >= NEGATIVE_V03_READINGS) {
        released = true;
    }
    return released ? shown : 0.0;
}

// The display of v03: it steps like the ordinary hysteresis while the weight moves, but where the
// detectors say it lies flat, a single step also needs v02 to land on that same step. And a shown value
// within ZERO_V03_GRAMS of zero for ZERO_V03_READINGS readings in a row is shown as a plain zero, and
// a small negative value is held at zero by holdNegative() - the display only, the weight behind it is
// untouched and nothing is tared
static void updateShownV03(double value, double other, double softFlat, bool flat) {
    static int units = 0, pending = 0, direction = 0, zeros = 0;
    static bool started = false;

    int wanted = (int)lround(value / DISPLAY_STEP);
    if (!started) {
        units = wanted;
        started = true;
    }
    bool strict = softFlat >= HYSTERESIS_FLAT_FROM;
    double extra = strict ? HYSTERESIS_GRAMS_FLAT : HYSTERESIS_GRAMS;
    int confirm = strict ? HYSTERESIS_READINGS_FLAT : HYSTERESIS_READINGS;

    int delta = wanted - units;
    if (delta >= 2 || delta <= -2) {
        units = wanted; // more than one step, the hysteresis does not apply
        pending = direction = 0;
    } else if (delta == 0) {
        pending = direction = 0;
    } else {
        if (delta != direction) {
            direction = delta;
            pending = 0;
        }
        pending++;
        double boundary = (units + delta * 0.5) * DISPLAY_STEP + delta * extra;
        if (pending >= confirm || (value - boundary) * delta >= 0) {
            if (!flat) {
                units += delta; // the weight is moving, the display has to follow
                pending = direction = 0;
            } else if ((int)lround(other / DISPLAY_STEP) == units + delta) {
                units += delta; // lying flat and v02 agrees: the step is real
                pending = direction = 0;
            }
            // Lying flat and v02 still on the old step: no step, and `pending` is kept so it is made
            // the moment v02 comes along
        }
    }

    double shown = holdNegative(units * DISPLAY_STEP);
    zeros = ABS(shown) <= ZERO_V03_GRAMS ? zeros + 1 : 0;
    shownWeight = zeros >= ZERO_V03_READINGS ? 0.0 : shown;
}

// Rounds the weight to DISPLAY_STEP, with the hysteresis described in config.hpp. The shown value is
// kept as a whole number of steps, otherwise adding 0.1 over and over drifts off
static void updateShownWeight(double weight, double flatness) {
    static int units = 0, pending = 0, direction = 0;
    static bool started = false;

    int wanted = (int)lround(weight / DISPLAY_STEP);
    if (!started) {
        units = wanted;
        started = true;
    }
    bool strict = flatness >= HYSTERESIS_FLAT_FROM;
    double extra = strict ? HYSTERESIS_GRAMS_FLAT : HYSTERESIS_GRAMS;
    int confirm = strict ? HYSTERESIS_READINGS_FLAT : HYSTERESIS_READINGS;

    int delta = wanted - units;
    if (delta >= 2 || delta <= -2) {
        units = wanted; // more than one step, the hysteresis does not apply
        pending = direction = 0;
    } else if (delta == 0) {
        pending = direction = 0;
    } else {
        if (delta != direction) {
            direction = delta;
            pending = 0;
        }
        pending++;
        // The middle between the two steps, moved by `extra` into the direction the weight wants to go
        double boundary = (units + delta * 0.5) * DISPLAY_STEP + delta * extra;
        if (pending >= confirm || (weight - boundary) * delta >= 0) {
            units += delta;
            pending = direction = 0;
        }
    }
    shownWeight = units * DISPLAY_STEP;
}

// Hands a new weight to the rest of the firmware
static void publishWeight(double weight, double flatness) {
    previousScaleWeight = scaleWeight;
    scaleWeight = weight;
    updateShownWeight(weight, flatness);
    scaleLastUpdatedAt = millis();
    weightHistory.push(scaleWeight);
    scaleReady = true;
}

// Task to continuously update the scale readings
void updateScale(void *parameter) {
    for (;;) {
        if (lastTareAt == 0) {
            Serial.println("retaring scale");
            Serial.println("current offset");
            Serial.println(offset);
            tareScale();
            if (lastTareAt == 0) {
                continue; // the tare did not succeed, no readings before the scale has a zero point
            }
        }
        if (loadcell.wait_ready_timeout(300)) {
            // Single readings without a filter: the game needs the laser to react quickly when the scale
            // is pressed, paging through the Weight History the same, and the Weight Data shows what the
            // load cell really delivers
            bool fastReadings = scaleStatus == STATUS_GAME || currentSetting == GRIND_HISTORY_SETTING ||
                                currentSetting == WEIGHT_DATA_SETTING;
            // The readings are taken one by one instead of through get_units(n), which averages them inside
            // the library: the grind log needs every single one of them, unfiltered and at the full 10 Hz of
            // the HX711, and v01 works on them one by one as well
            int bundle = fastReadings ? 1 : SCALE_READINGS_PER_UPDATE;
            double sum = 0;
            int taken = 0;
            for (int i = 0; i < bundle; i++) {
                if (i > 0 && !loadcell.wait_ready_timeout(300)) {
                    break; // the chip stopped answering mid-bundle, the readings so far still give a weight
                }
                long raw = loadcell.read();
                double grams = (raw - loadcell.get_offset()) / (double)loadcell.get_scale();
                grindLogSample(raw, grams);
                sum += grams;
                taken++;
#if FILTER_FAST
                // The filter is fed every reading, also while a game is running, so its window is
                // current when the game is left. Only the published weight is the raw reading there
#if FILTER == FILTER_V01
                double filtered = v01Filter(grams, millis());
                double flatness = 1.0 - ABS(v01Slope) / FILTER_V01_FLAT_SLOPE;
#elif FILTER == FILTER_V02
                double filtered = v02Filter(grams);
                double flatness = 0; // v02 knows nothing about flat stretches, the soft hysteresis holds
#else
                // v03 runs both: its weight is the one of v01, and v02 has a say in the display
                static double v03Previous = 0;
                bool flat = v03Detect(grams, millis(), v03Previous);
                v03Previous = grams;
                double filtered = v01Filter(grams, millis());
                double other = v02Filter(grams);
                double flatness = 1.0 - ABS(v01Slope) / FILTER_V01_FLAT_SLOPE;
#endif
                if (!fastReadings) {
#if FILTER == FILTER_V03
                    previousScaleWeight = scaleWeight;
                    scaleWeight = filtered;
                    updateShownV03(filtered, other, flatness > 0 ? flatness : 0, flat);
                    scaleLastUpdatedAt = millis();
                    weightHistory.push(scaleWeight);
                    scaleReady = true;
#else
                    publishWeight(filtered, flatness > 0 ? flatness : 0);
#endif
                }
#endif
            }
            if (taken == 0) {
                Serial.println("HX711 stopped answering.");
                scaleReady = false;
                continue;
            }
#if FILTER_FAST
            if (fastReadings) {
                publishWeight(sum / taken, 0);
            }
#else
            float reading = sum / taken;
            publishWeight(fastReadings ? reading : kalmanFilter.updateEstimate(reading), 0);
#endif
        } else {
            Serial.println("HX711 not found.");
            scaleReady = false;
        }
    }
}

// Toggles the grinder on or off based on mode
void grinderToggle() {
    if (!scaleMode) {
        if (grindMode) {
            grinderActive = !grinderActive;
            digitalWrite(GRINDER_ACTIVE_PIN, grinderActive);
        } else {
            digitalWrite(GRINDER_ACTIVE_PIN, 1);
            delay(100);
            digitalWrite(GRINDER_ACTIVE_PIN, 0);
        }
    }
}

// Adds a finished grind to the history (newest first); caller saves it to preferences
void addGrindRecord(uint32_t shot, float duration, float usedOffset, float target, float actual) {
    for (int i = GRIND_HISTORY_SIZE - 1; i > 0; i--) {
        grindHistory[i] = grindHistory[i - 1];
    }
    grindHistory[0] = {shot, duration, usedOffset, target, actual};
    if (grindHistoryCount < GRIND_HISTORY_SIZE) {
        grindHistoryCount++;
    }
}

// Stops the grinder and switches to the failed state, which is left by pressing the knob
void abortGrinding(const char *reason) {
    grinderToggle();
    grindFailReason = reason;
    scaleStatus = STATUS_GRINDING_FAILED;
    grindLogEnd("aborted", "why=\"%s\"", reason);
    Serial.print("Grinding failed: ");
    Serial.println(reason);
}

// Checks if the given cup has been resting on the scale for the last second
// and the reading has settled, so the empty cup is weighed accurately
bool isCupDetected(double cupWeight) {
    return ABS(weightHistory.minSince((int64_t)millis() - 1000) - cupWeight) < CUP_DETECTION_TOLERANCE &&
           ABS(weightHistory.maxSince((int64_t)millis() - 1000) - cupWeight) < CUP_DETECTION_TOLERANCE &&
           weightHistory.isSteady(STEADY_READINGS, STEADY_TOLERANCE);
}

// Task to manage the status of the scale
void scaleStatusLoop(void *p) {
    for (;;) {
        double tenSecAvg = weightHistory.averageSince((int64_t)millis() - 10000);
        // Placing, removing or tapping something on the scale keeps the display awake or wakes it
        if (ABS(tenSecAvg - scaleWeight) > SIGNIFICANT_WEIGHT_CHANGE ||
            (ABS(scaleWeight - previousScaleWeight) > WAKE_WEIGHT_CHANGE && millis() - lastTareAt > WAKE_IGNORE_AFTER_TARE_MS)) {
            lastActivityAt = millis();
        }

        switch (scaleStatus) {
            case STATUS_EMPTY: {
                // Anything resting on the scale up to TARE_MAX_WEIGHT is tared away, however small it is
                if (millis() - lastTareAt > TARE_MIN_INTERVAL && tenSecAvg < TARE_MAX_WEIGHT && scaleWeight < TARE_MAX_WEIGHT &&
                    ABS(scaleWeight - tenSecAvg) < TARE_STEADY_TOLERANCE) {
                    lastTareAt = 0; // Retare if conditions are met
                }
                if (isCupDetected(setCupWeight) || isCupDetected(setCupWeight2)) {
                    // Same window as the cup detection, so it is guaranteed to contain readings
                    cupWeightEmpty = weightHistory.averageSince((int64_t)millis() - 1000);
                    scaleStatus = STATUS_GRINDING_IN_PROGRESS;
                    grindLogBegin(); // from here on every reading of the load cell is logged
                    if (!scaleMode) {
                        newOffset = true;
                        startedGrindingAt = millis();
                    }
                    grinderToggle();
                    grindLogMark("grinder_on");
                    continue;
                }
                break;
            }
            case STATUS_GRINDING_IN_PROGRESS: {
                // Keep the display awake, otherwise the sleep timer resets the status mid-grind
                lastActivityAt = millis();
                if (!scaleReady) {
                    abortGrinding("Scale error");
                    continue;
                }
                if (scaleMode && startedGrindingAt == 0 && scaleWeight - cupWeightEmpty >= 0.1) {
                    startedGrindingAt = millis();
                    continue;
                }
                if (millis() - startedGrindingAt > MAX_GRINDING_TIME && !scaleMode) {
                    abortGrinding("Timeout");
                    continue;
                }
                if (millis() - startedGrindingAt > NO_PROGRESS_START_DELAY &&
                    scaleWeight - weightHistory.firstValueOlderThan(millis() - NO_PROGRESS_WINDOW) < 1 &&
                    !scaleMode) {
                    abortGrinding("No progress");
                    continue;
                }
                // Use the last two readings: a new reading only arrives about every 500ms,
                // so a short time window is often empty and would report a weight of 0
                if (scaleWeight < cupWeightEmpty - CUP_DETECTION_TOLERANCE &&
                    previousScaleWeight < cupWeightEmpty - CUP_DETECTION_TOLERANCE && !scaleMode) {
                    abortGrinding("Cup removed");
                    continue;
                }
                double currentOffset = offset;
                if (scaleMode) {
                    currentOffset = 0;
                }
                double targetWeight = cupWeightEmpty + setWeight + currentOffset;
                // Stop only on a plausible reading: a small step from the previous reading,
                // or two readings in a row at the target (ignores single vibration spikes)
                if (scaleWeight >= targetWeight &&
                    (scaleWeight - previousScaleWeight < MAX_PLAUSIBLE_WEIGHT_JUMP || previousScaleWeight >= targetWeight)) {
                    finishedGrindingAt = millis();
                    grinderToggle(); // the grinder stops here, the dose is only confirmed in the next state
                    scaleStatus = STATUS_GRINDING_VERIFYING;
                    grindLogMark("grinder_off w=%.2f target=%.2f", scaleWeight, targetWeight);
                    continue;
                }
                break;
            }
            case STATUS_GRINDING_VERIFYING: {
                // The grinder is off, the last grounds are still landing. Keep the display awake until
                // the dose is confirmed, otherwise the sleep timer would leave this state
                lastActivityAt = millis();
                // Window of 1s so it always contains readings (an empty window would average to 0)
                double currentWeight = weightHistory.averageSince((int64_t)millis() - 1000);
                if (scaleWeight < 5) {
                    startedGrindingAt = 0;
                    scaleStatus = STATUS_EMPTY; // the cup was taken before the dose could be confirmed
                    grindLogEnd("unverified", "why=\"cup removed\"");
                    continue;
                }
                // The dose counts as reached once the reading has settled; after FINISHED_MAX_WAIT it is
                // taken anyway, so a restless scale still finishes the grind
                if (millis() - finishedGrindingAt > FINISHED_MIN_WAIT &&
                    (weightHistory.isSteady(STEADY_READINGS, STEADY_TOLERANCE) ||
                     millis() - finishedGrindingAt > FINISHED_MAX_WAIT)) {
                    if (newOffset) {
                        double usedOffset = offset;
                        // Correct only a part of the deviation: the offset adds up over the grinds, so it still
                        // reaches the right value, but a single bad reading does not swing it around
                        offset += OFFSET_CORRECTION * (setWeight + cupWeightEmpty - currentWeight);
                        offset = constrain(offset, OFFSET_MIN, OFFSET_MAX);
                        shotCount++;
                        addGrindRecord(shotCount, (finishedGrindingAt - startedGrindingAt) / 1000.0, usedOffset,
                                       setWeight, currentWeight - cupWeightEmpty);
                        preferences.begin("scale", false);
                        preferences.putDouble("offset", offset);
                        preferences.putUInt("shotCount", shotCount);
                        preferences.putBytes("grindHist", grindHistory, sizeof(grindHistory));
                        preferences.putInt("grindHistN", grindHistoryCount);
                        preferences.end();
                        newOffset = false;
                    }
                    // A second of readings is still logged after this, so the log also shows
                    // how the scale settles once the dose is confirmed
                    grindLogEnd("finished", "dose=%.2f dur=%.2f offset=%.2f", currentWeight - cupWeightEmpty,
                                (finishedGrindingAt - startedGrindingAt) / 1000.0, offset);
                    scaleStatus = STATUS_GRINDING_FINISHED;
                }
                break;
            }
            case STATUS_GRINDING_FINISHED: {
                if (scaleWeight < 5) {
                    startedGrindingAt = 0;
                    scaleStatus = STATUS_EMPTY;
                    continue;
                }
                break;
            }
            case STATUS_GRINDING_FAILED: {
                // Keep the display awake, otherwise the sleep timer would leave the failed state
                // and a cup still on the scale would restart the grinder unattended
                lastActivityAt = millis();
                break;
            }
        }
        grindLogPoll(); // "r" and "s" on the serial connection start and end a recording without a grind
        rotary_loop();
        delay(50);
    }
}

// Initializes the scale hardware and settings
void setupScale() {
    rotaryEncoder.begin();
    rotaryEncoder.setup(readEncoderISR);
    rotaryEncoder.setBoundaries(-10000, 10000, true);
    rotaryEncoder.setAcceleration(100);

    loadcell.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
    pinMode(GRINDER_ACTIVE_PIN, OUTPUT);
    digitalWrite(GRINDER_ACTIVE_PIN, 0);

    preferences.begin("scale", false);
    scaleFactor = preferences.getDouble("calibration", (double)LOADCELL_SCALE_FACTOR);
    setWeight = preferences.getDouble("setWeight", (double)COFFEE_DOSE_WEIGHT);
    offset = constrain(preferences.getDouble("offset", (double)COFFEE_DOSE_OFFSET), OFFSET_MIN, OFFSET_MAX);
    setCupWeight = preferences.getDouble("cup", (double)CUP_WEIGHT);
    setCupWeight2 = preferences.getDouble("cup2", (double)CUP_WEIGHT_2);
    scaleMode = preferences.getBool("scaleMode", false);
    grindMode = preferences.getBool("grindMode", true);
    shotCount = preferences.getUInt("shotCount", SHOT_COUNT_DEFAULT);
    sleepTime = constrain(preferences.getInt("sleepTime", SLEEP_AFTER_MS), 5000, 600000);
    if (preferences.getBytesLength("grindHist") == sizeof(grindHistory)) {
        preferences.getBytes("grindHist", grindHistory, sizeof(grindHistory));
        grindHistoryCount = constrain(preferences.getInt("grindHistN", 0), 0, GRIND_HISTORY_SIZE);
    }
    preferences.end();

    loadcell.set_scale(scaleFactor);

    xTaskCreatePinnedToCore(updateScale, "Scale", 10000, NULL, 0, &ScaleTask, 1);
    xTaskCreatePinnedToCore(scaleStatusLoop, "ScaleStatus", 10000, NULL, 0, &ScaleStatusTask, 1);
}
