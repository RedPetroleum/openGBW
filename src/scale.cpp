#include "config.hpp"
#include "grindlog.hpp"
#include "rotary.hpp"
#include "scale.hpp"

// Variables for scale functionality
double scaleWeight = 0;       // Current weight measured by the scale
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

// Task to continuously update the scale readings
void updateScale(void *parameter) {
    float lastEstimate;
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
            // Single readings without the (lagging) Kalman estimate: the game needs the laser to react
            // quickly when the scale is pressed, paging through the Weight History the same, and the
            // Weight Chart shows what the load cell really delivers
            bool fastReadings = scaleStatus == STATUS_GAME || currentSetting == GRIND_HISTORY_SETTING ||
                                currentSetting == WEIGHT_CHART_SETTING;
            // The readings are taken one by one instead of through get_units(n), which averages them inside
            // the library: the grind log needs every single one of them, unfiltered and at the full 10 Hz of
            // the HX711. Their average is the same value get_units(n) would have returned
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
            }
            if (taken == 0) {
                Serial.println("HX711 stopped answering.");
                scaleReady = false;
                continue;
            }
            float reading = sum / taken;
            lastEstimate = kalmanFilter.updateEstimate(reading);
            previousScaleWeight = scaleWeight;
            scaleWeight = fastReadings ? reading : lastEstimate;
            scaleLastUpdatedAt = millis();
            weightHistory.push(scaleWeight);
            scaleReady = true;
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
