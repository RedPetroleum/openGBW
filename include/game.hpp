#pragma once

#include "config.hpp"

// Games submenu and the games started from it. The knob and the scale are the controllers.

#define GAMES_MENU_SETTING 14 // currentSetting while the Games submenu is shown

void showGamesMenu();            // Draws the Games submenu
void gamesMenuOnTurn(int steps); // Moves the selection in the Games submenu
void gamesMenuOnClick();         // Starts the selected game or returns to the main menu
void gameOnTurn(int steps);      // Knob input from the rotary task while playing
void gameOnClick();              // Button input from the rotary task while playing
void gameLoop();                 // Updates and draws one frame of the running game (called from the display task)
