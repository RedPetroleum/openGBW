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
};
#define GRIND_HISTORY_SIZE 10 // number of grinds kept in the Weight History
#define GRIND_HISTORY_ROWS 5  // number of grinds visible at once in the Weight History

// Debug mode toggle
#define DEBUG_MODE true;
extern bool debugMode;

//Set your sleep variable
#define SLEEP_AFTER_MS 60000 // default time without activity until the display turns off, adjustable in the menu

//Main Variables and Pins
#define STATUS_EMPTY 0
#define STATUS_GRINDING_IN_PROGRESS 1
#define STATUS_GRINDING_FINISHED 2
#define STATUS_GRINDING_FAILED 3
#define STATUS_IN_MENU 4
#define STATUS_IN_SUBMENU 5
#define STATUS_INFO_MENU 8
#define STATUS_GAME 9

#define CUP_WEIGHT 396.1 //war 292
#define CUP_WEIGHT_2 76.3 // second cup
#define CUP_DETECTION_TOLERANCE 10 // 5 grams tolerance above or bellow cup weight to detect it

#define LOADCELL_DOUT_PIN 19
#define LOADCELL_SCK_PIN 18

#define LOADCELL_SCALE_FACTOR 1760 // war 7207, davor 7351

#define TARE_MEASURES 20 // use the average of measure for taring
#define SIGNIFICANT_WEIGHT_CHANGE 10 // 5 grams changes are used to detect a significant change
#define WAKE_WEIGHT_CHANGE 1.0 // a change of this many grams between two readings (e.g. tapping the scale) counts as activity
#define WAKE_IGNORE_AFTER_TARE_MS 3000 // readings settle this long after taring, their changes do not count as activity
#define MAX_PLAUSIBLE_WEIGHT_JUMP 3 // larger jumps between two readings are treated as spikes when stopping the grinder
#define COFFEE_DOSE_WEIGHT 17.5 //war 18
#define COFFEE_DOSE_OFFSET -1.67 //war -2.5
#define MAX_GRINDING_TIME 60000 // 60 seconds (war 40, davor 20)
#define SHOT_COUNT_DEFAULT 299 // start value of the shot counter (used on first start and on reset)
#define NO_PROGRESS_START_DELAY 10000 // "no progress" abort is only checked this long (ms) after grinding started

#define GRINDER_ACTIVE_PIN 25 // war 33

#define TARE_MIN_INTERVAL 5 * 1000 // auto-tare at most once every 5 seconds
#define TARE_MAX_WEIGHT 30 // readings up to this many grams are tared away automatically
#define TARE_STEADY_TOLERANCE 0.5 // ... but only when the reading has been this steady for the last 10 seconds,
                                  // so nothing is tared away while a cup is being placed on the scale

#define ROTARY_ENCODER_A_PIN 32
#define ROTARY_ENCODER_B_PIN 23
#define ROTARY_ENCODER_BUTTON_PIN 34
#define ROTARY_ENCODER_VCC_PIN -1
#define ROTARY_ENCODER_STEPS 4

// External User Variables
extern volatile bool displayLock; // Add this declaration
extern double scaleWeight;
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
extern MathBuffer<double, 100> weightHistory;
extern GrindRecord grindHistory[GRIND_HISTORY_SIZE];
extern int grindHistoryCount;
extern int grindHistoryScroll; 