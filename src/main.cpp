#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>


#include "display.hpp"
#include "scale.hpp"
#include "config.hpp"

// Definitions of global variables (memory allocated here)
Preferences preferences;             // Preferences object
HX711 loadcell;                      // HX711 load cell object
SimpleKalmanFilter kalmanFilter(0.02, 0.02, 0.01); // Kalman filter for weight smoothing

TaskHandle_t ScaleTask = nullptr;    // Initialize task handles to nullptr
TaskHandle_t ScaleStatusTask = nullptr;

volatile bool displayLock = false; 
/*
Main runner for Screen setup and work
*/
void setup() {
  Serial.begin(115200);
  while(!Serial){delay(100);}

  
  setupDisplay();
  setupScale();

  Serial.println();
  Serial.println("******************************************************");
}

void loop() {
  delay(1000);
}
