#pragma once

// Load cell stub, the simulator sets scaleWeight directly
class HX711
{
public:
  void begin(int, int) {}
  void set_scale(float scale) { SCALE = scale; }
  float get_scale() { return SCALE; }
  void tare(int = 10) {}
  long read() { return 0; }
  void set_offset(long offset) { OFFSET = offset; }
  long get_offset() { return OFFSET; }
  bool wait_ready_timeout(unsigned long = 1000) { return true; }
  float get_units(int = 1) { return 0; }

private:
  long OFFSET = 0;
  float SCALE = 1;
};
