#pragma once

// Load cell stub, the simulator sets scaleWeight directly
class HX711
{
public:
  void begin(int, int) {}
  void set_scale(float) {}
  void tare(int = 10) {}
  bool wait_ready_timeout(unsigned long = 1000) { return true; }
  float get_units(int = 1) { return 0; }
};
