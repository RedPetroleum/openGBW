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