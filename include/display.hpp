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
void finishedScreenOnTurn(int steps); // Pages between the two pages of the finished screen