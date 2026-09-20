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
    float deadTime;  // dead time the grinder was stopped with, in seconds
    float flow;      // mass flow at the moment of the switch-off, in grams per second
    float target;    // target weight of this grind in grams
    float actual;    // weight actually ground in grams, without the cup
};
#define GRIND_HISTORY_SIZE 10 // number of grinds kept in the Weight History
#define GRIND_HISTORY_ROWS 5  // number of grinds visible at once in the Weight History
#define GRIND_HISTORY_SETTING 13 // currentSetting while the Weight History is shown
#define GRIND_HISTORY_PAGES 2 // column pages of the Weight History: time/dead time/flow and target/actual/difference
#define WEIGHT_DATA_SETTING 12 // currentSetting while the Weight Data is shown
#define STYLE_MENU_SETTING 15 // currentSetting while the Style submenu is shown
#define GRIND_SCREEN_SETTING 16 // currentSetting while the grinding screen style is chosen

// How the grinding screen shows the progress, chosen in the Style menu
#define GRIND_STYLE_BAR 0    // a bar below the weights (default)
#define GRIND_STYLE_INVERT 1 // the whole screen is inverted from the bottom up
#define GRIND_STYLE_FRAME 2  // a border grows out of the middle of the top and bottom edge around the corners
#define GRIND_STYLE_CURVE 3  // the whole grind as a curve, with the set weight and the switch-off in it
#define GRIND_STYLE_COUNT 4
#define WEIGHT_DATA_SIZE 128 // readings it keeps, one per pixel column; the grind also works on them

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
// Filter v02: the average of the last FILTER_V02_WINDOW readings through the Kalman filter, the same
// amount of averaging as the old filter and the same Kalman values - but as a moving average, so a
// weight comes out for every reading and not only for every fifth. It has no flatness of its own, so
// the display hysteresis stays on its soft level with it.
//
// FILTER picks which one the scale runs
// Filter v03: the value of v01, but the display is only allowed to step by one when the scale is not
// lying flat - and where it is, v02 has to land on that same step first. Whether it lies flat is what
// the three detectors below decide. The same holds while a tolerance test runs on the readings, flat or
// not: the cup detection on the empty scale and the dose verification after the grind both decide
// whether the readings lie within a tolerance of each other, and the display is held just as tightly
// there. All of it was developed and tried out in tools/plotgrind.py
#define FILTER_OLD 0
#define FILTER_V01 1
#define FILTER_V02 2
#define FILTER_V03 3
#define FILTER FILTER_V03
#define FILTER_FAST (FILTER != FILTER_OLD) // ... delivers a weight for every reading, ten a second
#define FILTER_V01_LONGEST 40     // readings the window may grow to
#define FILTER_V01_SHORTEST 2     // ... and never falls below
#define FILTER_V01_TOLERANCE 0.15 // g, how far the readings may sit off the line (RMS)
#define FILTER_V01_JUMP 1.0       // g, a difference this large is a step: taken over, window emptied
#define FILTER_V01_KALMAN_ERROR 0.02 // measurement and estimate error of the Kalman filter behind it
#define FILTER_V01_KALMAN_NOISE 0.02 // ... and its process noise; only the ratio of the two does anything
#define FILTER_V01_FLAT_SLOPE 2.5 // g/s, from here on the line does not count as horizontal at all
#define FILTER_V02_WINDOW 5       // readings of the moving average, the same number the old filter bundles

// The three detectors of v03, each on the raw readings and each between 0 and 1. They need to know how
// fast the weight really moves, which is the change of a moving average over FILTER_V03_AVERAGE
// readings. On the scale that change is taken backwards over two readings; the drawing in
// tools/plotgrind.py takes it centred, which no filter can do, but on the recordings both give the
// same answer in every single reading
#define FILTER_V03_WINDOW 20  // readings kept for the detectors
#define FILTER_V03_AVERAGE 15 // readings of the moving average whose change they look at

#define FLAT_V01_SHORT 5    // readings within FLAT_V01_TIGHT give FLAT_V01_FEW ...
#define FLAT_V01_LONG 20    // ... and this many of them give the full 1
#define FLAT_V01_FEW 0.8
#define FLAT_V01_TIGHT 0.7  // g
#define FLAT_V01_WIDE 1.2   // g, this spread over the short window gives FLAT_V01_LOOSE
#define FLAT_V01_LOOSE 0.5
#define FLAT_V01_QUIET 0.5  // g/s, up to this rate the value is left alone
#define FLAT_V01_MOVING 1.0 // g/s, from here the weight is moving and the value is 0

#define PLACED_V01_GRAMS 1.5 // g, a step of more than this between two readings

#define GRIND_V01_CORE_LOW 0.5   // g/s, in here the full value is possible
#define GRIND_V01_CORE_HIGH 5.5
#define GRIND_V01_WIDE_LOW 0.4   // g/s, outside of this it is nothing, in between it fades
#define GRIND_V01_WIDE_HIGH 6.0
#define GRIND_V01_SHORT 5   // readings that give GRIND_V01_FEW ...
#define GRIND_V01_LONG 20   // ... and this many give the full 1
#define GRIND_V01_FEW 0.5
#define GRIND_V01_QUIET 1.0 // g/s, below this rate a value under GRIND_V01_SURE is not grinding at all
#define GRIND_V01_SURE 0.6

#define DECIDED_V01_ALONE 0.8 // a detector this high wins when the other two are at zero

#define ZERO_V03_GRAMS 0.2    // a shown value this close to zero ...
#define ZERO_V03_READINGS 10  // ... for this many readings in a row is shown as a plain zero

// Taking a heavy weight off makes the scale swing through zero and hang in the negative for a moment,
// which has nothing to do with the cup being lighter than the zero point. A small negative value is
// therefore held at zero: it is only shown once it has reached NEGATIVE_V03_GRAMS, or once
// NEGATIVE_V03_READINGS readings in a row have all been negative. The display only, the weight behind
// it is untouched, and the moment the hold ends the true value appears at once
#define NEGATIVE_V03_GRAMS 0.5   // g below zero which is shown straight away ...
#define NEGATIVE_V03_READINGS 5  // ... or this many negative readings in a row

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
// Whether the scale is standing still. Two rules, and either of them on its own is enough:
//
//     STEADY_READINGS_SHORT readings within STEADY_TOLERANCE_SHORT   - a scale that has come to rest
//     STEADY_READINGS_LONG readings within STEADY_TOLERANCE_LONG     - one that keeps rustling a little
//
// The same two decide whether a cup is standing on the scale and whether the dose has settled after
// grinding, so both answers come from the same idea of "not moving". The difference between the two is
// only what else has to be true: for a cup the readings must also lie around the cup weight, for the
// dose they must all have been taken after the dead time.
//
// They look at the raw readings, which arrive at the full 10 Hz of the HX711 whatever the filter does -
// the filter lays a line through its window and would report a smooth value even where the readings
// underneath scatter, so a scale that is not at rest at all would pass. Spread is the difference
// between the lowest and the highest of the window, so it cannot add up over the readings: they all
// have to fit into a band of that width
#define STEADY_READINGS_SHORT 10 // one second at 10 Hz ...
#define STEADY_TOLERANCE_SHORT 0.5
#define STEADY_READINGS_LONG 20  // ... and two seconds
#define STEADY_TOLERANCE_LONG 0.7

// Once such a window has been accepted, the average over it says what the weight is - the empty cup in
// the cup detection, the dose after grinding. That window reaches back to the edge of the settled
// stretch, and its oldest readings still carry a little of what happened before it: the cup coming to
// rest, the last grounds landing. They are therefore counted with less weight, the oldest one least, so
// the weight of a reading fades in linearly over the first two instead of starting at full strength
#define VERIFY_WEIGHT_OLDEST 0.33 // a third for the oldest reading of the window ...
#define VERIFY_WEIGHT_SECOND 0.67 // ... two thirds for the one after it, all the others count fully

#define LOADCELL_DOUT_PIN 19
#define LOADCELL_SCK_PIN 18

#define LOADCELL_SCALE_FACTOR 1760 // war 7207, davor 7351

#define TARE_MEASURES 20 // use the average of measure for taring
#define SCALE_READINGS_PER_UPDATE 5 // readings of the load cell bundled into one weight; the HX711
                                    // delivers 10 per second, so the weight is updated twice a second
#define SIGNIFICANT_WEIGHT_CHANGE 10 // 5 grams changes are used to detect a significant change
#define WAKE_WEIGHT_CHANGE 1.0 // a change of this many grams between two readings (e.g. tapping the scale) counts as activity
#define WAKE_IGNORE_AFTER_TARE_MS 3000 // readings settle this long after taring, their changes do not count as activity
#define COFFEE_DOSE_WEIGHT 17.5 //war 18

// Stopping the grinder early, modelled on the mass flow instead of a fixed offset in grams.
//
// Two dead times are at work. At the front the grinder needs a moment before the first grounds reach
// the scale, at the back it keeps delivering for a moment after it has been switched off. The front
// one is a fixed assumption, the back one is what decides the dose and is therefore calibrated after
// every grind.
//
// A straight line is fitted to the readings of the last FLOW_WINDOW seconds, and both numbers the
// decision needs are read off it: x is its value at this moment and the flow m is its slope. Taking x
// off the line rather than from the reading is what keeps a vibration spike from stopping the grinder
// early - a spike moves a line through dozens of readings by a fraction of what it moves the reading.
// For the first FLOW_EARLY_UNTIL seconds the line is still too short for a slope worth trusting, so
// the flow is the ground weight divided by the running time less the dead time at the front,
// m = x / (t - FLOW_START_DEAD_TIME); x comes off the line from the start.
//
// The grinder is switched off as soon as the weight it will still deliver during its dead time carries
// the dose over the target: x + m * deadTimeEnd >= setWeight. What actually arrived afterwards tells
// how long that dead time really was, deadTimeEnd is corrected towards it, and so the dose settles in
// over a few grinds the way the offset used to.
#define FLOW_START_DEAD_TIME 0.8 // s until the first grounds reach the scale, assumed for the early flow
#define FLOW_EARLY_MIN_RUN 0.2   // s of grinding past that dead time before the early flow says anything
#define FLOW_EARLY_UNTIL 5.0     // s after the start up to which the early flow is used ...
#define FLOW_WINDOW 4.0          // ... from there the slope of a line through this many seconds of readings
#define FLOW_WINDOW_MIN_READINGS 5 // below this the window has no line, x is the plain reading

#define DEAD_TIME_END_DEFAULT 0.3 // s the grinder keeps delivering after it was switched off, start value
#define DEAD_TIME_MIN 0.0 // s, the calibrated dead time stays between these two ...
#define DEAD_TIME_MAX 2.0
#define DEAD_TIME_CORRECTION 0.4 // ... and only this share of the last deviation goes into it, so a single
                                 // odd grind does not swing it around. It adds up over the grinds, so a
                                 // smaller share only settles slower, it does not leave an error
#define DEAD_TIME_MIN_FLOW 0.3 // g/s, below this flow at the switch-off the dead time cannot be measured
                               // (the division blows a small overshoot up into seconds), the grind is skipped

// The readings come ten times a second, so switching off on the first one that lies past the target
// would be up to a tenth of a second late - at 2 g/s a fifth of a gram, and that lateness is a
// different one in every grind, which no calibration can take out. Instead the line is extrapolated to
// the moment it will cross the target and the grinder is switched off on that millisecond, between two
// readings. The last turn of the status loop before it sleeps exactly up to that moment
#define STATUS_POLL_MS 50 // ms between two turns of the scale status loop
#define STOP_LOOKAHEAD 2.0 // s, a switch-off further ahead than this is not scheduled yet: that far out
                           // the line says little, and the next reading gives a better one anyway
#define MAX_GRINDING_TIME 60000 // 60 seconds (war 40, davor 20)
#define SHOT_COUNT_DEFAULT 299 // start value of the shot counter (used on first start and on reset)
#define NO_PROGRESS_START_DELAY 10000 // "no progress" abort is only checked this long (ms) after grinding started
#define NO_PROGRESS_WINDOW 7000 // ... and only when less than 1g was ground within this window (ms)
#define FINISHED_MAX_WAIT 6000 // ms the dose has to have settled in, counted from the end of the dead
                               // time. A grind that has not delivered a steady, plausible reading by
                               // then has failed and is neither counted nor calibrated from
#define DOSE_PLAUSIBLE_GRAMS 5.0 // g, a settled reading further than this from the target is not the
                                 // dose: a cup that was moved, a hand on the scale, a reading that
                                 // settled on something else. The grinder only ever switches off near
                                 // the target, so a deviation this large never comes from the grind

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
extern double deadTimeEnd; // s the grinder keeps delivering after the switch-off, calibrated per grind
extern double grindFlow;   // g/s, the mass flow the running grind is being stopped by
extern bool scaleMode;
extern bool grindMode;
extern int grindScreenStyle; // how the grinding screen shows the progress, one of GRIND_STYLE_*
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
extern int currentStyleMenuItem; // Current selection in the Style submenu
extern MathBuffer<double, WEIGHT_DATA_SIZE> weightData;
extern MathBuffer<double, WEIGHT_DATA_SIZE> rawData; // the same readings unfiltered
extern GrindRecord grindHistory[GRIND_HISTORY_SIZE];
extern int grindHistoryCount;
extern int grindHistoryScroll;
extern int grindHistoryPage; 