#pragma once

#include <SimpleKalmanFilter.h>
#include "HX711.h"
#include <MathBuffer.h>
#include <AiEsp32RotaryEncoder.h>
#include <Preferences.h>
#include <MathBuffer.h>
#include <SPI.h>
#include <U8g2lib.h>

// Declarations of global variables (no memory allocation here)
extern Preferences preferences;       // Preferences object
extern HX711 loadcell;                // HX711 load cell object
extern SimpleKalmanFilter kalmanFilter; // Kalman filter for smoothing weight measurements
extern SimpleKalmanFilter kalmanV01;   // the one behind filter v01, set softer

extern TaskHandle_t ScaleTask;        // Task handle for the scale task
extern TaskHandle_t ScaleStatusTask;  // Task handle for the scale status task

class MenuItem
{
    public:
        int id;
        bool selected;
        char menuName[16];
        double increment;
        double *value;
};
// One finished grind, stored for the Weight History in the Debug Menu
struct GrindRecord
{
    uint32_t shot;   // shot count after this grind
    float duration;  // grinding time in seconds
    float offset;    // offset used for this grind in grams
    float target;    // target weight of this grind in grams
    float actual;    // weight actually ground in grams, without the cup
};
#define GRIND_HISTORY_SIZE 10 // number of grinds kept in the Weight History
#define GRIND_HISTORY_ROWS 5  // number of grinds visible at once in the Weight History
#define WEIGHT_CHART_SETTING 12 // currentSetting while the Weight Chart is shown
#define WEIGHT_HISTORY_SIZE 128 // kept readings, one per pixel column of the Weight Chart
#define GRIND_HISTORY_SETTING 13 // currentSetting while the Weight History is shown
#define GRIND_HISTORY_PAGES 2 // column pages of the Weight History: time/offset and target/actual/difference

// The Weight History is too wide for the display, so the scale itself turns the pages:
// pressing it down shows the next page, pulling it up goes back
#define HISTORY_PAGE_PRESS 50 // grams of press or pull that turn a page, a light touch is enough
#define HISTORY_PAGE_RELEASE 12 // the scale has to come back within this many grams before the next page turn
#define HISTORY_BASELINE_FOLLOW 4.0f // how fast the zero point follows scale drift while it is not pressed (1/s)

// Debug mode toggle
#define DEBUG_MODE true;
extern bool debugMode;

//Set your sleep variable
#define SLEEP_AFTER_MS 60000 // default time without activity until the display turns off, adjustable in the menu

//Main Variables and Pins
#define STATUS_EMPTY 0
#define STATUS_GRINDING_IN_PROGRESS 1
#define STATUS_GRINDING_FINISHED 2
#define STATUS_GRINDING_VERIFYING 6 // grinder already off, waiting for the reading to settle
#define STATUS_GRINDING_FAILED 3
#define STATUS_IN_MENU 4
#define STATUS_IN_SUBMENU 5
#define STATUS_INFO_MENU 8
#define STATUS_GAME 9

// Filter v01: for every reading a straight line through the longest window of the last readings that
// the line still fits within FILTER_V01_TOLERANCE, read off at the newest reading, and a Kalman filter
// behind it. Where the weight rests a long window fits and the value is quiet, where grounds land in
// clumps only a short one does and it follows at once - and unlike an average a straight line does not
// trail a rising weight however long its window is. Developed and fitted on recorded grinds, see
// tools/plotgrind.py and tools/trainfilter.py.
// Set to 0 for the old filter: five readings averaged into one, that one through the Kalman filter
#define FILTER_V01 1
#define FILTER_V01_LONGEST 40     // readings the window may grow to
#define FILTER_V01_SHORTEST 2     // ... and never falls below
#define FILTER_V01_TOLERANCE 0.15 // g, how far the readings may sit off the line (RMS)
#define FILTER_V01_JUMP 1.0       // g, a difference this large is a step: taken over, window emptied
#define FILTER_V01_KALMAN_ERROR 0.02 // measurement and estimate error of the Kalman filter behind it
#define FILTER_V01_KALMAN_NOISE 0.02 // ... and its process noise; only the ratio of the two does anything
#define FILTER_V01_FLAT_SLOPE 2.5 // g/s, from here on the line does not count as horizontal at all

// The shown weight steps in DISPLAY_STEP grams. A step of one is only taken when the weight is
// HYSTERESIS_GRAMS past the middle between two steps, or when HYSTERESIS_READINGS readings in a row all
// want the same step; a difference of two steps or more is taken over as it is. Where the filter has
// recognised the trend as flat, both are harder - a resting weight does not step, so what moves the
// last digit there is the reading rustling. The grinding itself uses the unrounded weight
#define DISPLAY_STEP 0.1
#define HYSTERESIS_GRAMS 0.03
#define HYSTERESIS_READINGS 3
#define HYSTERESIS_FLAT_FROM 0.9 // flatness from which the harder conditions are used
#define HYSTERESIS_GRAMS_FLAT 0.05
#define HYSTERESIS_READINGS_FLAT 6

#define CUP_WEIGHT 396.1 //war 292
#define CUP_WEIGHT_2 76.3 // second cup
#define CUP_DETECTION_TOLERANCE 10 // 5 grams tolerance above or bellow cup weight to detect it
// v01 delivers a weight for every reading of the HX711, the old filter only for every fifth one, so
// everything that counts readings instead of time has to be five times as much
#if FILTER_V01
#define STEADY_READINGS 15 // this many readings in a row ...
#else
#define STEADY_READINGS 3
#endif
#define STEADY_TOLERANCE 0.1 // ... within this many grams of each other mean the reading has settled

#define LOADCELL_DOUT_PIN 19
#define LOADCELL_SCK_PIN 18

#define LOADCELL_SCALE_FACTOR 1760 // war 7207, davor 7351

#define TARE_MEASURES 20 // use the average of measure for taring
#define SCALE_READINGS_PER_UPDATE 5 // readings of the load cell bundled into one weight; the HX711
                                    // delivers 10 per second, so the weight is updated twice a second
#define SIGNIFICANT_WEIGHT_CHANGE 10 // 5 grams changes are used to detect a significant change
#define WAKE_WEIGHT_CHANGE 1.0 // a change of this many grams between two readings (e.g. tapping the scale) counts as activity
#define WAKE_IGNORE_AFTER_TARE_MS 3000 // readings settle this long after taring, their changes do not count as activity
#if FILTER_V01
#define MAX_PLAUSIBLE_WEIGHT_JUMP 1.2 // larger jumps between two readings are treated as spikes when stopping
#else
#define MAX_PLAUSIBLE_WEIGHT_JUMP 3   // ... five times as much, because a reading is five times as far apart
#endif
#define COFFEE_DOSE_WEIGHT 17.5 //war 18
#define COFFEE_DOSE_OFFSET -1.67 //war -2.5
#define OFFSET_CORRECTION 0.7 // share of the last deviation that is corrected into the offset; the offset
                              // adds up, so a smaller share only settles slower, it does not leave an error
#define OFFSET_MIN -10.0 // the offset only ever stops the grinder earlier, so it stays between these grams
#define OFFSET_MAX 0.0
#define MAX_GRINDING_TIME 60000 // 60 seconds (war 40, davor 20)
#define SHOT_COUNT_DEFAULT 299 // start value of the shot counter (used on first start and on reset)
#define NO_PROGRESS_START_DELAY 10000 // "no progress" abort is only checked this long (ms) after grinding started
#define NO_PROGRESS_WINDOW 7000 // ... and only when less than 1g was ground within this window (ms)
#define FINISHED_MIN_WAIT 1500 // wait at least this long (ms) after the grinder stopped before measuring the dose
#define FINISHED_MAX_WAIT 6000 // ... and at most this long if the reading never settles

#define GRINDER_ACTIVE_PIN 25 // war 33

#define TARE_MIN_INTERVAL 5 * 1000 // auto-tare at most once every 5 seconds
#define TARE_MAX_WEIGHT 30 // readings up to this many grams are tared away automatically
#define TARE_STEADY_TOLERANCE 0.5 // ... but only when the reading has been this steady for the last 10 seconds,
                                  // so nothing is tared away while a cup is being placed on the scale
#define TARE_MAX_SPREAD 2.0 // a tare whose readings are further apart than this many grams is discarded:
                            // something was put on the scale while taring, and half of it would become the new zero.
                            // Generous on purpose: a cup spreads the readings by tens of grams, while a noisy
                            // load cell must not have every tare rejected

#define ROTARY_ENCODER_A_PIN 32
#define ROTARY_ENCODER_B_PIN 23
#define ROTARY_ENCODER_BUTTON_PIN 34
#define ROTARY_ENCODER_VCC_PIN -1
#define ROTARY_ENCODER_STEPS 4

// External User Variables
extern volatile bool displayLock; // Add this declaration
extern double scaleWeight;
extern double shownWeight; // scaleWeight in steps of DISPLAY_STEP with the hysteresis, for the display
extern unsigned long scaleLastUpdatedAt;
extern unsigned long lastActivityAt; // last scale change or knob use, the display sleeps sleepTime after it
extern unsigned long lastTareAt;
extern bool scaleReady;
extern int scaleStatus;
extern double cupWeightEmpty;
extern unsigned long startedGrindingAt;
extern unsigned long finishedGrindingAt;
extern double setWeight;
extern double offset;
extern bool scaleMode;
extern bool grindMode;
extern bool greset;
extern int menuItemsCount;
extern double setCupWeight;
extern const char *grindFailReason;
extern double setCupWeight2;
extern double scaleFactor;
extern MenuItem menuItems[];
extern int currentMenuItem;
extern int currentSetting;
extern int sleepTime;
extern unsigned int shotCount;
extern int debugMenuItemsCount;
extern int currentDebugMenuItem;
extern MathBuffer<double, WEIGHT_HISTORY_SIZE> weightHistory;
extern GrindRecord grindHistory[GRIND_HISTORY_SIZE];
extern int grindHistoryCount;
extern int grindHistoryScroll;
extern int grindHistoryPage; 