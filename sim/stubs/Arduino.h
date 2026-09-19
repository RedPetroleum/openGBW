#pragma once

// Minimal Arduino API for the display simulator: simulated clock, Serial to stderr, math helpers

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdarg>
#include <algorithm>
#include <functional>
#include "Print.h"

using std::abs;
using std::max;
using std::min;

#define PI 3.1415926535897932384626433832795
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105

#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define radians(deg) ((deg) * DEG_TO_RAD)
#define degrees(rad) ((rad) * RAD_TO_DEG)
#define sq(x) ((x) * (x))

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define PROGMEM
#define IRAM_ATTR

typedef uint8_t byte;
typedef bool boolean;

// Simulated time, advanced by delay() and by the simulator
unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
static inline void yield() {}

long random(long max);
long random(long min, long max);
void randomSeed(unsigned long seed);
long map(long x, long in_min, long in_max, long out_min, long out_max);

static inline void pinMode(uint8_t, uint8_t) {}
static inline void digitalWrite(uint8_t, uint8_t) {}
static inline int digitalRead(uint8_t) { return 0; }

class HardwareSerial : public Print
{
public:
  void begin(unsigned long) {}
  operator bool() const { return true; }
  int available() { return 0; } // no commands reach the simulator
  int read() { return -1; }
  size_t write(uint8_t c) override;
  size_t printf(const char *format, ...);
};
extern HardwareSerial Serial;

// FreeRTOS tasks are not started, the simulator calls the drawing code itself
typedef void *TaskHandle_t;
static inline int xTaskCreatePinnedToCore(void (*)(void *), const char *, uint32_t, void *, int, TaskHandle_t *, int) { return 1; }
