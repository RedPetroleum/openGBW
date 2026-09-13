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
// Debug mode toggle
#define DEBUG_MODE true;
extern bool debugMode;

//Set your sleep variable
#define SLEEP_AFTER_MS 60000

//Main Variables and Pins
#define STATUS_EMPTY 0
#define STATUS_GRINDING_IN_PROGRESS 1
#define STATUS_GRINDING_FINISHED 2
#define STATUS_GRINDING_FAILED 3
#define STATUS_IN_MENU 4
#define STATUS_IN_SUBMENU 5
#define STATUS_INFO_MENU 8

#define CUP_WEIGHT 396.1 //war 292
#define CUP_DETECTION_TOLERANCE 10 // 5 grams tolerance above or bellow cup weight to detect it

#define LOADCELL_DOUT_PIN 19
#define LOADCELL_SCK_PIN 18

#define LOADCELL_SCALE_FACTOR 7207 // war 7351

#define TARE_MEASURES 20 // use the average of measure for taring
#define SIGNIFICANT_WEIGHT_CHANGE 10 // 5 grams changes are used to detect a significant change
#define COFFEE_DOSE_WEIGHT 17.5 //war 18
#define COFFEE_DOSE_OFFSET -1.67 //war -2.5
#define MAX_GRINDING_TIME 40000 // 20 seconds diff
#define GRINDING_FAILED_WEIGHT_TO_RESET 150 // force on balance need to be measured to reset grinding

#define GRINDER_ACTIVE_PIN 25 // war 33

#define TARE_MIN_INTERVAL 5 * 1000 // auto-tare at most once every 10 seconds

#define ROTARY_ENCODER_A_PIN 32
#define ROTARY_ENCODER_B_PIN 23
#define ROTARY_ENCODER_BUTTON_PIN 34
#define ROTARY_ENCODER_VCC_PIN -1
#define ROTARY_ENCODER_STEPS 4

// External User Variables
extern volatile bool displayLock; // Add this declaration
extern double scaleWeight;
extern unsigned long scaleLastUpdatedAt;
extern unsigned long lastSignificantWeightChangeAt;
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
extern MenuItem menuItems[];
extern int currentMenuItem;
extern int currentSetting;
extern int sleepTime;
extern bool screenJustWoke;
extern unsigned int shotCount;
extern int debugMenuItemsCount;
extern int currentDebugMenuItem; 