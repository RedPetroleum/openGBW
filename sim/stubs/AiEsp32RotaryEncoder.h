#pragma once

// Rotary encoder stub driven by the simulator (see sim.cpp)
class AiEsp32RotaryEncoder
{
public:
  AiEsp32RotaryEncoder(int, int, int, int, int) {}
  void begin() {}
  void setup(void (*)(void), void (*)(void) = nullptr) {}
  void setBoundaries(long, long, bool) {}
  void setAcceleration(unsigned long) {}
  void readEncoder_ISR() {}

  long readEncoder() { return position; }
  long encoderChanged()
  {
    long diff = position - lastReadPosition;
    lastReadPosition = position;
    return diff;
  }
  bool isEncoderButtonClicked(unsigned long = 300)
  {
    bool result = clicked;
    clicked = false;
    return result;
  }
  bool isEncoderButtonDown() { return buttonDown; }

  // Simulator input
  long position = 0;
  long lastReadPosition = 0;
  bool clicked = false;
  bool buttonDown = false;
};
