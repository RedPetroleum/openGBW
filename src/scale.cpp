#include "config.hpp"
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
MathBuffer<double, 100> weightHistory;

// Last finished grinds, newest first
GrindRecord grindHistory[GRIND_HISTORY_SIZE];
int grindHistoryCount = 0;

// Timing and status variables
unsigned long scaleLastUpdatedAt = 0;  // Timestamp of the last scale update
unsigned long lastSignificantWeightChangeAt = 0; // Timestamp of the last significant weight change
unsigned long lastTareAt = 0; // Timestamp of the last tare operation
bool scaleReady = false;      // Indicates if the scale is ready to measure
int scaleStatus = STATUS_EMPTY; // Current status of the scale
double cupWeightEmpty = 0;    // Measured weight of the empty cup
unsigned long startedGrindingAt = 0;  // Timestamp of when grinding started
unsigned long finishedGrindingAt = 0; // Timestamp of when grinding finished
bool greset = false;          // Flag for reset operation
bool newOffset = false;       // Indicates if a new offset value is pending
const char *grindFailReason = ""; // Why the last grind was aborted, shown on the display

// Tares the scale (sets the current weight to zero)
void tareScale() {
    Serial.println("Taring scale");
    loadcell.tare(TARE_MEASURES);
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
        }
        if (loadcell.wait_ready_timeout(300)) {
            lastEstimate = kalmanFilter.updateEstimate(loadcell.get_units(5));
            previousScaleWeight = scaleWeight;
            scaleWeight = lastEstimate;
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
void addGrindRecord(uint32_t shot, float duration, float usedOffset) {
    for (int i = GRIND_HISTORY_SIZE - 1; i > 0; i--) {
        grindHistory[i] = grindHistory[i - 1];
    }
    grindHistory[0] = {shot, duration, usedOffset};
    if (grindHistoryCount < GRIND_HISTORY_SIZE) {
        grindHistoryCount++;
    }
}

// Stops the grinder and switches to the failed state, which is left by pressing the knob
void abortGrinding(const char *reason) {
    grinderToggle();
    grindFailReason = reason;
    scaleStatus = STATUS_GRINDING_FAILED;
    Serial.print("Grinding failed: ");
    Serial.println(reason);
}

// Checks if the given cup has been resting on the scale for the last second
bool isCupDetected(double cupWeight) {
    return ABS(weightHistory.minSince((int64_t)millis() - 1000) - cupWeight) < CUP_DETECTION_TOLERANCE &&
           ABS(weightHistory.maxSince((int64_t)millis() - 1000) - cupWeight) < CUP_DETECTION_TOLERANCE;
}

// Task to manage the status of the scale
void scaleStatusLoop(void *p) {
    for (;;) {
        double tenSecAvg = weightHistory.averageSince((int64_t)millis() - 10000);
        if (ABS(tenSecAvg - scaleWeight) > SIGNIFICANT_WEIGHT_CHANGE) {
            lastSignificantWeightChangeAt = millis();
        }

        switch (scaleStatus) {
            case STATUS_EMPTY: {
                if (millis() - lastTareAt > TARE_MIN_INTERVAL && ABS(tenSecAvg) > 0.2 && tenSecAvg < 3 && scaleWeight < 3) {
                    lastTareAt = 0; // Retare if conditions are met
                }
                if (isCupDetected(setCupWeight) || isCupDetected(setCupWeight2)) {
                    // Same window as the cup detection, so it is guaranteed to contain readings
                    cupWeightEmpty = weightHistory.averageSince((int64_t)millis() - 1000);
                    scaleStatus = STATUS_GRINDING_IN_PROGRESS;
                    if (!scaleMode) {
                        newOffset = true;
                        startedGrindingAt = millis();
                    }
                    grinderToggle();
                    continue;
                }
                break;
            }
            case STATUS_GRINDING_IN_PROGRESS: {
                // Keep the display awake, otherwise the sleep timer resets the status mid-grind
                lastSignificantWeightChangeAt = millis();
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
                    scaleWeight - weightHistory.firstValueOlderThan(millis() - 2000) < 1 &&
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
                    grinderToggle();
                    scaleStatus = STATUS_GRINDING_FINISHED;
                    continue;
                }
                break;
            }
            case STATUS_GRINDING_FINISHED: {
                // Window of 1s so it always contains readings (an empty window would average to 0)
                double currentWeight = weightHistory.averageSince((int64_t)millis() - 1000);
                if (scaleWeight < 5) {
                    startedGrindingAt = 0;
                    scaleStatus = STATUS_EMPTY;
                    continue;
                } else if (currentWeight != setWeight + cupWeightEmpty && millis() - finishedGrindingAt > 1500 && newOffset) {
                    double usedOffset = offset;
                    offset += setWeight + cupWeightEmpty - currentWeight;
                    if (ABS(offset) >= setWeight) {
                        offset = COFFEE_DOSE_OFFSET;
                    }
                    shotCount++;
                    addGrindRecord(shotCount, (finishedGrindingAt - startedGrindingAt) / 1000.0, usedOffset);
                    preferences.begin("scale", false);
                    preferences.putDouble("offset", offset);
                    preferences.putUInt("shotCount", shotCount);
                    preferences.putBytes("grindHist", grindHistory, sizeof(grindHistory));
                    preferences.putInt("grindHistN", grindHistoryCount);
                    preferences.end();
                    newOffset = false;
                }
                break;
            }
            case STATUS_GRINDING_FAILED: {
                // Keep the display awake, otherwise the sleep timer would leave the failed state
                // and a cup still on the scale would restart the grinder unattended
                lastSignificantWeightChangeAt = millis();
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
    offset = preferences.getDouble("offset", (double)COFFEE_DOSE_OFFSET);
    setCupWeight = preferences.getDouble("cup", (double)CUP_WEIGHT);
    setCupWeight2 = preferences.getDouble("cup2", (double)CUP_WEIGHT_2);
    scaleMode = preferences.getBool("scaleMode", false);
    grindMode = preferences.getBool("grindMode", true);
    shotCount = preferences.getUInt("shotCount", SHOT_COUNT_DEFAULT);
    if (preferences.getBytesLength("grindHist") == sizeof(grindHistory)) {
        preferences.getBytes("grindHist", grindHistory, sizeof(grindHistory));
        grindHistoryCount = constrain(preferences.getInt("grindHistN", 0), 0, GRIND_HISTORY_SIZE);
    }
    preferences.end();

    loadcell.set_scale(scaleFactor);

    xTaskCreatePinnedToCore(updateScale, "Scale", 10000, NULL, 0, &ScaleTask, 1);
    xTaskCreatePinnedToCore(scaleStatusLoop, "ScaleStatus", 10000, NULL, 0, &ScaleStatusTask, 1);
}
