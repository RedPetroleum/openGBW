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
double deadTimeEnd = DEAD_TIME_END_DEFAULT; // s the grinder keeps delivering after the switch-off
double grindFlow = 0;         // g/s, the mass flow of the running grind, see stopping logic below
double scaleFactor = LOADCELL_SCALE_FACTOR; // Load cell calibration factor
bool scaleMode = false;       // Indicates if the scale is used in timer mode
bool grindMode = true;        // Grinder mode: impulse (false) or continuous (true, default)
int grindScreenStyle = GRIND_STYLE_DEFAULT; // How the grinding screen shows the progress
int bootScreenStyle = BOOT_STYLE_DEFAULT;   // How the initializing screen shows that the scale is getting ready
bool grinderActive = false;   // Grinder state (on/off)
unsigned int shotCount;  

// The last readings of the scale, what the Weight Data of the Debug Menu draws. Filtered, so this is
// what the weight IS
MathBuffer<double, WEIGHT_DATA_SIZE> weightData;

// The same readings unfiltered, straight from the load cell. Whether the scale is standing still is
// asked of these and not of the filtered ones: filter v01 lays a line through its window and reads it
// off at the newest reading, so it delivers a smooth value even where the readings underneath scatter
// wildly - a scale that is not at rest at all would pass a steadiness check on the filtered signal.
// The filter says what the weight is, the raw readings say whether it is moving
MathBuffer<double, WEIGHT_DATA_SIZE> rawData;

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
bool newDeadTime = false;     // Indicates the running grind still has to calibrate the dead time
double flowAtSwitchOff = 0;   // g/s, the mass flow at the moment the grinder was switched off ...
double doseAtSwitchOff = 0;   // ... and the ground weight without the cup at that moment
unsigned long verifyingFrom = 0; // from when readings count towards the dose, switch-off plus dead time
// From when the readings are averaged into the dose the finished screen shows: the oldest reading of
// the steadiness window that confirmed the dose, 0 while no grind has been confirmed
unsigned long doseVerifiedFrom = 0;
const char *grindFailReason = ""; // Why the last grind was aborted, shown on the display

// Tares the scale (sets the current weight to zero). A tare is wanted while lastTareAt is zero, and
// its readings are taken by the sampling loop in its normal rhythm - one per turn, through tareTake().
// A tare that read its TARE_MEASURES readings in one go blocked the loop for about two seconds, in
// which nothing was measured, published or logged and the display stood still. This way the weight
// keeps running from the old zero point until the new one is there.
//
// The readings are taken one by one instead of through loadcell.tare(), so that a weight arriving
// while taring can be noticed: a cup placed during those two seconds would otherwise end up in the
// new zero point with a part of its weight
static long tareSum = 0, tareLowest = 0, tareHighest = 0;
static int tareCount = 0;

// Throws away the readings of a running tare, which then starts over
static void tareReset() {
    tareSum = 0;
    tareCount = 0;
}

// One reading into the running tare; sets the new zero point once TARE_MEASURES of them are together
static void tareTake(long reading) {
    if (tareCount == 0) {
        Serial.println("retaring scale"); // the readings for it come from the sampling loop
        Serial.println("current dead time");
        Serial.println(deadTimeEnd);
    }
    if (tareCount == 0 || reading < tareLowest) {
        tareLowest = reading;
    }
    if (tareCount == 0 || reading > tareHighest) {
        tareHighest = reading;
    }
    tareSum += reading;
    tareCount++;
    if (tareCount < TARE_MEASURES) {
        return; // not enough readings yet, the next turn of the loop brings the next one
    }

    double spread = ABS((tareHighest - tareLowest) / scaleFactor);
    long average = tareSum / tareCount;
    tareReset();
    if (spread > TARE_MAX_SPREAD) {
        Serial.printf("Tare discarded, the readings are %.1fg apart\n", spread);
        // Once the scale has a zero point, the next quiet moment is awaited instead of taring right away,
        // which would zero away whatever was just placed on the scale
        lastTareAt = scaleTared ? millis() : 0;
        return;
    }

    loadcell.set_offset(average);
    scaleTared = true;
    lastTareAt = millis();
    Serial.println("Scale tared");
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

// Whether a tolerance test is running on the readings right now: the cup detection on the empty scale,
// and the dose verification from the moment the readings count towards the dose. Both of them ask
// whether the readings lie within a tolerance of each other, and the display must not wander off by a
// step while that is being decided - so it is held as tightly there as on a weight lying flat
static bool testingTolerance() {
    if (scaleStatus == STATUS_EMPTY) {
        return true; // the cup detection checks every reading against its two rules
    }
    if (scaleStatus == STATUS_GRINDING_VERIFYING) {
        return millis() >= verifyingFrom; // before that the last grounds are still landing
    }
    return false;
}

static int shownUnits = 0, shownPending = 0, shownDirection = 0, shownZeros = 0;
static bool shownStarted = false;

// Puts the display on the given weight, which has to be a whole number of steps. The micro-tare moves
// the zero point of the load cell underneath the running filters, and only it knows what the weight on
// the scale really is - the hysteresis would otherwise have to find its way onto that value on its own,
// from a step it took over readings that counted from the old zero point
static void setShownV03(double grams) {
    shownUnits = (int)lround(grams / DISPLAY_STEP);
    shownPending = shownDirection = shownZeros = 0;
    shownStarted = true;
    shownWeight = shownUnits * DISPLAY_STEP;
}

// The display of v03: it steps like the ordinary hysteresis while the weight moves, but where the
// detectors say it lies flat - or where a tolerance test is running, see testingTolerance() - a single
// step also needs v02 to land on that same step. And a shown value within ZERO_V03_GRAMS of zero for
// ZERO_V03_READINGS readings in a row is shown as a plain zero, and a small negative value is held at
// zero by holdNegative() - the display only, the weight behind it is untouched and nothing is tared
static void updateShownV03(double value, double other, double softFlat, bool flat) {
    int &units = shownUnits, &pending = shownPending, &direction = shownDirection, &zeros = shownZeros;

    int wanted = (int)lround(value / DISPLAY_STEP);
    if (!shownStarted) {
        units = wanted;
        shownStarted = true;
    }
    bool strict = softFlat >= HYSTERESIS_FLAT_FROM;
    double extra = strict ? HYSTERESIS_GRAMS_FLAT : HYSTERESIS_GRAMS;
    int confirm = strict ? HYSTERESIS_READINGS_FLAT : HYSTERESIS_READINGS;
    bool held = flat || testingTolerance(); // the two cases in which v02 has to agree to a single step

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
            if (!held) {
                units += delta; // the weight is moving, the display has to follow
                pending = direction = 0;
            } else if ((int)lround(other / DISPLAY_STEP) == units + delta) {
                units += delta; // held back and v02 agrees: the step is real
                pending = direction = 0;
            }
            // Held back and v02 still on the old step: no step, and `pending` is kept so it is made
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
    weightData.push(scaleWeight);
    scaleReady = true;
}

// Task to continuously update the scale readings
void updateScale(void *parameter) {
    for (;;) {
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
                    tareReset(); // a running tare starts over rather than average across the gap
                    break; // the chip stopped answering mid-bundle, the readings so far still give a weight
                }
                long raw = loadcell.read();
                if (lastTareAt == 0) {
                    tareTake(raw); // a tare is running, this reading is one of the ones it needs
                }
                if (!scaleTared) {
                    continue; // the scale has no zero point yet, the reading says nothing about a weight
                }
                double grams = (raw - loadcell.get_offset()) / (double)loadcell.get_scale();
                grindLogSample(raw, grams);
                rawData.push(grams); // unfiltered, for the steadiness checks
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
                    weightData.push(scaleWeight);
                    scaleReady = true;
#else
                    publishWeight(filtered, flatness > 0 ? flatness : 0);
#endif
                }
#endif
            }
            if (taken == 0) {
                if (!scaleTared) {
                    continue; // nothing to report, the readings went into the first tare
                }
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
            tareReset(); // a running tare starts over rather than average across the gap
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
void addGrindRecord(uint32_t shot, float duration, float deadTime, float flow, float target, float actual) {
    for (int i = GRIND_HISTORY_SIZE - 1; i > 0; i--) {
        grindHistory[i] = grindHistory[i - 1];
    }
    grindHistory[0] = {shot, duration, deadTime, flow, target, actual};
    if (grindHistoryCount < GRIND_HISTORY_SIZE) {
        grindHistoryCount++;
    }
}

// ---------------------------------------------------------------------------------------------------
// The mass flow the grind is stopped by, see config.hpp

// Straight line through the readings of the last FLOW_WINDOW seconds, never reaching back further than
// the first grounds can have arrived - before that the cup was only resting, and those readings would
// pull the line flat. Writes its slope in grams per second and its value at `now`, the weight the line
// says is on the scale at this moment; false while the window holds too few readings for a line
static bool windowLine(unsigned long now, double *slope, double *fitted) {
    int64_t from = (int64_t)now - (int64_t)(FLOW_WINDOW * 1000);
    int64_t firstGrounds = (int64_t)startedGrindingAt + (int64_t)(FLOW_START_DEAD_TIME * 1000);
    if (from < firstGrounds) {
        from = firstGrounds;
    }
    int count = 0;
    double sumTime = 0, sumValue = 0, sumSquares = 0, sumMixed = 0;
    // Seconds counted backwards from now, so the sums stay small numbers however long the board has run
    weightData.executeOnSamplesSince(from, [&](double value, int64_t at) {
        double t = (at - (int64_t)now) / 1000.0;
        count++;
        sumTime += t;
        sumValue += value;
        sumSquares += t * t;
        sumMixed += t * value;
    });
    if (count < FLOW_WINDOW_MIN_READINGS) {
        return false;
    }
    double meanTime = sumTime / count, meanValue = sumValue / count;
    double spread = sumSquares - sumTime * meanTime;
    *slope = spread > 0 ? (sumMixed - sumTime * meanValue) / spread : 0;
    *fitted = meanValue - *slope * meanTime; // the line read off at now, which sits at t = 0
    return true;
}

// The dose in the cup and the mass flow of the running grind, both taken off that line wherever there
// is one. The cup sits in every reading of the window as the same constant, so it only has to be taken
// off the value, not off the slope. Over the first FLOW_EARLY_UNTIL seconds the line is still too short
// for a slope worth trusting, so the flow is the dose divided by the running time less the dead time at
// the front; the dose itself already comes from the line
static void grindState(unsigned long now, double *dose, double *flow) {
    double slope = 0, fitted = 0;
    bool line = windowLine(now, &slope, &fitted);
    *dose = (line ? fitted : scaleWeight) - cupWeightEmpty;

    double running = (now - startedGrindingAt) / 1000.0;
    if (running >= FLOW_EARLY_UNTIL) {
        *flow = line ? slope : 0;
        return;
    }
    double since = running - FLOW_START_DEAD_TIME;
    // Nothing has reached the scale yet, or so little of it that the division runs away
    *flow = since >= FLOW_EARLY_MIN_RUN && *dose > 0 ? *dose / since : 0;
}

// The line the switch-off is calculated from: which reading it was built on, when it was read off and
// what it said there, plus the moment the grinder has to go off. All of it is renewed with every new
// reading; between two readings the clock alone decides, see the stop logic in the status loop
static unsigned long stopLineFromReading = 0;
static unsigned long stopLineAt = 0;
static double stopLineDose = 0;
static unsigned long switchOffAt = 0; // 0 while no switch-off is scheduled

// What the switch-off of the last grind tells about the dead time: the grounds that arrived after it,
// divided by the flow at that moment. Corrects deadTimeEnd towards it by DEAD_TIME_CORRECTION
static void calibrateDeadTime(double finalDose) {
    if (flowAtSwitchOff < DEAD_TIME_MIN_FLOW) {
        return; // too slow to divide by, the grind says nothing about the dead time and it is left alone
    }
    double measured = constrain((finalDose - doseAtSwitchOff) / flowAtSwitchOff, DEAD_TIME_MIN, DEAD_TIME_MAX);
    deadTimeEnd = constrain(deadTimeEnd + DEAD_TIME_CORRECTION * (measured - deadTimeEnd),
                            DEAD_TIME_MIN, DEAD_TIME_MAX);
}

// Switches to the failed state, which is left by pressing the knob. The grinder is only toggled where
// it can still be running: from the verifying phase on it is already off, and toggling it there would
// start it again - in continuous mode it would stay on, in impulse mode it would get its starting pulse
static void failGrinding(const char *reason, bool stopGrinder) {
    if (stopGrinder) {
        grinderToggle();
    }
    grindFailReason = reason;
    scaleStatus = STATUS_GRINDING_FAILED;
    grindLogEnd("aborted", "why=\"%s\"", reason);
    Serial.print("Grinding failed: ");
    Serial.println(reason);
}

// Stops the grinder and switches to the failed state
void abortGrinding(const char *reason) {
    failGrinding(reason, true);
}

// One of the two steadiness rules on the raw readings: the last `readings` of them within `tolerance`.
// Writes their lowest and highest, which the cup detection checks against the cup weight as well
static bool rawSteady(size_t readings, double tolerance, double &lowest, double &highest) {
    return rawData.spreadOfLast(readings, lowest, highest) && highest - lowest <= tolerance;
}

// Which of the two rules found the scale standing still, as the number of raw readings it looked at -
// 0 if neither did. On top of the rule, at least that many readings have to have been taken after
// `since` - for the dose, where everything from before the dead time says nothing; `since` of 0 asks
// nothing of the kind. See config.hpp for the two rules
static size_t steadyOver(unsigned long since) {
    double lowest = 0, highest = 0;
    size_t usable = since == 0 ? rawData.capacity : rawData.countSamplesSince(since);
    if (usable >= STEADY_READINGS_SHORT &&
        rawSteady(STEADY_READINGS_SHORT, STEADY_TOLERANCE_SHORT, lowest, highest)) {
        return STEADY_READINGS_SHORT;
    }
    if (usable >= STEADY_READINGS_LONG &&
        rawSteady(STEADY_READINGS_LONG, STEADY_TOLERANCE_LONG, lowest, highest)) {
        return STEADY_READINGS_LONG;
    }
    return 0;
}

// The same two rules with the cup weight on top: the readings stand still and all of them lie around
// the given cup weight, so the cup is standing on the scale and its empty weight can be taken. Both
// ends are checked against the cup weight, everything else lies between them
static bool cupResting(double cupWeight, size_t readings, double tolerance) {
    double lowest = 0, highest = 0;
    return rawSteady(readings, tolerance, lowest, highest) &&
           ABS(lowest - cupWeight) < CUP_DETECTION_TOLERANCE && ABS(highest - cupWeight) < CUP_DETECTION_TOLERANCE;
}

// Which of the two rules recognised the given cup, as the number of readings it looked at - 0 if
// neither did. The caller needs that number for the micro-tare: it is exactly those readings that say
// what the cup weighs, and only they are known to lie within a tolerance of each other
static size_t cupDetectedOver(double cupWeight) {
    if (cupResting(cupWeight, STEADY_READINGS_SHORT, STEADY_TOLERANCE_SHORT)) {
        return STEADY_READINGS_SHORT;
    }
    if (cupResting(cupWeight, STEADY_READINGS_LONG, STEADY_TOLERANCE_LONG)) {
        return STEADY_READINGS_LONG;
    }
    return 0;
}

// Checks if the given cup is resting on the scale, either rule is enough
bool isCupDetected(double cupWeight) {
    return cupDetectedOver(cupWeight) > 0;
}

// The dose the finished screen shows: the average of the raw readings since the steadiness window that
// confirmed it began. Every reading that arrives afterwards goes into it, so the value only gets
// quieter as long as the cup stands still. Falls back to the displayed weight where no grind has been
// confirmed
double verifiedDose() {
    if (doseVerifiedFrom == 0 || rawData.countSamplesSince(doseVerifiedFrom) == 0) {
        return shownWeight - cupWeightEmpty;
    }
    return rawData.averageSince(doseVerifiedFrom) - cupWeightEmpty;
}

// Moves the zero point of the load cell by `grams`. This is not a tare of its own: nothing is measured
// here, the correction comes from readings that have already been taken, and it is a twentieth of a
// gram at most
static void microTare(double grams) {
    loadcell.set_offset(loadcell.get_offset() + lround(grams * (double)loadcell.get_scale()));
}

// Task to manage the status of the scale
void scaleStatusLoop(void *p) {
    for (;;) {
        double tenSecAvg = weightData.averageSince((int64_t)millis() - 10000);
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
                size_t cupOver = cupDetectedOver(setCupWeight);
                if (cupOver == 0) {
                    cupOver = cupDetectedOver(setCupWeight2);
                }
                if (cupOver > 0) {
                    // Micro-tare. The readings that recognised the cup say what it really weighs, and
                    // their average almost never sits on a whole DISPLAY_STEP - 76.34 g, say. The zero
                    // point is moved by those 0.04 g, so the step becomes the truth instead of a
                    // rounding of it, and the display is put on it: it rounds the value of v01 and the
                    // hysteresis holds that where it is, which is up to a step away from the average of
                    // the raw readings the cup weight comes from. Everything from here on counts from a
                    // cup that weighs exactly what is displayed
                    double resting = rawData.taperedAverageOfLast(cupOver, VERIFY_WEIGHT_OLDEST,
                                                                 VERIFY_WEIGHT_SECOND);
                    cupWeightEmpty = lround(resting / DISPLAY_STEP) * DISPLAY_STEP;
                    microTare(resting - cupWeightEmpty);
#if FILTER == FILTER_V03
                    setShownV03(cupWeightEmpty); // the display shows the cup the dose counts from
#endif
                    scaleStatus = STATUS_GRINDING_IN_PROGRESS;
                    grindLogBegin(); // from here on every reading of the load cell is logged
                    grindLogMark("microtare cup=%.2f was=%.3f", cupWeightEmpty, resting);
                    if (!scaleMode) {
                        newDeadTime = true;
                        startedGrindingAt = millis();
                    }
                    grindFlow = 0;
                    flowAtSwitchOff = 0;
                    doseAtSwitchOff = 0;
                    doseVerifiedFrom = 0;
                    stopLineFromReading = stopLineAt = switchOffAt = 0;
                    stopLineDose = 0;
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
                    scaleWeight - weightData.firstValueOlderThan(millis() - NO_PROGRESS_WINDOW) < 1 &&
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
                // The dose and how fast it is growing, both read off the line through the last readings
                // - a single vibration spike moves that line by a fraction of what it moves the reading
                // itself. The line only changes when a new reading has arrived, so it is renewed with
                // the reading and not with every turn of the loop
                if (scaleLastUpdatedAt != stopLineFromReading) {
                    stopLineFromReading = scaleLastUpdatedAt;
                    stopLineAt = millis();
                    grindState(stopLineAt, &stopLineDose, &grindFlow);
                    // What the grinder will still deliver during its dead time is counted in. In scale
                    // mode there is no grinder to switch off, so there the target is simply reached
                    double lead = scaleMode ? 0 : grindFlow * deadTimeEnd;
                    double missing = setWeight - stopLineDose - lead; // grams left before the switch-off
                    if (missing <= 0) {
                        switchOffAt = stopLineAt; // the moment has already passed, off at once
                    } else if (grindFlow > 0 && missing / grindFlow <= STOP_LOOKAHEAD) {
                        // At this flow the line needs this long to carry the dose over the target
                        switchOffAt = stopLineAt + (unsigned long)(missing / grindFlow * 1000);
                    } else {
                        switchOffAt = 0; // not growing, or too far out to say
                    }
                }
                // The calculated moment lies between two readings. The turn of the loop that reaches it
                // sleeps the last few milliseconds up to it, so the grinder goes off on that
                // millisecond and not on whichever reading happens to arrive next
                if (switchOffAt != 0 && (long)(switchOffAt - millis()) < STATUS_POLL_MS) {
                    long remaining = (long)(switchOffAt - millis());
                    if (remaining > 0) {
                        delay(remaining);
                    }
                    finishedGrindingAt = millis();
                    flowAtSwitchOff = grindFlow;
                    // The line read off at the moment it is really switched off, which is what the
                    // dead time is measured against once the dose has settled
                    doseAtSwitchOff = stopLineDose + grindFlow * (long)(finishedGrindingAt - stopLineAt) / 1000.0;
                    // The last grounds are still on their way; only from here on does a reading say
                    // anything about where the dose ends up
                    verifyingFrom = finishedGrindingAt + (unsigned long)(scaleMode ? 0 : deadTimeEnd * 1000);
                    grinderToggle(); // the grinder stops here, the dose is only confirmed in the next state
                    scaleStatus = STATUS_GRINDING_VERIFYING;
                    grindLogMark("grinder_off w=%.2f dose=%.2f flow=%.2f dead=%.2f late=%ld", scaleWeight,
                                 doseAtSwitchOff, grindFlow, deadTimeEnd,
                                 (long)(finishedGrindingAt - switchOffAt));
                    continue;
                }
                break;
            }
            case STATUS_GRINDING_VERIFYING: {
                // The grinder is off, the last grounds are still landing. Keep the display awake until
                // the dose is confirmed, otherwise the sleep timer would leave this state
                lastActivityAt = millis();
                // Window of 1s so it always contains readings (an empty window would average to 0)
                double currentWeight = weightData.taperedAverageSince((int64_t)millis() - 1000,
                                                                      VERIFY_WEIGHT_OLDEST, VERIFY_WEIGHT_SECOND);
                if (scaleWeight < 5) {
                    startedGrindingAt = 0;
                    scaleStatus = STATUS_EMPTY; // the cup was taken before the dose could be confirmed
                    grindLogEnd("unverified", "why=\"cup removed\"");
                    continue;
                }
                // The dead time first has to run out: until then the last grounds are still landing and
                // no reading says anything about where the dose ends up. Only from verifyingFrom on do
                // they count
                if (millis() < verifyingFrom) {
                    break;
                }
                double dose = currentWeight - cupWeightEmpty;
                // The dose is reached once the raw readings stand still by the same two rules the cup
                // detection uses - and the value they settled on is a plausible dose. Steadiness alone
                // is not enough: a cup put down again or a hand resting on the scale is just as steady,
                // and it must not be taken for the dose
                size_t settledOver = steadyOver(verifyingFrom);
                bool settled = settledOver > 0;
                bool plausible = ABS(dose - setWeight) <= DOSE_PLAUSIBLE_GRAMS;
                if (!settled || !plausible) {
                    // Whatever is on the scale after FINISHED_MAX_WAIT is not a dose this grind can
                    // answer for, so it is not counted and the dead time is not calibrated from it
                    if (millis() - verifyingFrom > FINISHED_MAX_WAIT) {
                        failGrinding(settled ? "Dose off target" : "Scale unsteady", false);
                    }
                    break;
                }
                double usedDeadTime = deadTimeEnd;
                if (newDeadTime) {
                    // What still arrived after the switch-off says how long the dead time really was;
                    // only a part of the deviation goes into it, see config.hpp
                    calibrateDeadTime(dose);
                    shotCount++;
                    addGrindRecord(shotCount, (finishedGrindingAt - startedGrindingAt) / 1000.0,
                                   usedDeadTime, flowAtSwitchOff, setWeight, dose);
                    preferences.begin("scale", false);
                    preferences.putDouble("deadtime", deadTimeEnd);
                    preferences.putUInt("shotCount", shotCount);
                    preferences.putBytes("grindHist", grindHistory, sizeof(grindHistory));
                    preferences.putInt("grindHistN", grindHistoryCount);
                    preferences.end();
                    newDeadTime = false;
                }
                // A second of readings is still logged after this, so the log also shows
                // how the scale settles once the dose is confirmed
                grindLogEnd("finished", "dose=%.2f dur=%.2f flow=%.2f dead=%.2f next_dead=%.2f", dose,
                            (finishedGrindingAt - startedGrindingAt) / 1000.0, flowAtSwitchOff,
                            usedDeadTime, deadTimeEnd);
                // The readings of the accepted window are the first ones that say what the dose is,
                // the finished screen averages from there on
                doseVerifiedFrom = (unsigned long)rawData.timestampOfLast(settledOver);
                scaleStatus = STATUS_GRINDING_FINISHED;
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
        delay(STATUS_POLL_MS);
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
    deadTimeEnd = constrain(preferences.getDouble("deadtime", (double)DEAD_TIME_END_DEFAULT), DEAD_TIME_MIN, DEAD_TIME_MAX);
    setCupWeight = preferences.getDouble("cup", (double)CUP_WEIGHT);
    setCupWeight2 = preferences.getDouble("cup2", (double)CUP_WEIGHT_2);
    scaleMode = preferences.getBool("scaleMode", false);
    grindMode = preferences.getBool("grindMode", true);
    grindScreenStyle = constrain(preferences.getInt("grindStyle", GRIND_STYLE_DEFAULT), 0, GRIND_STYLE_COUNT - 1);
    bootScreenStyle = constrain(preferences.getInt("bootStyle", BOOT_STYLE_DEFAULT), 0, BOOT_STYLE_COUNT - 1);
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
