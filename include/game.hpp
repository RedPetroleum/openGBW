#pragma once

#include "config.hpp"

// Comet Blaster: score points for surviving and destroying comets flying in from the right.
// The knob moves the ship, pressing the scale fires the laser.

void gameEnter();            // Starts a new game (called from the menu)
void gameOnTurn(int steps);  // Knob input from the rotary task
void gameOnClick();          // Button input from the rotary task
void gameLoop();             // Updates and draws one frame (called from the display task)
