#pragma once

#include "config.hpp"
#include <SPI.h>
#include <U8g2lib.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C screen;

void setupDisplay();
void refreshDisplay(); // Draws the current state once
void CenterPrintToScreen(char const *str, u8g2_uint_t y);
void LeftPrintToScreen(char const *str, u8g2_uint_t y);
void LeftPrintActiveToScreen(char const *str, u8g2_uint_t y);
void showCupWeightSetScreen(double cupWeight);
void showInfoMenu();
void wakeScreen();
bool displayAsleep();
void showDebugModeStatus(bool debugMode);
void showDebugMenu();
void handleDebugMenuAction();
void showStyleMenu();            // Draws the Style submenu
void styleMenuOnTurn(int steps); // Moves the selection in the Style submenu
void styleMenuOnClick();         // Opens the selected style setting or returns to the main menu
void setupMenuItems();               // Builds the menu from the items that are visible
void showCalibrateMenu();            // Draws the Calibrate submenu
void calibrateMenuOnTurn(int steps); // Moves the selection in it
void calibrateMenuOnClick();         // Opens the selected setting or returns to the main menu
void finishedScreenOnTurn(int steps); // Pages between the two pages of the finished screen
void recordSwitchOffReading(double grams); // one raw reading for the picture of the switch-off
void infoMenuOnTurn(int steps);       // Pages through the lines of the Info Menu
void resetInfoMenu();                 // ... which always opens on the first of them
void showNoiseScreen();               // Draws the noise measurement of the Calibrate submenu
void startNoiseMeasurement();         // Starts its twenty seconds over
void noiseSample(double grams);       // one raw reading for it, called for every single one
bool noiseMeasurementReady();         // whether its bar has run out, from when its sigma counts ...
double noiseMeasuredSigma();          // ... and the scatter it has found so far
void showNoiseSaveScreen();           // Asks whether to keep that sigma
void noiseSaveOnTurn(int steps);      // ... turning switches between the two answers ...
void resetNoiseSave();                // ... which always opens on Save ...
bool noiseSaveSelected();             // ... and this is the one standing when it is clicked
