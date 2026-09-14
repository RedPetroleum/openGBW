#pragma once

#include <cstddef>
#include <cstdint>

// Preferences stub: nothing is stored, every get returns the default, so the firmware defaults are used
class Preferences
{
public:
  bool begin(const char *, bool = false) { return true; }
  void end() {}
  double getDouble(const char *, double defaultValue = 0) { return defaultValue; }
  bool getBool(const char *, bool defaultValue = false) { return defaultValue; }
  uint32_t getUInt(const char *, uint32_t defaultValue = 0) { return defaultValue; }
  int32_t getInt(const char *, int32_t defaultValue = 0) { return defaultValue; }
  size_t getBytesLength(const char *) { return 0; }
  size_t getBytes(const char *, void *, size_t) { return 0; }
  size_t putDouble(const char *, double) { return sizeof(double); }
  size_t putBool(const char *, bool) { return 1; }
  size_t putUInt(const char *, uint32_t) { return 4; }
  size_t putInt(const char *, int32_t) { return 4; }
  size_t putBytes(const char *, const void *, size_t len) { return len; }
};
